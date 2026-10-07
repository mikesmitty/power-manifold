#include "tls_bundle.h"

#include <string.h>
#include <strings.h>

#include "pem.h"

#define RECORD_MAGIC   0x4C544D50u // "PMTL"
#define RECORD_VERSION 1

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t key_len;
    uint32_t seq;
    uint8_t  n_certs;
    uint8_t  pad[3];
    uint16_t cert_len[TLS_CERTS_MAX];
    uint32_t crc; // over this header up to here, then the payload
} record_hdr_t;

_Static_assert(sizeof(record_hdr_t) == 28, "TLS record header moved");

static uint32_t crc32_update(uint32_t crc, const uint8_t *data, size_t len) {
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return crc;
}

static uint32_t record_crc(const record_hdr_t *h, const uint8_t *payload, size_t len) {
    uint32_t crc = crc32_update(0xFFFFFFFFu, (const uint8_t *)h, offsetof(record_hdr_t, crc));
    return ~crc32_update(crc, payload, len);
}

// The next "-----BEGIN <label>-----" at or after p, before end.
static const char *find_begin(const char *p, const char *end) {
    static const char tag[] = "-----BEGIN ";
    for (; p + sizeof(tag) - 1 <= end; p++)
        if (!memcmp(p, tag, sizeof(tag) - 1)) return p;
    return NULL;
}

static const char *find_text(const char *p, const char *end, const char *s, size_t n) {
    for (; p + n <= end; p++)
        if (!memcmp(p, s, n)) return p;
    return NULL;
}

static bool label_is(const char *label, size_t n, const char *want) {
    return n == strlen(want) && !memcmp(label, want, n);
}

const char *tls_bundle_parse(const char *pem, size_t len, uint8_t *der, size_t cap,
                             tls_bundle_t *b) {
    memset(b, 0, sizeof(*b));
    const char *end = pem + len;
    size_t used = 0;
    for (const char *p = find_begin(pem, end); p; p = find_begin(p, end)) {
        const char *label = p + 11;
        const char *label_end = find_text(label, end, "-----", 5);
        if (!label_end) return "a BEGIN line is not closed";
        size_t ln = (size_t)(label_end - label);
        const char *body = label_end + 5;

        // the END line must name the same label
        char end_line[48];
        if (ln + 14 > sizeof(end_line)) return "a PEM label is too long";
        memcpy(end_line, "-----END ", 9);
        memcpy(end_line + 9, label, ln);
        memcpy(end_line + 9 + ln, "-----", 5);
        const char *body_end = find_text(body, end, end_line, ln + 14);
        if (!body_end) return "a PEM block has no END line";
        p = body_end + ln + 14;

        bool cert = label_is(label, ln, "CERTIFICATE");
        bool key = label_is(label, ln, "EC PRIVATE KEY") || label_is(label, ln, "PRIVATE KEY");
        if (label_is(label, ln, "ENCRYPTED PRIVATE KEY"))
            return "the private key is encrypted: send it without a passphrase";
        if (label_is(label, ln, "RSA PRIVATE KEY"))
            return "RSA keys are not supported: issue an ECDSA certificate (P-256 or P-384)";
        if (!cert && !key) continue; // EC PARAMETERS and the like
        if (find_text(body, body_end, "Proc-Type:", 10))
            return "the private key is encrypted: send it without a passphrase";
        if (cert && b->n_certs == TLS_CERTS_MAX) return "more than 4 certificates";
        if (key && b->key) return "more than one private key";

        size_t n = pem_base64_decode(body, (size_t)(body_end - body), der + used, cap - used);
        if (!n) return used >= cap ? "the key and certificates are too large" : "a PEM block is not valid base64";
        if (n > UINT16_MAX) return "the key and certificates are too large";
        if (cert) {
            if (!der_cert_shape_ok(der + used, n)) return "a certificate is not DER";
            b->cert[b->n_certs] = der + used;
            b->cert_len[b->n_certs++] = (uint16_t)n;
        } else {
            b->key = der + used;
            b->key_len = (uint16_t)n;
        }
        used += n;
    }
    if (!b->key) return "no private key: send the key and the certificate chain together";
    if (!b->n_certs) return "no certificate: send the key and the certificate chain together";
    return NULL;
}

size_t tls_record_encode(const tls_bundle_t *b, uint32_t seq, uint8_t *out, size_t cap) {
    size_t len = sizeof(record_hdr_t) + b->key_len;
    for (int i = 0; i < b->n_certs; i++) len += b->cert_len[i];
    if (len > cap || !b->key_len || !b->n_certs || b->n_certs > TLS_CERTS_MAX) return 0;

    record_hdr_t h;
    memset(&h, 0, sizeof(h));
    h.magic = RECORD_MAGIC;
    h.version = RECORD_VERSION;
    h.key_len = b->key_len;
    h.seq = seq;
    h.n_certs = b->n_certs;
    for (int i = 0; i < b->n_certs; i++) h.cert_len[i] = b->cert_len[i];

    uint8_t *payload = out + sizeof(h);
    size_t off = 0;
    memmove(payload + off, b->key, b->key_len);
    off += b->key_len;
    for (int i = 0; i < b->n_certs; i++) {
        memmove(payload + off, b->cert[i], b->cert_len[i]);
        off += b->cert_len[i];
    }
    h.crc = record_crc(&h, payload, off);
    memcpy(out, &h, sizeof(h));
    return len;
}

bool tls_record_decode(const uint8_t *rec, size_t cap, tls_bundle_t *b, uint32_t *seq) {
    record_hdr_t h;
    if (cap < sizeof(h)) return false;
    memcpy(&h, rec, sizeof(h));
    if (h.magic != RECORD_MAGIC || h.version != RECORD_VERSION) return false;
    if (!h.key_len || !h.n_certs || h.n_certs > TLS_CERTS_MAX) return false;
    size_t len = h.key_len;
    for (int i = 0; i < h.n_certs; i++) {
        if (!h.cert_len[i]) return false;
        len += h.cert_len[i];
    }
    if (len > cap - sizeof(h)) return false;
    const uint8_t *payload = rec + sizeof(h);
    if (record_crc(&h, payload, len) != h.crc) return false;

    memset(b, 0, sizeof(*b));
    b->key = payload;
    b->key_len = h.key_len;
    size_t off = h.key_len;
    b->n_certs = h.n_certs;
    for (int i = 0; i < h.n_certs; i++) {
        b->cert[i] = payload + off;
        b->cert_len[i] = h.cert_len[i];
        off += h.cert_len[i];
    }
    if (seq) *seq = h.seq;
    return true;
}

bool tls_name_match(const char *pattern, size_t plen, const char *name, size_t n) {
    if (!plen || !n) return false;
    if (plen > 2 && pattern[0] == '*' && pattern[1] == '.') {
        // the wildcard stands for one whole label: "a.example.com" matches
        // "*.example.com", "example.com" and "a.b.example.com" do not
        const char *dot = memchr(name, '.', n);
        if (!dot || dot == name) return false;
        size_t rest = n - (size_t)(dot - name);
        return rest == plen - 1 && !strncasecmp(dot, pattern + 1, rest);
    }
    return plen == n && !strncasecmp(pattern, name, n);
}
