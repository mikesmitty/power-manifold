#include "https.h"

#include <stdio.h>
#include <string.h>

#include "hardware/flash.h"
#include "pico/flash.h"
#include "pico/rand.h"

#include "lwip/altcp_tls.h"
#include "mbedtls/asn1.h"
#include "mbedtls/pk.h"
#include "mbedtls/sha256.h"
#include "mbedtls/ssl.h"
#include "mbedtls/x509_crt.h"

#include "civil_time.h"
#include "flash_map.h"
#include "jsonlite.h"
#include "net.h"
#include "settings.h"
#include "tls_bundle.h"

// Two copies after the settings pair and the fault ring (fault_log.c) in
// the data partition. A save writes the copy not holding the newest record.
#define TLS_OFFSET (18 * FLASH_SECTOR_SIZE)
#define TLS_SLOTS  2
_Static_assert(TLS_RECORD_MAX % FLASH_SECTOR_SIZE == 0, "TLS record must fill whole sectors");

// A configuration with the certificate it was built from. The one new
// connections get is "live"; the one before it lives on until the last
// connection made with it closes. Three cover a renewal arriving while a
// connection from two certificates ago is still open.
typedef struct {
    struct altcp_tls_config *conf;
    mbedtls_x509_crt chain;
    mbedtls_pk_context key;
    uint8_t refs; // connections using conf
    bool used;
} gen_t;

#define GENS 3
static gen_t gens[GENS];
static gen_t *live;

static bool available;   // the data partition has room for the copies
static uint32_t region_off;
static int cur_slot = -1; // the copy holding the newest record, -1 none
static uint32_t cur_seq;

static char stage[HTTPS_UPLOAD_MAX];

// What the console, the API and the health line say about the live one
static char names[256];
static char issuer[64];
static uint32_t expires_epoch;
static uint8_t fingerprint[32];

static int rng(void *ctx, unsigned char *out, size_t n) {
    (void)ctx;
    while (n) {
        uint64_t r = get_rand_64();
        size_t k = n < sizeof(r) ? n : sizeof(r);
        memcpy(out, &r, k);
        out += k;
        n -= k;
    }
    return 0;
}

static uint32_t x509_epoch(const mbedtls_x509_time *t) {
    if (t->year < 1970) return 0;
    return civil_days((unsigned)t->year, (unsigned)t->mon, (unsigned)t->day) * 86400u +
           (uint32_t)t->hour * 3600u + (uint32_t)t->min * 60u + (uint32_t)t->sec;
}

static void date_text(char *out, size_t cap, uint32_t epoch) {
    unsigned y, m, d;
    civil_from_days(epoch / 86400u, &y, &m, &d);
    snprintf(out, cap, "%04u-%02u-%02u", y, m, d);
}

// ---- flash ------------------------------------------------------------

typedef struct {
    uint32_t offset;
    const uint8_t *data; // NULL: erase only
} flop_t;

static void do_flop(void *param) {
    const flop_t *op = (const flop_t *)param;
    flash_range_erase(op->offset, TLS_RECORD_MAX);
    if (op->data) flash_range_program(op->offset, op->data, TLS_RECORD_MAX);
}

static bool slot_write(int slot, const uint8_t *data) {
    flop_t op = {.offset = region_off + (uint32_t)slot * TLS_RECORD_MAX, .data = data};
    return flash_safe_execute(do_flop, &op, 500) == PICO_OK;
}

static const uint8_t *slot_ptr(int slot) {
    return (const uint8_t *)flash_map_xip_ptr(region_off + (uint32_t)slot * TLS_RECORD_MAX);
}

// ---- configurations ---------------------------------------------------

static void gen_free(gen_t *g) {
    if (g->conf) altcp_tls_free_config(g->conf);
    if (g->used) {
        mbedtls_x509_crt_free(&g->chain);
        mbedtls_pk_free(&g->key);
    }
    memset(g, 0, sizeof(*g));
}

static gen_t *gen_spare(void) {
    for (int i = 0; i < GENS; i++)
        if (!gens[i].used) return &gens[i];
    return NULL;
}

