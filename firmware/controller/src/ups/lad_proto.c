#include "lad_proto.h"

#include <string.h>

uint8_t lad_crc8(const uint8_t *p, size_t n) {
    uint8_t crc = 0;
    for (size_t i = 0; i < n; i++) {
        crc ^= p[i];
        for (int b = 0; b < 8; b++)
            crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x07) : (uint8_t)(crc << 1);
    }
    return crc;
}

static size_t build(uint8_t *out, uint8_t rw, uint16_t addr, const uint8_t *data, size_t n) {
    if (n > LAD_DATA_MAX) n = LAD_DATA_MAX;
    out[0] = rw;
    out[1] = (uint8_t)(2 + n + 1);
    out[2] = (uint8_t)(addr >> 8);
    out[3] = (uint8_t)addr;
    if (n) memcpy(out + 4, data, n);
    out[4 + n] = lad_crc8(out, 4 + n);
    return 5 + n;
}

size_t lad_build_read(uint8_t *out, uint16_t addr) {
    return build(out, LAD_RW_READ, addr, NULL, 0);
}

size_t lad_build_write(uint8_t *out, uint16_t addr, const uint8_t *data, size_t n) {
    return build(out, LAD_RW_WRITE, addr, data, n);
}

void lad_parser_reset(lad_parser_t *p) {
    p->n = 0;
}

static bool plausible_start(uint8_t b) {
    return b == LAD_RW_READ || b == LAD_RW_WRITE;
}

// Drop the first byte and re-run what is left through the state machine, so
// a stray 0x55 in line noise cannot swallow the real frame behind it.
static bool rescan(lad_parser_t *p, lad_frame_t *f) {
    uint8_t held[LAD_FRAME_MAX];
    uint8_t n = p->n;
    memcpy(held, p->buf, n);
    p->n = 0;
    for (uint8_t i = 1; i < n; i++)
        if (lad_parser_feed(p, held[i], f)) return true;
    return false;
}

bool lad_parser_feed(lad_parser_t *p, uint8_t b, lad_frame_t *f) {
    if (p->n == 0) {
        if (!plausible_start(b)) return false;
        p->buf[p->n++] = b;
        return false;
    }
    if (p->n == 1) {
        // LEN: at least the address and CRC, at most the largest reply
        if (b < 3 || b > LAD_FRAME_MAX - 2) {
            p->n = 0;
            p->bad++;
            // the byte itself might be the start of the real frame
            return lad_parser_feed(p, b, f);
        }
        p->buf[p->n++] = b;
        return false;
    }
    p->buf[p->n++] = b;
    uint8_t total = (uint8_t)(2 + p->buf[1]);
    if (p->n < total) return false;

    if (lad_crc8(p->buf, (size_t)(total - 1)) != p->buf[total - 1]) {
        p->bad++;
        return rescan(p, f);
    }
    f->rw = p->buf[0];
    f->addr = lad_u16(p->buf + 2);
    f->len = (uint8_t)(p->buf[1] - 3);
    memcpy(f->data, p->buf + 4, f->len);
    p->n = 0;
    return true;
}
