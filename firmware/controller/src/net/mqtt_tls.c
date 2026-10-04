#include "mqtt_tls.h"

#include <stdio.h>
#include <string.h>

#include "pico/time.h"

#include "lwip/altcp_tls.h"
#include "lwip/apps/mqtt.h"
#include "lwip/apps/mqtt_priv.h"
#include "mbedtls/platform_time.h"
#include "mbedtls/sha256.h"
#include "mbedtls/ssl.h"
#include "mbedtls/x509_crt.h"

#include "ip4_text.h"
#include "letsencrypt_roots.h"
#include "settings.h"

// mbedTLS asks the platform for a millisecond clock (pwrman_mbedtls_config.h
// sets MBEDTLS_PLATFORM_MS_TIME_ALT); it only times handshake steps with it.
mbedtls_ms_time_t mbedtls_ms_time(void) {
    return (mbedtls_ms_time_t)to_ms_since_boot(get_absolute_time());
}

const char *mqtt_ca_check(const uint8_t *der, size_t len) {
    mbedtls_x509_crt crt;
    mbedtls_x509_crt_init(&crt);
    int ret = mbedtls_x509_crt_parse_der(&crt, der, len);
    mbedtls_x509_crt_free(&crt);
    return ret == 0 ? NULL : "mqtt_ca: not an X.509 certificate";
}

// The broker is configured by a name rather than an address
static bool broker_by_name(void) {
    uint32_t addr;
    return !ip4_parse(g_settings.mqtt_host, &addr);
}

// A verified link trusts the installed certificate when there is one, else
// the built-in roots
static bool trusts_installed(void) {
    return g_settings.mqtt_ca_len != 0;
}

const char *mqtt_tls_mode_str(void) {
    switch (g_settings.mqtt_tls) {
    case MQTT_TLS_VERIFIED:   return trusts_installed() ? "verified, installed certificate" : "verified, Let's Encrypt";
    case MQTT_TLS_UNVERIFIED: return "unverified";
    default:                  return "plain";
    }
}

const char *mqtt_tls_blocker(void) {
    if (g_settings.mqtt_tls != MQTT_TLS_VERIFIED || trusts_installed() || broker_by_name()) return NULL;
    return "a verified link to a broker by address needs its certificate installed, or 'mqtt tls unverified'";
}

bool mqtt_tls_wants_clock(void) {
    return g_settings.mqtt_tls == MQTT_TLS_VERIFIED;
}

bool mqtt_ca_describe(char *out, size_t cap) {
    if (!g_settings.mqtt_ca_len) return false;
    mbedtls_x509_crt crt;
    mbedtls_x509_crt_init(&crt);
    if (mbedtls_x509_crt_parse_der(&crt, g_settings.mqtt_ca, g_settings.mqtt_ca_len) != 0) {
        mbedtls_x509_crt_free(&crt);
        snprintf(out, cap, "the installed certificate does not parse");
        return true;
    }
    char subject[72];
    if (mbedtls_x509_dn_gets(subject, sizeof(subject), &crt.subject) < 0) strcpy(subject, "?");
    uint8_t fp[32];
    mbedtls_sha256(g_settings.mqtt_ca, g_settings.mqtt_ca_len, fp, 0);
    size_t o = (size_t)snprintf(out, cap, "%s; expires %04d-%02d-%02d; SHA-256 ", subject,
                                crt.valid_to.year, crt.valid_to.mon, crt.valid_to.day);
    for (int i = 0; i < 32 && o + 3 < cap; i++)
        o += (size_t)snprintf(out + o, cap - o, "%02X%s", fp[i], i < 31 ? ":" : "");
    mbedtls_x509_crt_free(&crt);
    return true;
}

// The Let's Encrypt roots, parsed once and in place: the DER stays in flash
// and mbedTLS keeps only its pointers into it.
static mbedtls_x509_crt le_roots;
static int le_roots_state; // 0 not yet parsed, 1 ready, -1 refused