// Parse the bundle into g and build the server configuration on it. NULL,
// or what is wrong (then g is freed); *fault for running out of memory.
static const char *gen_build(gen_t *g, const tls_bundle_t *b, bool *fault) {
    memset(g, 0, sizeof(*g));
    g->used = true;
    mbedtls_x509_crt_init(&g->chain);
    mbedtls_pk_init(&g->key);
    const char *err = NULL;
    for (int i = 0; i < b->n_certs && !err; i++)
        if (mbedtls_x509_crt_parse_der(&g->chain, b->cert[i], b->cert_len[i]) != 0)
            err = i ? "a certificate in the chain does not parse" : "the certificate does not parse";
    if (!err && mbedtls_pk_parse_key(&g->key, b->key, b->key_len, NULL, 0, rng, NULL) != 0)
        err = "the private key does not parse";
    if (!err && !mbedtls_pk_can_do(&g->key, MBEDTLS_PK_ECKEY))
        err = "RSA keys are not supported: issue an ECDSA certificate (P-256 or P-384)";
    if (!err && mbedtls_pk_check_pair(&g->chain.pk, &g->key, rng, NULL) != 0)
        err = "the first certificate is not for this key: put the server's own certificate first";
    if (!err && !(g->conf = altcp_tls_create_config_server(0))) {
        err = "out of memory for the TLS configuration";
        *fault = true;
    }
    if (!err) {
        // lwIP's glue keeps the mbedTLS configuration as the first member of
        // its struct (see mqtt_tls.c). The compiled-in verification mode is
        // the broker link's "required", which on a server would demand a
        // client certificate: browsers have none.
        mbedtls_ssl_config *sc = (mbedtls_ssl_config *)g->conf;
        mbedtls_ssl_conf_authmode(sc, MBEDTLS_SSL_VERIFY_NONE);
        if (mbedtls_ssl_conf_own_cert(sc, &g->chain, &g->key) != 0) {
            err = "out of memory for the TLS configuration";
            *fault = true;
        }
    }
    if (err) gen_free(g);
    return err;
}

