#include "http_req.h"

#include <ctype.h>
#include <string.h>
#include <strings.h>

bool http_req_header(const char *req, const char *name, char *out, size_t cap) {
    size_t nlen = strlen(name);
    const char *end = strstr(req, "\r\n\r\n"); // headers stop here; the body may say anything
    if (!end) end = req + strlen(req);
    const char *p = strchr(req, '\n'); // skip the request line
    while (p && p < end) {
        p++;
        if (!strncasecmp(p, name, nlen) && p[nlen] == ':') {
            const char *v = p + nlen + 1;
            while (*v == ' ' || *v == '\t') v++;
            const char *e = v;
            while (e < end && *e != '\r' && *e != '\n') e++;
            while (e > v && (e[-1] == ' ' || e[-1] == '\t')) e--;
            size_t n = (size_t)(e - v);
            if (n >= cap) return false;
            memcpy(out, v, n);
            out[n] = '\0';
            return true;
        }
        p = strchr(p, '\n');
    }
    return false;
}

bool http_req_is_json(const char *req) {
    char v[64];
    if (!http_req_header(req, "Content-Type", v, sizeof(v))) return false;
    if (strncasecmp(v, "application/json", 16)) return false;
    return v[16] == '\0' || v[16] == ';' || v[16] == ' ';
}

bool http_req_bearer_ok(const char *req, const char *secret) {
    char v[96];
    if (!http_req_header(req, "Authorization", v, sizeof(v))) return false;
    if (strncasecmp(v, "Bearer ", 7)) return false;
    const char *given = v + 7;
    size_t n = strlen(secret), g = strlen(given);
    // every byte of the secret is visited, and differences are only collected
    unsigned char diff = (unsigned char)(g != n);
    for (size_t i = 0; i < n; i++)
        diff |= (unsigned char)((i < g ? given[i] : 0) ^ secret[i]);
    return n && !diff;
}

// name (n bytes, any case) is one of the space-separated entries in list
static bool listed(const char *name, size_t n, const char *list) {
    for (const char *p = list; *p;) {
        while (*p == ' ') p++;
        const char *e = p;
        while (*e && *e != ' ') e++;
        if ((size_t)(e - p) == n && n && !strncasecmp(p, name, n)) return true;
        p = e;
    }
    return false;
}

bool http_req_host_ok(const char *req, const char *local_ip, const char *device_name,
                      const char *hostnames) {
    char host[256];
    if (!http_req_header(req, "Host", host, sizeof(host))) return false;
    char *colon = strchr(host, ':');
    if (colon) {
        if (strcmp(colon, ":80")) return false;
        *colon = '\0';
    }
    size_t n = strlen(host);
    if (n && host[n - 1] == '.') host[--n] = '\0'; // a fully qualified name may end in a dot
    if (!n) return false;
    if (!strcmp(host, local_ip)) return true;
    if (listed(host, n, hostnames)) return true;
    size_t d = strlen(device_name);
    if (!d || strncasecmp(host, device_name, d)) return false;
    return host[d] == '\0' || !strcasecmp(host + d, ".local");
}

const char *http_req_hostnames_parse(const char *in, char *out, size_t cap) {
    size_t off = 0;
    out[0] = '\0';
    for (const char *p = in; *p;) {
        while (*p == ' ' || *p == ',') p++;
        if (!*p) break;
        const char *e = p;
        while (*e && *e != ' ' && *e != ',') e++;
        size_t n = (size_t)(e - p);
        if (p[n - 1] == '.') n--; // a fully qualified name may end in a dot
        if (!n) return "hostnames: an entry is empty";
        for (size_t i = 0; i < n; i++) {
            char c = p[i];
            if (c == ':') return "hostnames: give names without a port";
            if (!isalnum((unsigned char)c) && c != '-' && c != '.')
                return "hostnames: letters, digits, hyphens and dots only";
            // every dot-separated label is non-empty and starts with a letter or digit
            if ((i == 0 || p[i - 1] == '.') && (c == '.' || c == '-'))
                return "hostnames: an entry is not a valid name";
        }
        if (off + (off ? 1 : 0) + n >= cap) return "hostnames: the list is too long";
        if (off) out[off++] = ' ';
        for (size_t i = 0; i < n; i++) out[off++] = (char)tolower((unsigned char)p[i]);
        out[off] = '\0';
        p = e;
    }
    return NULL;
}

void http_req_line_text(const char *req, char *out, size_t cap) {
    if (!cap) return;
    size_t n = 0;
    int spaces = 0;
    for (const char *p = req; *p && *p != '\r' && *p != '\n' && n + 1 < cap; p++) {
        if (*p == ' ' && ++spaces == 2) break; // before the HTTP version
        if (*p == '?' && spaces == 1) break;
        out[n++] = (*p >= 0x20 && *p < 0x7F) ? *p : '?';
    }
    out[n] = '\0';
}

bool http_refusal_log_ok(http_refusal_limit_t *l, uint32_t now_ms, unsigned *unlogged) {
    if (!l->started || now_ms - l->window_ms >= HTTP_REFUSAL_WINDOW_MS) {
        l->started = true;
        l->window_ms = now_ms;
        l->logged = 0;
    }
    if (l->logged >= HTTP_REFUSAL_LOG_MAX) {
        l->unlogged++;
        return false;
    }
    l->logged++;
    *unlogged = l->unlogged;
    l->unlogged = 0;
    return true;
}
