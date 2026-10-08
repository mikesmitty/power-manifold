// Prints the settings object the way GET /api/v1/settings and the export
// with secrets build it, one JSON object per line, for
// tools/check_openapi.py to compare with the API description. Which keys
// appear depends only on the options, not on the values, so a zeroed
// record serves.

#include <stdio.h>
#include <string.h>

#include "net/mqtt_tls.h"
#include "settings_json.h"

// Linked in by settings_util.c; the dump builds from its own record.
settings_t g_settings;

// Only the import side calls this; see test_support.c.
const char *mqtt_ca_check(const uint8_t *der, size_t len) {
    (void)der;
    (void)len;
    return NULL;
}

static char json[SETTINGS_JSON_MAX];

static int dump(const char *label, const settings_json_opts_t *o) {
    static settings_t s;
    memset(&s, 0, sizeof(s));
    if (!settings_json_build(json, sizeof(json), &s, o)) {
        fprintf(stderr, "settings_json_dump: %s does not fit\n", label);
        return 1;
    }
    printf("%s %s\n", label, json);
    return 0;
}

int main(void) {
    settings_json_opts_t get = {0};
    settings_json_opts_t export = {.secrets = true, .export = true};
    return dump("settings", &get) | dump("export", &export);
}
