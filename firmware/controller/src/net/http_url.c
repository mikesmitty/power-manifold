#include "http_url.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool eout(char *err, size_t errlen, const char *msg) {
    if (err && errlen) snprintf(err, errlen, "%s", msg);
    return false;
}

bool http_url_parse(const char *url, http_url_t *out, char *err, size_t errlen) {
    if (!strncmp(url, "https://", 8))
        return eout(err, errlen, "https unsupported; serve the image over plain http");
    if (strncmp(url, "http://", 7))
        return eout(err, errlen, "url must start with http://");

    const char *h = url + 7;
    const char *path = strchr(h, '/');
    const char *colon = strchr(h, ':');
    if (colon && path && colon > path) colon = NULL; // ':' inside the path

    size_t hlen = (size_t)((colon ? colon : path ? path : h + strlen(h)) - h);
    if (hlen == 0 || hlen >= sizeof(out->host))
        return eout(err, errlen, "bad host in url");
    memcpy(out->host, h, hlen);
    out->host[hlen] = '\0';

    out->port = 80;
    if (colon) {
        long pn = strtol(colon + 1, NULL, 10);
        if (pn <= 0 || pn > 65535) return eout(err, errlen, "bad port in url");
        out->port = (uint16_t)pn;
    }
    if (path && strlen(path) >= sizeof(out->path))
        return eout(err, errlen, "path in url too long");
    snprintf(out->path, sizeof(out->path), "%s", path ? path : "/");
    return true;
}
