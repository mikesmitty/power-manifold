#include "http_req.h"

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

bool http_req_host_ok(const char *req, const char *local_ip, const char *device_name) {
    char host[96];
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
    size_t d = strlen(device_name);
    if (!d || strncasecmp(host, device_name, d)) return false;
    return host[d] == '\0' || !strcasecmp(host + d, ".local");
}
