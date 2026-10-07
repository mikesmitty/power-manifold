#include <string.h>

#include "civil_time.h"
#include "microtest.h"
#include "net/tls_bundle.h"
#include "pem.h"

static char text[16384];
static uint8_t der[TLS_RECORD_MAX];
static uint8_t rec[TLS_RECORD_MAX];

// A DER blob of n bytes with a certificate's outer shape (one SEQUENCE)
static size_t fake_der(uint8_t *out, size_t n, uint8_t fill) {
    size_t body = n - 4;
    out[0] = 0x30;
    out[1] = 0x82;
    out[2] = (uint8_t)(body >> 8);
    out[3] = (uint8_t)body;
    memset(out + 4, fill, body);
    return n;
}

// Append one PEM block with this label, its base64 wrapped at 64 columns
static void add_block(const char *label, const uint8_t *data, size_t n) {
    static char block[12000];
    size_t len = der_to_pem(data, n, block, sizeof(block), "\n");
    if (!len) return;
    // der_to_pem writes the CERTIFICATE armour: swap in the label
    const char *b64 = strchr(block, '\n') + 1;
    const char *tail = strstr(b64, "-----END");
    char *t = text + strlen(text);
    t += sprintf(t, "-----BEGIN %s-----\n", label);
    memcpy(t, b64, (size_t)(tail - b64));
    t += tail - b64;
    sprintf(t, "-----END %s-----\n", label);
}

static const char *parse(tls_bundle_t *b) {
    return tls_bundle_parse(text, strlen(text), der, sizeof(der), b);
}

// acme.sh's key file joined to its fullchain file, and the other way round
static void test_key_and_chain(void) {
    uint8_t leaf[600], mid[500], key[121];
    fake_der(leaf, sizeof(leaf), 0x11);
    fake_der(mid, sizeof(mid), 0x22);
    memset(key, 0x33, sizeof(key));
    tls_bundle_t b;

    text[0] = '\0';
    add_block("EC PRIVATE KEY", key, sizeof(key));
    add_block("CERTIFICATE", leaf, sizeof(leaf));
    add_block("CERTIFICATE", mid, sizeof(mid));
    MT_ASSERT(parse(&b) == NULL);
    MT_ASSERT_EQ(b.key_len, sizeof(key));
    MT_ASSERT(!memcmp(b.key, key, sizeof(key)));
    MT_ASSERT_EQ(b.n_certs, 2);
    MT_ASSERT_EQ(b.cert_len[0], sizeof(leaf));
    MT_ASSERT(!memcmp(b.cert[0], leaf, sizeof(leaf)));
    MT_ASSERT(!memcmp(b.cert[1], mid, sizeof(mid)));

    text[0] = '\0';
    add_block("CERTIFICATE", leaf, sizeof(leaf));
    add_block("CERTIFICATE", mid, sizeof(mid));
    add_block("PRIVATE KEY", key, sizeof(key)); // PKCS#8, as certbot writes it
    MT_ASSERT(parse(&b) == NULL);
    MT_ASSERT_EQ(b.n_certs, 2);
    MT_ASSERT(!memcmp(b.cert[0], leaf, sizeof(leaf)));
    MT_ASSERT(!memcmp(b.key, key, sizeof(key)));
}

