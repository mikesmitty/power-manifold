#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// The parts of a plain http:// URL, for lwIP's HTTP client (the update pull
// and the update check). No TLS on this stack, so https:// is refused by
// name. Pure string code; host-tested.

#define HTTP_URL_HOST_MAX 64  // with the NUL
#define HTTP_URL_PATH_MAX 160

typedef struct {
    char     host[HTTP_URL_HOST_MAX];
    char     path[HTTP_URL_PATH_MAX]; // "/" when the URL has none
    uint16_t port;                    // 80 when the URL has none
} http_url_t;

// False with a message in err when the URL cannot be used.
bool http_url_parse(const char *url, http_url_t *out, char *err, size_t errlen);
