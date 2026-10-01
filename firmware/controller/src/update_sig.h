#pragma once

#include <stdbool.h>
#include <stdint.h>

// Signed OTA images: the raw controller.bin with this trailer appended
// (tools/sign_image.py). Free of pico-sdk dependencies, like update_image.h,
// so the host tests and tools/verify_image.c run the code the firmware runs.
//
//     0  magic "PWRMSIG1"
//     8  image length, u32 little-endian: the bytes before the trailer
//    12  reserved, zero
//    16  board the image was built for (PICO_BOARD), NUL-padded
//    48  SHA-512 of the image bytes
//   112  Ed25519 signature (RFC 8032, no prehash) over bytes 0..111
//
// The image's version is not repeated here: it sits in the IMAGE_DEF inside
// the hashed bytes (update_image.h), so it is as trusted as the signature.

#define UPD_SIG_MAGIC       "PWRMSIG1"
#define UPD_SIG_MAGIC_LEN   8u
#define UPD_SIG_BOARD_OFF   16u
#define UPD_SIG_BOARD_LEN   32u
#define UPD_SIG_HASH_OFF    48u
#define UPD_SIG_HASH_LEN    64u
#define UPD_SIG_SIGNED_LEN  112u // what the signature covers
#define UPD_SIG_TRAILER_LEN 176u
#define UPD_SIG_KEY_LEN     32u  // a raw Ed25519 public key

// Whether the last UPD_SIG_TRAILER_LEN bytes of a stream are a trailer. An
// image that says it is signed is never treated as an unsigned one.
bool update_sig_present(const uint8_t *trailer);

// Checks a trailer against the image it came with: its length, the board,
// the hash, and the signature under any of nkeys public keys (keys = nkeys *
// UPD_SIG_KEY_LEN bytes). With nkeys == 0 the signature is not checked: a
// build without keys does not enforce signing. Returns NULL when the image
// is good, else a static error string.
const char *update_sig_check(const uint8_t *trailer, uint32_t image_len,
                             const uint8_t hash[UPD_SIG_HASH_LEN], const char *board,
                             const uint8_t *keys, unsigned nkeys);