// openssl ecparam writes the curve before the key; line ends may be lost
static void test_other_blocks_and_line_ends(void) {
    uint8_t leaf[300], key[121], params[10] = {0x06, 0x08, 0x2a, 0x86, 0x48, 0xce, 0x3d, 0x03, 0x01, 0x07};
    fake_der(leaf, sizeof(leaf), 0x44);
    memset(key, 0x55, sizeof(key));
    tls_bundle_t b;

    text[0] = '\0';
    add_block("EC PARAMETERS", params, sizeof(params));
    add_block("EC PRIVATE KEY", key, sizeof(key));
    add_block("CERTIFICATE", leaf, sizeof(leaf));
    MT_ASSERT(parse(&b) == NULL);
    MT_ASSERT_EQ(b.n_certs, 1);

    // a shell that drops newlines: the armour lines still delimit the blocks
    char *w = text;
    for (const char *r = text; *r; r++)
        if (*r != '\n') *w++ = *r;
    *w = '\0';
    MT_ASSERT(parse(&b) == NULL);
    MT_ASSERT_EQ(b.n_certs, 1);
    MT_ASSERT(!memcmp(b.cert[0], leaf, sizeof(leaf)));
    MT_ASSERT(!memcmp(b.key, key, sizeof(key)));

    // CRLF line ends
    text[0] = '\0';
    add_block("EC PRIVATE KEY", key, sizeof(key));
    add_block("CERTIFICATE", leaf, sizeof(leaf));
    static char crlf[sizeof(text)];
    size_t n = 0;
    for (const char *r = text; *r; r++) {
        if (*r == '\n') crlf[n++] = '\r';
        crlf[n++] = *r;
    }
    crlf[n] = '\0';
    strcpy(text, crlf);
    MT_ASSERT(parse(&b) == NULL);
    MT_ASSERT_EQ(b.n_certs, 1);
}

static void test_refusals(void) {
    uint8_t leaf[300], key[121];
    fake_der(leaf, sizeof(leaf), 0x66);
    memset(key, 0x77, sizeof(key));
    tls_bundle_t b;

    text[0] = '\0';
    add_block("CERTIFICATE", leaf, sizeof(leaf));
    MT_ASSERT(parse(&b) != NULL); // no key

    text[0] = '\0';
    add_block("EC PRIVATE KEY", key, sizeof(key));
    MT_ASSERT(parse(&b) != NULL); // no certificate

    text[0] = '\0';
    add_block("RSA PRIVATE KEY", key, sizeof(key));
    add_block("CERTIFICATE", leaf, sizeof(leaf));
    MT_ASSERT(strstr(parse(&b), "RSA") != NULL);

    text[0] = '\0';
    add_block("ENCRYPTED PRIVATE KEY", key, sizeof(key));
    add_block("CERTIFICATE", leaf, sizeof(leaf));
    MT_ASSERT(strstr(parse(&b), "encrypted") != NULL);

    // the older encrypted form keeps its label and adds headers
    strcpy(text, "-----BEGIN EC PRIVATE KEY-----\nProc-Type: 4,ENCRYPTED\nDEK-Info: AES-128-CBC,00\n\n"
                 "MAAA\n-----END EC PRIVATE KEY-----\n");
    add_block("CERTIFICATE", leaf, sizeof(leaf));
    MT_ASSERT(strstr(parse(&b), "encrypted") != NULL);

    text[0] = '\0';
    add_block("EC PRIVATE KEY", key, sizeof(key));
    add_block("EC PRIVATE KEY", key, sizeof(key));
    add_block("CERTIFICATE", leaf, sizeof(leaf));
    MT_ASSERT(parse(&b) != NULL); // two keys

    text[0] = '\0';
    add_block("EC PRIVATE KEY", key, sizeof(key));
    for (int i = 0; i < TLS_CERTS_MAX + 1; i++) add_block("CERTIFICATE", leaf, sizeof(leaf));
    MT_ASSERT(parse(&b) != NULL); // one certificate too many

    // a certificate block holding something else
    text[0] = '\0';
    add_block("EC PRIVATE KEY", key, sizeof(key));
    add_block("CERTIFICATE", key, sizeof(key));
    MT_ASSERT(parse(&b) != NULL);

    // END naming another label, and no END at all
    strcpy(text, "-----BEGIN CERTIFICATE-----\nMAAA\n-----END PRIVATE KEY-----\n");
    MT_ASSERT(parse(&b) != NULL);
    strcpy(text, "-----BEGIN CERTIFICATE-----\nMAAA\n");
    MT_ASSERT(parse(&b) != NULL);

    // more DER than a record holds
    static uint8_t big[3000];
    fake_der(big, sizeof(big), 0x12);
    text[0] = '\0';
    add_block("EC PRIVATE KEY", key, sizeof(key));
    for (int i = 0; i < 3; i++) add_block("CERTIFICATE", big, sizeof(big));
    MT_ASSERT(parse(&b) != NULL);

    MT_ASSERT(tls_bundle_parse("", 0, der, sizeof(der), &b) != NULL);
}

