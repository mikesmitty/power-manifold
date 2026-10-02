#include "update_latest.h"

#include <stdio.h>
#include <string.h>

#include "net/http_url.h"
#include "net/jsonlite.h"
#include "update_image.h"

static char     version[UPDATE_LATEST_VERSION_MAX];
static char     url[UPDATE_LATEST_URL_MAX];
static unsigned seq;

bool update_latest_offer(const char *v, const char *u) {
    uint32_t word = update_version_word(v);
    if (!word || strlen(v) >= sizeof(version)) return false;
    http_url_t parts;
    if (strlen(u) >= sizeof(url) || !http_url_parse(u, &parts, NULL, 0)) return false;
    if (version[0] && word <= update_version_word(version)) return false;
    strcpy(version, v);
    strcpy(url, u);
    seq++;
    return true;
}

bool update_latest_offer_json(const char *json) {
    char v[UPDATE_LATEST_VERSION_MAX], u[UPDATE_LATEST_URL_MAX];
    if (json_get_str(json, "version", v, sizeof(v)) != 1) return false;
    if (json_get_str(json, "url", u, sizeof(u)) != 1) return false;
    return update_latest_offer(v, u);
}

const char *update_latest_version(void) {
    return version;
}

const char *update_latest_url(void) {
    return url;
}

bool update_latest_newer_than(const char *running) {
    return version[0] && update_version_word(version) > update_version_word(running);
}

unsigned update_latest_seq(void) {
    return seq;
}

void update_latest_clear(void) {
    version[0] = url[0] = '\0';
    seq++;
}

bool update_source_valid(const char *base, size_t cap) {
    size_t len = strlen(base);
    if (len == 0) return true;
    if (len >= cap || base[len - 1] == '/') return false;
    for (const char *p = base; *p; p++)
        if (*p <= ' ' || *p > '~') return false;
    http_url_t parts;
    return http_url_parse(base, &parts, NULL, 0);
}

bool update_source_pointer_url(const char *base, const char *board, char *out, size_t cap) {
    int n = snprintf(out, cap, "%s/controller/%s/latest.json", base, board);
    return n > 0 && (size_t)n < cap;
}
