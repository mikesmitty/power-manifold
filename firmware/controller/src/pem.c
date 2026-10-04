#include "pem.h"

#include <string.h>

static const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static int b64_value(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

size_t pem_to_der(const char *text, uint8_t *out, size_t cap) {
    size_t n = 0;
    uint32_t acc = 0;
    int bits = 0;
    bool any = false;
    const char *p = text;
    while (*p) {
        if (*p == '-') { // an armour line: BEGIN is skipped, END ends the certificate
            bool end = strncmp(p, "-----END", 8) == 0;
            while (*p && *p != '\n') p++;
            if (end) break;
            continue;
        }
        char c = *p++;
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        if (c == '=') break; // padding: the base64 is complete
        int v = b64_value(c);
        if (v < 0) return 0;
        acc = (acc << 6) | (uint32_t)v;
        bits += 6;
        any = true;
        if (bits >= 8) {
            bits -= 8;
            if (n >= cap) return 0;
            out[n++] = (uint8_t)(acc >> bits);
            acc &= (1u << bits) - 1u;
        }
    }
    return any ? n : 0;
}

size_t der_to_pem(const uint8_t *der, size_t len, char *out, size_t cap, const char *eol) {
    static const char head[] = "-----BEGIN CERTIFICATE-----", tail[] = "-----END CERTIFICATE-----";
    size_t eol_len = strlen(eol);
    size_t b64 = (len + 2) / 3 * 4;
    size_t lines = (b64 + 63) / 64;
    size_t need = sizeof(head) - 1 + eol_len + b64 + lines * eol_len + sizeof(tail) - 1 + eol_len;
    if (need + 1 > cap) return 0;
    size_t o = 0;
    memcpy(out + o, head, sizeof(head) - 1);
    o += sizeof(head) - 1;
    memcpy(out + o, eol, eol_len);
    o += eol_len;
    size_t col = 0;
    for (size_t i = 0; i < len; i += 3) {
        uint32_t v = (uint32_t)der[i] << 16;
        if (i + 1 < len) v |= (uint32_t)der[i + 1] << 8;
        if (i + 2 < len) v |= der[i + 2];
        out[o++] = B64[(v >> 18) & 63];
        out[o++] = B64[(v >> 12) & 63];
        out[o++] = i + 1 < len ? B64[(v >> 6) & 63] : '=';
        out[o++] = i + 2 < len ? B64[v & 63] : '=';
        col += 4;
        if (col == 64 || i + 3 >= len) {
            memcpy(out + o, eol, eol_len);
            o += eol_len;
            col = 0;
        }
    }
    memcpy(out + o, tail, sizeof(tail) - 1);
    o += sizeof(tail) - 1;
    memcpy(out + o, eol, eol_len);
    o += eol_len;
    out[o] = '\0';
    return o;
}

bool der_cert_shape_ok(const uint8_t *der, size_t len) {
    if (len < 4 || der[0] != 0x30) return false;
    size_t hdr, body;
    if (der[1] < 0x80) {
        hdr = 2;
        body = der[1];
    } else if (der[1] == 0x81) {
        hdr = 3;
        body = der[2];
    } else if (der[1] == 0x82) {
        hdr = 4;
        body = ((size_t)der[2] << 8) | der[3];
    } else {
        return false; // no certificate needs a length field longer than two bytes
    }
    return hdr + body == len;
}