static void test_record_round_trip(void) {
    uint8_t leaf[900], mid[800], key[121];
    fake_der(leaf, sizeof(leaf), 0x01);
    fake_der(mid, sizeof(mid), 0x02);
    memset(key, 0x03, sizeof(key));
    text[0] = '\0';
    add_block("CERTIFICATE", leaf, sizeof(leaf));
    add_block("EC PRIVATE KEY", key, sizeof(key));
    add_block("CERTIFICATE", mid, sizeof(mid));
    tls_bundle_t b, back;
    MT_ASSERT(parse(&b) == NULL);

    memset(rec, 0xFF, sizeof(rec));
    size_t n = tls_record_encode(&b, 7, rec, sizeof(rec));
    MT_ASSERT(n > 0);
    uint32_t seq = 0;
    MT_ASSERT(tls_record_decode(rec, sizeof(rec), &back, &seq));
    MT_ASSERT_EQ(seq, 7);
    MT_ASSERT_EQ(back.n_certs, 2);
    MT_ASSERT_EQ(back.key_len, sizeof(key));
    MT_ASSERT(!memcmp(back.key, key, sizeof(key)));
    MT_ASSERT(!memcmp(back.cert[0], leaf, sizeof(leaf)));
    MT_ASSERT(!memcmp(back.cert[1], mid, sizeof(mid)));

    // a torn or damaged record is not a record
    rec[n - 1] ^= 0x01;
    MT_ASSERT(!tls_record_decode(rec, sizeof(rec), &back, &seq));
    rec[n - 1] ^= 0x01;
    rec[5] ^= 0x01; // the header is covered too
    MT_ASSERT(!tls_record_decode(rec, sizeof(rec), &back, &seq));
    rec[5] ^= 0x01;
    MT_ASSERT(!tls_record_decode(rec, n - 1, &back, &seq));
    MT_ASSERT(tls_record_decode(rec, n, &back, &seq));

    memset(rec, 0xFF, sizeof(rec)); // erased flash
    MT_ASSERT(!tls_record_decode(rec, sizeof(rec), &back, &seq));
    MT_ASSERT_EQ(tls_record_encode(&b, 1, rec, 100), 0); // does not fit
}

static void test_name_match(void) {
    MT_ASSERT(tls_name_match("pm.example.com", 14, "PM.Example.COM", 14));
    MT_ASSERT(!tls_name_match("pm.example.com", 14, "pm.example.co", 13));
    MT_ASSERT(tls_name_match("*.example.com", 13, "pm.example.com", 14));
    MT_ASSERT(!tls_name_match("*.example.com", 13, "example.com", 11));
    MT_ASSERT(!tls_name_match("*.example.com", 13, "a.b.example.com", 15));
    MT_ASSERT(!tls_name_match("*.example.com", 13, ".example.com", 12));
    MT_ASSERT(!tls_name_match("", 0, "", 0));
}

static void test_civil_days(void) {
    MT_ASSERT_EQ(civil_days(1970, 1, 1), 0);
    MT_ASSERT_EQ(civil_days(2000, 3, 1), 11017);
    MT_ASSERT_EQ(civil_days(2026, 10, 7), 20733);
    for (uint32_t d = 0; d < 40000; d += 37) {
        unsigned y, m, day;
        civil_from_days(d, &y, &m, &day);
        MT_ASSERT_EQ(civil_days(y, m, day), d);
    }
}

void run_tls_bundle_tests(void) {
    mt_run("tls_bundle: a key and its chain, in either order", test_key_and_chain);
    mt_run("tls_bundle: other blocks and lost line ends", test_other_blocks_and_line_ends);
    mt_run("tls_bundle: what is refused", test_refusals);
    mt_run("tls_bundle: the flash record round trip", test_record_round_trip);
    mt_run("tls_bundle: certificate names and wildcards", test_name_match);
    mt_run("tls_bundle: dates to days", test_civil_days);
}
