// Host-side check of a signed controller image, with the code the firmware
// runs (src/update_sig.c) and, by default, the keys it is built with
// (keys/*.pem). Built by the host test project; the release workflow runs it
// on what it is about to publish.
//
//   verify_image controller.signed.bin <board> [public-key-hex ...]

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "monocypher-ed25519.h"
#include "update_image.h"
#include "update_keys.h"
#include "update_sig.h"

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: %s <controller.signed.bin> <board> [public-key-hex ...]\n", argv[0]);
        return 2;
    }

    const uint8_t *keys = update_keys;
    unsigned nkeys = UPDATE_KEY_COUNT;
    static uint8_t given[16 * UPD_SIG_KEY_LEN];
    if (argc > 3) {
        nkeys = (unsigned)argc - 3;
        if (nkeys > 16) nkeys = 16;
        for (unsigned k = 0; k < nkeys; k++) {
            const char *hex = argv[3 + k];
            if (strlen(hex) != 2 * UPD_SIG_KEY_LEN) {
                fprintf(stderr, "key %u: want %u hex digits\n", k + 1, 2 * UPD_SIG_KEY_LEN);
                return 2;
            }
            for (unsigned i = 0; i < UPD_SIG_KEY_LEN; i++) {
                unsigned b;
                if (sscanf(hex + 2 * i, "%2x", &b) != 1) {
                    fprintf(stderr, "key %u: not hex\n", k + 1);
                    return 2;
                }
                given[k * UPD_SIG_KEY_LEN + i] = (uint8_t)b;
            }
        }
        keys = given;
    }
    if (nkeys == 0) {
        fprintf(stderr, "no keys to check against (keys/*.pem is empty and none were given)\n");
        return 2;
    }

    FILE *f = fopen(argv[1], "rb");
    if (!f) {
        perror(argv[1]);
        return 2;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    rewind(f);
    if (size <= (long)UPD_SIG_TRAILER_LEN) {
        fprintf(stderr, "%s: too small to be a signed image\n", argv[1]);
        return 1;
    }
    uint8_t *buf = malloc((size_t)size);
    if (!buf || fread(buf, 1, (size_t)size, f) != (size_t)size) {
        fprintf(stderr, "%s: read failed\n", argv[1]);
        return 2;
    }
    fclose(f);

    uint32_t image_len = (uint32_t)size - UPD_SIG_TRAILER_LEN;
    uint8_t hash[UPD_SIG_HASH_LEN];
    crypto_sha512(hash, buf, image_len);
    const char *bad = update_sig_check(buf + image_len, image_len, hash, argv[2], keys, nkeys);
    if (bad) {
        fprintf(stderr, "%s: %s\n", argv[1], bad);
        return 1;
    }

    image_def_t def;
    const char *e = update_image_scan(buf, image_len < 4096 ? image_len : 4096, &def);
    if (e) {
        fprintf(stderr, "%s: signature good, but %s\n", argv[1], e);
        return 1;
    }
    printf("%s: good signature, board %s, %lu bytes, version %u.%u.%u\n", argv[1], argv[2],
           (unsigned long)image_len, def.ver_major, def.ver_minor >> 8, def.ver_minor & 0xff);
    free(buf);
    return 0;
}