static bool le_roots_ready(void) {
    if (le_roots_state) return le_roots_state > 0;
    le_roots_state = -1;
    if (LETSENCRYPT_ROOT_COUNT == 0) {
        printf("mqtt: no Let's Encrypt roots are built into this image\n");
        return false;
    }
    mbedtls_x509_crt_init(&le_roots);
    for (size_t i = 0; i < LETSENCRYPT_ROOT_COUNT; i++) {
        int ret = mbedtls_x509_crt_parse_der_nocopy(&le_roots, letsencrypt_roots[i].der, letsencrypt_roots[i].len);
        if (ret != 0) {
            printf("mqtt: built-in root %s does not parse (-0x%04x)\n", letsencrypt_roots[i].name, (unsigned)-ret);
            mbedtls_x509_crt_free(&le_roots);
            return false;
        }
    }
    le_roots_state = 1;
    return true;
}

// What "not trusted" means for the configuration in use, for the log below
static const char *untrusted_why = "is not trusted";

// mbedTLS reports each certificate it refuses here before the handshake
// fails, so the console says why rather than just "disconnected".
static int verify_cb(void *arg, mbedtls_x509_crt *crt, int depth, uint32_t *flags) {
    (void)arg;
    (void)crt;
    if (!*flags) return 0;
    const char *why = *flags & MBEDTLS_X509_BADCERT_NOT_TRUSTED ? untrusted_why
                    : *flags & MBEDTLS_X509_BADCERT_CN_MISMATCH ? "does not carry the broker's name"
                    : *flags & MBEDTLS_X509_BADCERT_EXPIRED     ? "has expired"
                    : *flags & MBEDTLS_X509_BADCERT_FUTURE      ? "is not valid yet"
                                                                 : "was refused";
    printf("mqtt: broker certificate (depth %d) %s (flags 0x%lx)\n", depth, why, (unsigned long)*flags);
    return 0;
}

// Two generations of configuration: a connection that is still closing may
// hold the last one, so a new one takes the slot of the one before that.
// The installed certificate is parsed afresh for each generation, because
// the settings copy may change while a connection still uses the old one.
typedef struct {
    struct altcp_tls_config *conf;
    mbedtls_x509_crt installed;
    bool has_installed;
} tls_gen_t;

static tls_gen_t gens[2];
static int cur_gen;

static void release(tls_gen_t *g) {
    if (g->conf) altcp_tls_free_config(g->conf);
    if (g->has_installed) mbedtls_x509_crt_free(&g->installed);
    memset(g, 0, sizeof(*g));
}

struct altcp_tls_config *mqtt_tls_config(void) {
    cur_gen ^= 1;
    tls_gen_t *g = &gens[cur_gen];
    release(g);
    g->conf = altcp_tls_create_config_client(NULL, 0);
    if (!g->conf) return NULL;
    // lwIP's glue keeps the mbedTLS configuration as the first member of the
    // struct it hands back and defines that struct only in its own source
    // file, so this cast is how the trust chain and the verification mode
    // are reached. The compiled-in mode is "required" (lwipopts.h); the
    // unverified state relaxes it, the verified state supplies the chain
    // and the reason log above.
    mbedtls_ssl_config *ssl_conf = (mbedtls_ssl_config *)g->conf;
    if (g_settings.mqtt_tls == MQTT_TLS_UNVERIFIED) {
        mbedtls_ssl_conf_authmode(ssl_conf, MBEDTLS_SSL_VERIFY_NONE);
        return g->conf;
    }
    if (trusts_installed()) {
        mbedtls_x509_crt_init(&g->installed);
        g->has_installed = true;
        if (mbedtls_x509_crt_parse_der(&g->installed, g_settings.mqtt_ca, g_settings.mqtt_ca_len) != 0) {
            printf("mqtt: the installed certificate does not parse\n");
            release(g);
            return NULL;
        }
        mbedtls_ssl_conf_ca_chain(ssl_conf, &g->installed, NULL);
        untrusted_why = "does not lead to the installed certificate";
    } else {
        if (!broker_by_name() || !le_roots_ready()) {
            release(g);
            return NULL;
        }
        mbedtls_ssl_conf_ca_chain(ssl_conf, &le_roots, NULL);
        untrusted_why = "does not lead to a Let's Encrypt root";
    }
    mbedtls_ssl_conf_verify(ssl_conf, verify_cb, NULL);
    return g->conf;
}

void mqtt_tls_set_hostname(struct mqtt_client_s *client) {
    if (!client->conn || !broker_by_name()) return;
    mbedtls_ssl_context *ssl = (mbedtls_ssl_context *)altcp_tls_context(client->conn);
    if (ssl) mbedtls_ssl_set_hostname(ssl, g_settings.mqtt_host);
}
