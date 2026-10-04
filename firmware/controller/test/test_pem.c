#include <string.h>

#include "microtest.h"
#include "letsencrypt_roots.h"
#include "pem.h"

static char pem[4096];
static uint8_t der[2100];

// RFC 4648 vectors, through the certificate armour
static void test_base64_vectors(void) {
    static const struct { const char *in, *b64; } V[] = {
        {"f", "Zg=="}, {"fo", "Zm8="}, {"foo", "Zm9v"}, {"foob", "Zm9vYg=="},
        {"fooba", "Zm9vYmE="}, {"foobar", "Zm9vYmFy"},
    };
    for (size_t i = 0; i < sizeof(V) / sizeof(V[0]); i++) {
        char want[96];
        snprintf(want, sizeof(want), "-----BEGIN CERTIFICATE-----\n%s\n-----END CERTIFICATE-----\n", V[i].b64);
        size_t n = der_to_pem((const uint8_t *)V[i].in, strlen(V[i].in), pem, sizeof(pem), "\n");
        MT_ASSERT_EQ(n, strlen(want));
        MT_ASSERT(!strcmp(pem, want));
        size_t m = pem_to_der(pem, der, sizeof(der));
        MT_ASSERT_EQ(m, strlen(V[i].in));
        MT_ASSERT(!memcmp(der, V[i].in, m));
    }
}

static void test_round_trip_wraps_at_64(void) {
    uint8_t blob[2048];
    for (size_t i = 0; i < sizeof(blob); i++) blob[i] = (uint8_t)(i * 7 + (i >> 3));
    size_t n = der_to_pem(blob, sizeof(blob), pem, sizeof(pem), "\n");
    MT_ASSERT(n > 0);
    MT_ASSERT_EQ(n, (size_t)PEM_CERT_TEXT_MAX(2048) - 1);
    // every base64 line is 64 columns except the last
    const char *p = strchr(pem, '\n') + 1;
    int lines = 0;
    while (*p != '-') {
        const char *e = strchr(p, '\n');
        MT_ASSERT(e != NULL);
        lines++;
        if (*(e + 1) != '-') MT_ASSERT_EQ((size_t)(e - p), 64);
        p = e + 1;
    }
    MT_ASSERT_EQ(lines, (2048 + 2) / 3 * 4 / 64 + 1);
    MT_ASSERT_EQ(pem_to_der(pem, der, sizeof(der)), sizeof(blob));
    MT_ASSERT(!memcmp(der, blob, sizeof(blob)));
    // too small a buffer, by one byte, gives nothing rather than a torn text
    MT_ASSERT_EQ(der_to_pem(blob, sizeof(blob), pem, n, "\n"), 0);
    MT_ASSERT_EQ(pem_to_der(pem, der, sizeof(blob) - 1), 0);
}

// Line ends written as the two characters backslash and n go straight into a
// JSON string and decode once the JSON parser has turned them back
static void test_json_line_ends(void) {
    const uint8_t blob[70] = {1, 2, 3};
    size_t n = der_to_pem(blob, sizeof(blob), pem, sizeof(pem), "\\n");
    MT_ASSERT(n > 0);
    MT_ASSERT(strchr(pem, '\n') == NULL);
    MT_ASSERT(strstr(pem, "-----BEGIN CERTIFICATE-----\\n") == pem);
    MT_ASSERT(strstr(pem, "\\n-----END CERTIFICATE-----\\n") != NULL);
}

static void test_decoder_tolerance(void) {
    const uint8_t want[6] = {'f', 'o', 'o', 'b', 'a', 'r'};
    // bare base64, no armour
    MT_ASSERT_EQ(pem_to_der("Zm9vYmFy", der, sizeof(der)), 6);
    MT_ASSERT(!memcmp(der, want, 6));
    // Windows line ends, indentation and a trailing blank line
    MT_ASSERT_EQ(pem_to_der("-----BEGIN CERTIFICATE-----\r\n  Zm9v\r\n\tYmFy\r\n-----END CERTIFICATE-----\r\n\r\n",
                            der, sizeof(der)), 6);
    MT_ASSERT(!memcmp(der, want, 6));
    // a pasted chain: the first certificate is the one taken
    MT_ASSERT_EQ(pem_to_der("-----BEGIN CERTIFICATE-----\nZm9v\n-----END CERTIFICATE-----\n"
                            "-----BEGIN CERTIFICATE-----\nYmFy\n-----END CERTIFICATE-----\n", der, sizeof(der)), 3);
    MT_ASSERT(!memcmp(der, "foo", 3));
    // nothing, or something that is not base64, is refused
    MT_ASSERT_EQ(pem_to_der("", der, sizeof(der)), 0);
    MT_ASSERT_EQ(pem_to_der("   \n", der, sizeof(der)), 0);
    MT_ASSERT_EQ(pem_to_der("Zm9v!", der, sizeof(der)), 0);
    MT_ASSERT_EQ(pem_to_der("Certificate:\n  Data:\n", der, sizeof(der)), 0);
}

static void test_der_shape(void) {
    const uint8_t short_form[7] = {0x30, 0x05, 1, 2, 3, 4, 5};
    MT_ASSERT(der_cert_shape_ok(short_form, sizeof(short_form)));
    MT_ASSERT(!der_cert_shape_ok(short_form, sizeof(short_form) - 1)); // stated length overruns
    MT_ASSERT(!der_cert_shape_ok(short_form + 1, sizeof(short_form) - 1)); // not a SEQUENCE
    uint8_t one_byte[3 + 200] = {0x30, 0x81, 200};
    MT_ASSERT(der_cert_shape_ok(one_byte, sizeof(one_byte)));
    uint8_t two_byte[4 + 2044] = {0x30, 0x82, 0x07, 0xFC};
    MT_ASSERT(der_cert_shape_ok(two_byte, sizeof(two_byte)));
    MT_ASSERT(!der_cert_shape_ok(two_byte, sizeof(two_byte) - 1));
    const uint8_t long_form[5] = {0x30, 0x83, 0x00, 0x00, 0x01};
    MT_ASSERT(!der_cert_shape_ok(long_form, sizeof(long_form)));
    MT_ASSERT(!der_cert_shape_ok(short_form, 3)); // shorter than any certificate
}

// The roots built into the image are each one DER certificate, as the
// generator promised, and together stay small. The count is pinned so a
// change to roots/ is a deliberate one.
static void test_builtin_roots(void) {
    MT_ASSERT_EQ(LETSENCRYPT_ROOT_COUNT, 4);
    size_t total = 0;
    for (size_t i = 0; i < LETSENCRYPT_ROOT_COUNT; i++) {
        MT_ASSERT(letsencrypt_roots[i].name && letsencrypt_roots[i].name[0]);
        MT_ASSERT(der_cert_shape_ok(letsencrypt_roots[i].der, letsencrypt_roots[i].len));
        total += letsencrypt_roots[i].len;
    }
    MT_ASSERT(total < 4096);
}

void run_pem_tests(void) {
    mt_run("pem: RFC 4648 vectors through the armour", test_base64_vectors);
    mt_run("pem: a 2 KB certificate round-trips with 64-column lines", test_round_trip_wraps_at_64);
    mt_run("pem: JSON line ends", test_json_line_ends);
    mt_run("pem: the decoder takes bare base64, CRLF and chains", test_decoder_tolerance);
    mt_run("pem: DER certificate shape", test_der_shape);
    mt_run("pem: the built-in Let's Encrypt roots are DER certificates", test_builtin_roots);
}