// The DNS names in the leaf's subjectAltName, space-separated into out
static void leaf_names(const mbedtls_x509_crt *crt, char *out, size_t cap) {
    size_t n = 0;
    out[0] = '\0';
    for (const mbedtls_x509_sequence *s = &crt->subject_alt_names; s && s->buf.p; s = s->next) {
        if ((s->buf.tag & MBEDTLS_ASN1_TAG_CLASS_MASK) != MBEDTLS_ASN1_CONTEXT_SPECIFIC ||
            (s->buf.tag & MBEDTLS_ASN1_TAG_VALUE_MASK) != MBEDTLS_X509_SAN_DNS_NAME)
            continue;
        bool ok = s->buf.len > 0;
        for (size_t i = 0; i < s->buf.len && ok; i++) {
            char c = (char)s->buf.p[i];
            ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                 c == '-' || c == '.' || c == '*';
        }
        if (!ok || n + (n ? 1 : 0) + s->buf.len >= cap) continue;
        if (n) out[n++] = ' ';
        for (size_t i = 0; i < s->buf.len; i++) {
            char c = (char)s->buf.p[i];
            out[n++] = (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
        }
        out[n] = '\0';
    }
}

static void info_take(const gen_t *g) {
    names[0] = issuer[0] = '\0';
    expires_epoch = 0;
    memset(fingerprint, 0, sizeof(fingerprint));
    if (!g) return;
    const mbedtls_x509_crt *leaf = &g->chain;
    leaf_names(leaf, names, sizeof(names));
    char dn[160];
    if (mbedtls_x509_dn_gets(dn, sizeof(dn), &leaf->issuer) > 0) {
        const char *cn = strstr(dn, "CN=");
        snprintf(issuer, sizeof(issuer), "%s", cn ? cn + 3 : dn);
        char *comma = strchr(issuer, ',');
        if (comma) *comma = '\0';
    }
    expires_epoch = x509_epoch(&leaf->valid_to);
    mbedtls_sha256(leaf->raw.p, leaf->raw.len, fingerprint, 0);
}

static void make_live(gen_t *g) {
    gen_t *old = live;
    live = g;
    if (old && !old->refs) gen_free(old);
    info_take(g);
}

void https_conn_opened(struct altcp_tls_config *conf) {
    for (int i = 0; i < GENS; i++)
        if (gens[i].used && gens[i].conf == conf) gens[i].refs++;
}

void https_conn_closed(struct altcp_tls_config *conf) {
    for (int i = 0; i < GENS; i++) {
        gen_t *g = &gens[i];
        if (!g->used || g->conf != conf) continue;
        if (g->refs) g->refs--;
        if (!g->refs && g != live) gen_free(g);
    }
}

// ---- the API ------------------------------------------------------------

void https_init(void) {
    uint32_t off, size;
    if (!flash_map_find(FLASH_MAP_ID_DATA, &off, &size) ||
        size < TLS_OFFSET + TLS_SLOTS * TLS_RECORD_MAX)
        return;
    region_off = off + TLS_OFFSET;
    available = true;

    tls_bundle_t b, best = {0};
    uint32_t seq;
    for (int i = 0; i < TLS_SLOTS; i++) {
        if (!tls_record_decode(slot_ptr(i), TLS_RECORD_MAX, &b, &seq)) continue;
        if (cur_slot < 0 || seq > cur_seq) {
            cur_slot = i;
            cur_seq = seq;
            best = b;
        }
    }
    if (cur_slot < 0) return;
    // the DER is parsed with copies: a later save rewrites the flash under it
    bool fault = false;
    const char *err = gen_build(&gens[0], &best, &fault);
    if (err) {
        printf("https: the stored certificate does not load: %s\n", err);
        return;
    }
    make_live(&gens[0]);
    char date[16];
    date_text(date, sizeof(date), expires_epoch);
    printf("https: certificate for %s, expires %s\n", names[0] ? names : "(no names)", date);
}

char *https_upload_buffer(void) {
    return stage;
}

const char *https_install(size_t len, bool *server_fault) {
    static uint8_t der[TLS_RECORD_MAX];
    static char why[112];
    *server_fault = false;
    if (!available) {
        *server_fault = true;
        return "this board has no data partition for a certificate";
    }
    if (len >= sizeof(stage)) return "the upload is too large";
    stage[len] = '\0';

    tls_bundle_t b;
    const char *err = tls_bundle_parse(stage, len, der, sizeof(der), &b);
    if (err) return err;
    gen_t *g = gen_spare();
    if (!g) return "connections still use the earlier certificates: retry in a minute";
    if ((err = gen_build(g, &b, server_fault))) return err;

    char leaf[256];
    leaf_names(&g->chain, leaf, sizeof(leaf));
    uint32_t now = net_epoch(), until = x509_epoch(&g->chain.valid_to);
    uint32_t from = x509_epoch(&g->chain.valid_from);
    char date[16];
    if (!leaf[0]) {
        err = "the certificate names no host (no DNS name in subjectAltName)";
    } else if (now && until <= now) {
        date_text(date, sizeof(date), until);
        snprintf(why, sizeof(why), "the certificate expired on %s", date);
        err = why;
    } else if (now && from > now + 86400u) {
        date_text(date, sizeof(date), from);
        snprintf(why, sizeof(why), "the certificate is not valid until %s", date);
        err = why;
    }
    if (err) {
        gen_free(g);
        return err;
    }

    // the PEM text is no longer needed: the record is built in its place
    uint8_t *rec = (uint8_t *)stage;
    size_t n = tls_record_encode(&b, cur_seq + 1, rec, TLS_RECORD_MAX);
    if (!n) {
        gen_free(g);
        return "the key and certificates are larger than 8 KB";
    }
    memset(rec + n, 0xFF, TLS_RECORD_MAX - n);
    int slot = cur_slot < 0 ? 0 : cur_slot ^ 1;
    if (!slot_write(slot, rec)) {
        gen_free(g);
        *server_fault = true;
        return "flash write failed";
    }
    cur_slot = slot;
    cur_seq++;
    make_live(g);
    date_text(date, sizeof(date), expires_epoch);
    printf("https: certificate for %s installed, expires %s\n", names, date);
    return NULL;
}

const char *https_remove(void) {
    if (g_settings.https) return "turn HTTPS off first";
    if (!available || cur_slot < 0) return "no certificate is installed";
    bool ok = true;
    for (int i = 0; i < TLS_SLOTS; i++)
        if (!slot_write(i, NULL)) ok = false;
    cur_slot = -1;
    if (live) {
        gen_t *old = live;
        live = NULL;
        if (!old->refs) gen_free(old);
    }
    info_take(NULL);
    printf("https: certificate removed\n");
    return ok ? NULL : "flash erase failed";
}

bool https_wipe(void) {
    if (!available) return true;
    bool ok = true;
    for (int i = 0; i < TLS_SLOTS; i++)
        if (!slot_write(i, NULL)) ok = false;
    return ok;
}

struct altcp_tls_config *https_config(void) {
    return live ? live->conf : NULL;
}

bool https_enforced(void) {
    return g_settings.https && live;
}

const char *https_names(void) {
    return names;
}

uint32_t https_expires(void) {
    return live ? expires_epoch : 0;
}

bool https_days_left(uint32_t now_epoch, int32_t *days) {
    if (!live) return false;
    int64_t left = (int64_t)expires_epoch - (int64_t)now_epoch;
    *days = (int32_t)(left >= 0 ? left / 86400 : -((-left + 86399) / 86400));
    return true;
}

const char *https_problem(uint32_t now_epoch) {
    static char text[64];
    if (!g_settings.https) return NULL;
    if (!live) return "HTTPS on but no certificate, serving plain HTTP";
    int32_t days;
    if (!now_epoch || !https_days_left(now_epoch, &days)) return NULL;
    if (now_epoch >= expires_epoch) return "HTTPS certificate expired";
    if (days > HTTPS_EXPIRY_WARN_DAYS) return NULL;
    snprintf(text, sizeof(text), "HTTPS certificate expires in %ld day%s", (long)days, days == 1 ? "" : "s");
    return text;
}

static size_t put_fingerprint(char *out, size_t cap) {
    size_t o = 0;
    for (int i = 0; i < 32 && o + 3 < cap; i++)
        o += (size_t)snprintf(out + o, cap - o, "%02X%s", fingerprint[i], i < 31 ? ":" : "");
    return o;
}

bool https_describe(char *out, size_t cap) {
    if (!live) return false;
    char date[16], fp[100];
    date_text(date, sizeof(date), expires_epoch);
    put_fingerprint(fp, sizeof(fp));
    snprintf(out, cap, "%s; issued by %s; expires %s; SHA-256 %s", names[0] ? names : "(no names)",
             issuer[0] ? issuer : "?", date, fp);
    return true;
}

size_t https_json(char *out, size_t cap, uint32_t now_epoch) {
    size_t o = (size_t)snprintf(out, cap, "{\"enabled\":%s,\"active\":%s,\"installed\":%s",
                                g_settings.https ? "true" : "false", https_enforced() ? "true" : "false",
                                live ? "true" : "false");
    if (live && o < cap) {
        char date[16], fp[100], esc[sizeof(issuer) * 2];
        date_text(date, sizeof(date), expires_epoch);
        put_fingerprint(fp, sizeof(fp));
        json_escape(esc, sizeof(esc), issuer);
        o += (size_t)snprintf(out + o, cap - o, ",\"names\":[");
        // names hold only letters, digits, dots, hyphens and stars: no escaping
        bool first = true;
        for (const char *p = names; *p && o < cap;) {
            const char *e = strchr(p, ' ');
            size_t len = e ? (size_t)(e - p) : strlen(p);
            o += (size_t)snprintf(out + o, cap - o, "%s\"%.*s\"", first ? "" : ",", (int)len, p);
            first = false;
            p += len;
            while (*p == ' ') p++;
        }
        int32_t days;
        char daysf[16] = "null";
        if (now_epoch && https_days_left(now_epoch, &days)) snprintf(daysf, sizeof(daysf), "%ld", (long)days);
        if (o < cap)
            o += (size_t)snprintf(out + o, cap - o,
                                  "],\"issuer\":\"%s\",\"expires\":\"%s\",\"days_left\":%s,\"sha256\":\"%s\"",
                                  esc, date, daysf, fp);
    }
    if (o < cap) o += (size_t)snprintf(out + o, cap - o, "}");
    return o < cap ? o : 0;
}
