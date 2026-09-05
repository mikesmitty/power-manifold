#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "settings.h"

// The settings record as the management API's flat JSON object, both ways.
// Hardware-free: the web API, the console's `export` and the host tests
// share it. Keys are the /api/v1/settings vocabulary; an export is the same
// object plus "format" and "fw", and optionally the secrets, so importing is
// just POSTing an export back.

typedef struct {
    bool fan_on;  // the engine's live fan state, reported as fan_mode when manual
    bool setup;   // the request came in on the first-time setup secret
    bool secrets; // include wifi_pass, mqtt_pass and token
    bool export;  // add "format" and "fw"
} settings_json_opts_t;

size_t settings_json_build(char *out, size_t cap, const settings_t *s,
                           const settings_json_opts_t *o);

typedef struct {
    bool fan_mode_given;  // "fan_mode" was in the body: apply it
    bool fan_manual_on;   // ...and it was "on" (else "off"), when not auto
    bool fan_thresholds;  // fan_on_w / fan_off_w / fan_on_ma changed
} settings_apply_t;

// Apply any subset of the keys in body to *s, which the caller pre-filled
// with the current settings; absent keys keep their value and unknown ones
// are ignored. NULL on success, else a message for a 400 and *s is
// untrustworthy. via_setup: the result must carry an API token.
const char *settings_json_apply(const char *body, settings_t *s, bool via_setup,
                                settings_apply_t *out);
