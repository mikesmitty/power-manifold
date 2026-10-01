#include <string.h>

#include "microtest.h"
#include "monocypher-ed25519.h"
#include "update_image.h"
#include "update_sig.h"

// Signed-image trailers, built here the way tools/sign_image.py builds them
// (test/sign_roundtrip.sh checks the tool itself against the same verifier).

#define BOARD "pico2_w"

static uint8_t image[5000];
static uint8_t hash[UPD_SIG_HASH_LEN];
static uint8_t keys[2 * UPD_SIG_KEY_LEN]; // [0] primary, [1] backup
static uint8_t secret[2][64];

static void setup(void) {
    for (unsigned i = 0; i < sizeof(image); i++) image[i] = (uint8_t)(i * 7 + 3);
    crypto_sha512(hash, image, sizeof(image));
    for (unsigned k = 0; k < 2; k++) {
        uint8_t seed[32];
        memset(seed, 0x40 + k, sizeof(seed));
        crypto_ed25519_key_pair(secret[k], keys + k * UPD_SIG_KEY_LEN, seed);
    }
}

static void make_trailer(uint8_t *t, uint32_t len, const char *board, unsigned key) {
    memset(t, 0, UPD_SIG_TRAILER_LEN);
    memcpy(t, UPD_SIG_MAGIC, UPD_SIG_MAGIC_LEN);
    t[8] = (uint8_t)len;
    t[9] = (uint8_t)(len >> 8);
    t[10] = (uint8_t)(len >> 16);
    t[11] = (uint8_t)(len >> 24);
    strncpy((char *)t + UPD_SIG_BOARD_OFF, board, UPD_SIG_BOARD_LEN - 1);
    memcpy(t + UPD_SIG_HASH_OFF, hash, UPD_SIG_HASH_LEN);
    crypto_ed25519_sign(t + UPD_SIG_SIGNED_LEN, secret[key], t, UPD_SIG_SIGNED_LEN);
}

static const char *check(const uint8_t *t) {
    return update_sig_check(t, sizeof(image), hash, BOARD, keys, 2);
}

// RFC 8032 section 7.1, TEST 2: what Cloud KMS and a hardware token produce
// is this Ed25519, not Monocypher's own BLAKE2b flavour of EdDSA.
static void test_rfc8032_vector(void) {
    static const uint8_t pub[32] = {
        0x3d, 0x40, 0x17, 0xc3, 0xe8, 0x43, 0x89, 0x5a, 0x92, 0xb7, 0x0a, 0xa7, 0x4d, 0x1b, 0x7e, 0xbc,
        0x9c, 0x98, 0x2c, 0xcf, 0x2e, 0xc4, 0x96, 0x8c, 0xc0, 0xcd, 0x55, 0xf1, 0x2a, 0xf4, 0x66, 0x0c};
    static const uint8_t sig[64] = {
        0x92, 0xa0, 0x09, 0xa9, 0xf0, 0xd4, 0xca, 0xb8, 0x72, 0x0e, 0x82, 0x0b, 0x5f, 0x64, 0x25, 0x40,
        0xa2, 0xb2, 0x7b, 0x54, 0x16, 0x50, 0x3f, 0x8f, 0xb3, 0x76, 0x22, 0x23, 0xeb, 0xdb, 0x69, 0xda,
        0x08, 0x5a, 0xc1, 0xe4, 0x3e, 0x15, 0x99, 0x6e, 0x45, 0x8f, 0x36, 0x13, 0xd0, 0xf1, 0x1d, 0x8c,
        0x38, 0x7b, 0x2e, 0xae, 0xb4, 0x30, 0x2a, 0xee, 0xb0, 0x0d, 0x29, 0x16, 0x12, 0xbb, 0x0c, 0x00};
    static const uint8_t msg[1] = {0x72};
    MT_ASSERT(crypto_ed25519_check(sig, pub, msg, sizeof(msg)) == 0);
    static const uint8_t other[1] = {0x73};
    MT_ASSERT(crypto_ed25519_check(sig, pub, other, sizeof(other)) != 0);
}

static void test_good_trailer_either_key(void) {
    uint8_t t[UPD_SIG_TRAILER_LEN];
    make_trailer(t, sizeof(image), BOARD, 0);
    MT_ASSERT(update_sig_present(t));
    MT_ASSERT(check(t) == NULL);
    make_trailer(t, sizeof(image), BOARD, 1); // the backup key signs as well as the primary
    MT_ASSERT(check(t) == NULL);
}

