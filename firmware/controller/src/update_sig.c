#include "update_sig.h"

#include <string.h>

#include "monocypher-ed25519.h"

bool update_sig_present(const uint8_t *trailer) {
    return memcmp(trailer, UPD_SIG_MAGIC, UPD_SIG_MAGIC_LEN) == 0;
}

const char *update_sig_check(const uint8_t *trailer, uint32_t image_len,
                             const uint8_t hash[UPD_SIG_HASH_LEN], const char *board,
                             const uint8_t *keys, unsigned nkeys) {
    if (!update_sig_present(trailer)) return "image is not signed";

    uint32_t len = (uint32_t)trailer[8] | ((uint32_t)trailer[9] << 8) |
                   ((uint32_t)trailer[10] << 16) | ((uint32_t)trailer[11] << 24);
    if (len != image_len) return "signed length does not match the image";

    // the whole field, padding included: "pico2" must not pass for "pico2_w"
    char want[UPD_SIG_BOARD_LEN] = {0};
    strncpy(want, board, sizeof(want) - 1);
    if (memcmp(trailer + UPD_SIG_BOARD_OFF, want, sizeof(want)) != 0)
        return "image was signed for another board";

    if (memcmp(trailer + UPD_SIG_HASH_OFF, hash, UPD_SIG_HASH_LEN) != 0)
        return "image does not match its signed hash";

    if (nkeys == 0) return NULL;
    for (unsigned i = 0; i < nkeys; i++)
        if (crypto_ed25519_check(trailer + UPD_SIG_SIGNED_LEN, keys + i * UPD_SIG_KEY_LEN,
                                 trailer, UPD_SIG_SIGNED_LEN) == 0)
            return NULL;
    return "signature is not from a trusted key";
}
