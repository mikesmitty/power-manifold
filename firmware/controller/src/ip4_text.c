#include "ip4_text.h"

#include <stdio.h>
#include <string.h>

bool ip4_parse(const char *s, uint32_t *addr_nbo) {
    uint8_t b[4];
    for (int i = 0; i < 4; i++) {
        if (*s < '0' || *s > '9') return false;
        unsigned v = 0;
        int digits = 0;
        while (*s >= '0' && *s <= '9') {
            v = v * 10 + (unsigned)(*s++ - '0');
            if (++digits > 3 || v > 255) return false;
        }
        b[i] = (uint8_t)v;
        if (i < 3) {
            if (*s != '.') return false;
            s++;
        }
    }
    if (*s) return false;
    memcpy(addr_nbo, b, 4);
    return true;
}

size_t ip4_format(char *out, size_t cap, uint32_t addr_nbo) {
    if (!cap) return 0;
    if (!addr_nbo) {
        out[0] = '\0';
        return 0;
    }
    uint8_t b[4];
    memcpy(b, &addr_nbo, 4);
    int n = snprintf(out, cap, "%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
    return n < 0 ? 0 : (size_t)n < cap ? (size_t)n : cap - 1;
}

bool ip4_mask_valid(uint32_t mask_nbo) {
    uint8_t b[4];
    memcpy(b, &mask_nbo, 4);
    uint32_t m = (uint32_t)b[0] << 24 | (uint32_t)b[1] << 16 | (uint32_t)b[2] << 8 | b[3];
    if (!m) return false;
    uint32_t zeros = ~m;               // the host part, as ones
    return (zeros & (zeros + 1)) == 0; // ...must be a trailing run
}