static void test_untrusted_key(void) {
    uint8_t t[UPD_SIG_TRAILER_LEN];
    make_trailer(t, sizeof(image), BOARD, 1);
    MT_ASSERT(update_sig_check(t, sizeof(image), hash, BOARD, keys, 1) != NULL); // only the primary trusted
    MT_ASSERT(update_sig_check(t, sizeof(image), hash, BOARD, keys + UPD_SIG_KEY_LEN, 1) == NULL);
}

static void test_every_signed_byte_counts(void) {
    uint8_t t[UPD_SIG_TRAILER_LEN];
    for (unsigned i = UPD_SIG_MAGIC_LEN; i < UPD_SIG_TRAILER_LEN; i++) {
        make_trailer(t, sizeof(image), BOARD, 0);
        t[i] ^= 0x01;
        MT_ASSERT(check(t) != NULL);
    }
}

static void test_wrong_image(void) {
    uint8_t t[UPD_SIG_TRAILER_LEN];
    make_trailer(t, sizeof(image), BOARD, 0);
    uint8_t other[UPD_SIG_HASH_LEN];
    memcpy(other, hash, sizeof(other));
    other[10] ^= 0x80; // the image that arrived is not the one that was signed
    MT_ASSERT(update_sig_check(t, sizeof(image), other, BOARD, keys, 2) != NULL);
    MT_ASSERT(update_sig_check(t, sizeof(image) - 1, hash, BOARD, keys, 2) != NULL);
}

static void test_wrong_board(void) {
    uint8_t t[UPD_SIG_TRAILER_LEN];
    make_trailer(t, sizeof(image), "pwrman_controller_card", 0); // validly signed, for the card
    MT_ASSERT(check(t) != NULL);
    MT_ASSERT(update_sig_check(t, sizeof(image), hash, "pwrman_controller_card", keys, 2) == NULL);
    make_trailer(t, sizeof(image), "pico2", 0); // a prefix of this board's name is another board
    MT_ASSERT(check(t) != NULL);
    make_trailer(t, sizeof(image), BOARD, 0);
    MT_ASSERT(update_sig_check(t, sizeof(image), hash, "pico2", keys, 2) != NULL);
}

static void test_unsigned_tail(void) {
    uint8_t t[UPD_SIG_TRAILER_LEN];
    memcpy(t, image + sizeof(image) - sizeof(t), sizeof(t)); // an unsigned image just ends
    MT_ASSERT(!update_sig_present(t));
    MT_ASSERT(check(t) != NULL);
}

// A build without keys does not enforce signing, but a trailer that is there
// still has to describe the image it came with.
static void test_no_keys(void) {
    uint8_t t[UPD_SIG_TRAILER_LEN];
    make_trailer(t, sizeof(image), BOARD, 0);
    memset(t + UPD_SIG_SIGNED_LEN, 0, 64);
    MT_ASSERT(update_sig_check(t, sizeof(image), hash, BOARD, keys, 0) == NULL);
    MT_ASSERT(update_sig_check(t, sizeof(image), hash, "pico2", keys, 0) != NULL);
    MT_ASSERT(update_sig_check(t, sizeof(image) + 4, hash, BOARD, keys, 0) != NULL);
    MT_ASSERT(update_sig_check(t, sizeof(image), hash, BOARD, keys, 2) != NULL);
}

// The downgrade rule compares the running FW_VERSION with the incoming
// IMAGE_DEF's (major << 16) | (y*256+z).
static void test_version_word(void) {
    MT_ASSERT_EQ(update_version_word("0.10.0"), 0x00000a00);
    MT_ASSERT_EQ(update_version_word("1.2.3"), 0x00010203);
    MT_ASSERT(update_version_word("0.9.5") < update_version_word("0.10.0"));
    MT_ASSERT(update_version_word("0.10.1") > update_version_word("0.10.0"));
    MT_ASSERT(update_version_word("0.255.255") < update_version_word("1.0.0"));
    MT_ASSERT_EQ(update_version_word("garbage"), 0);
    MT_ASSERT_EQ(update_version_word("1.2"), 0);
    MT_ASSERT_EQ(update_version_word("1.256.0"), 0); // would not fit the IMAGE_DEF either
}

void run_update_sig_tests(void) {
    setup();
    mt_run("sig: RFC 8032 test vector", test_rfc8032_vector);
    mt_run("sig: a good trailer passes under either key", test_good_trailer_either_key);
    mt_run("sig: a key that is not built in is refused", test_untrusted_key);
    mt_run("sig: any changed byte fails", test_every_signed_byte_counts);
    mt_run("sig: another image under this trailer fails", test_wrong_image);
    mt_run("sig: an image for another board is refused", test_wrong_board);
    mt_run("sig: an unsigned tail is not a trailer", test_unsigned_tail);
    mt_run("sig: no keys built in", test_no_keys);
    mt_run("sig: version words order like versions", test_version_word);
}
