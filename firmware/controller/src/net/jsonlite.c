#include "jsonlite.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

// Position of the first byte of the value for "key", or NULL. A match must be
// a quoted key followed (after optional whitespace) by a colon, so a string
// value that merely contains the key text is skipped over.
static const char *find_value(const char *json, const char *key) {
    size_t klen = strlen(key);
    for (const char *p = json; (p = strchr(p, '"')) != NULL; p++) {
        if (strncmp(p + 1, key, klen) != 0 || p[1 + klen] != '"') continue;
        const char *q = p + 2 + klen;
        while (*q == ' ' || *q == '\t' || *q == '\r' || *q == '\n') q++;
        if (*q != ':') continue;
        q++;
        while (*q == ' ' || *q == '\t' || *q == '\r' || *q == '\n') q++;
        return q;
    }
    return NULL;
}

static int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// Append up to 3 bytes of UTF-8 for a BMP code point; false if out is full.
static bool put_utf8(char *out, size_t cap, size_t *n, unsigned cp) {
    char b[3];
    size_t len;
    if (cp < 0x80) {
        b[0] = (char)cp;
        len = 1;
    } else if (cp < 0x800) {
        b[0] = (char)(0xC0 | (cp >> 6));
        b[1] = (char)(0x80 | (cp & 0x3F));
        len = 2;
    } else {
        b[0] = (char)(0xE0 | (cp >> 12));
        b[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        b[2] = (char)(0x80 | (cp & 0x3F));
        len = 3;
    }
    if (*n + len > cap - 1) return false;
    memcpy(out + *n, b, len);
    *n += len;
    return true;
}

// Decode the quoted string at p (which must point at its opening quote) into
// out; *end is left on the closing quote. Same result codes as json_get_str.
static int parse_string(const char *p, char *out, size_t cap, const char **end) {
    if (*p != '"' || cap == 0) return 0;
    p++;

    size_t n = 0;
    bool full = false;
    for (; *p && *p != '"'; p++) {
        unsigned cp;
        bool escaped = *p == '\\';
        if (escaped) {
            p++;
            switch (*p) {
            case '"': cp = '"'; break;
            case '\\': cp = '\\'; break;
            case '/': cp = '/'; break;
            case 'b': cp = '\b'; break;
            case 'f': cp = '\f'; break;
            case 'n': cp = '\n'; break;
            case 'r': cp = '\r'; break;
            case 't': cp = '\t'; break;
            case 'u': {
                int h[4];
                for (int i = 0; i < 4; i++) {
                    if ((h[i] = hexval(p[1 + i])) < 0) return 0; // malformed
                }
                cp = (unsigned)((h[0] << 12) | (h[1] << 8) | (h[2] << 4) | h[3]);
                if (cp >= 0xD800 && cp <= 0xDFFF) cp = '?'; // surrogates: not supported
                p += 4;
                break;
            }
            default:
                return 0; // malformed escape (or truncated at NUL)
            }
        } else {
            cp = (unsigned char)*p;
        }
        if (full) continue; // keep scanning for the closing quote
        if (!escaped) {
            // raw bytes (UTF-8 sequences included) copy through untouched
            if (n + 1 > cap - 1) full = true;
            else out[n++] = (char)cp;
        } else if (!put_utf8(out, cap, &n, cp)) {
            full = true;
        }
    }
    out[n] = '\0';
    if (*p != '"') return 0; // unterminated
    *end = p;
    return full ? -1 : 1;
}

int json_get_str(const char *json, const char *key, char *out, size_t cap) {
    const char *p = find_value(json, key), *end;
    if (!p) return 0;
    return parse_string(p, out, cap, &end);
}

static const char *skip_ws(const char *p) {
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    return p;
}

int json_get_str_at(const char *json, const char *key, unsigned idx, char *out, size_t cap) {
    const char *p = find_value(json, key), *end;
    if (!p || *p != '[') return 0;
    p = skip_ws(p + 1);
    for (unsigned i = 0; ; i++) {
        if (*p == ']') return 0; // fewer elements than idx + 1
        if (i == idx) return parse_string(p, out, cap, &end);
        char skip[1]; // earlier elements only need scanning to their close
        if (!parse_string(p, skip, sizeof(skip), &end)) return 0;
        p = skip_ws(end + 1);
        if (*p != ',') return 0;
        p = skip_ws(p + 1);
    }
}

bool json_get_int(const char *json, const char *key, long *out) {
    const char *p = find_value(json, key);
    if (!p) return false;
    if (*p != '-' && !isdigit((unsigned char)*p)) return false;
    char *end;
    long v = strtol(p, &end, 10);
    if (end == p) return false;
    *out = v;
    return true;
}

size_t json_escape(char *out, size_t cap, const char *in) {
    static const char hex[] = "0123456789abcdef";
    size_t n = 0;
    if (cap == 0) return 0;
    for (; *in; in++) {
        unsigned char c = (unsigned char)*in;
        char esc[6];
        size_t len;
        if (c == '"' || c == '\\') {
            esc[0] = '\\';
            esc[1] = (char)c;
            len = 2;
        } else if (c < 0x20) {
            esc[0] = '\\';
            esc[1] = 'u';
            esc[2] = '0';
            esc[3] = '0';
            esc[4] = hex[c >> 4];
            esc[5] = hex[c & 0xF];
            len = 6;
        } else {
            esc[0] = (char)c;
            len = 1;
        }
        if (n + len > cap - 1) break;
        memcpy(out + n, esc, len);
        n += len;
    }
    out[n] = '\0';
    return n;
}
