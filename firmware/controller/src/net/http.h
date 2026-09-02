#pragma once

#include <stdbool.h>
#include <stdint.h>

// Minimal HTTP server (core 0, lwIP raw API) — the broker-independent local
// management path.
//
//   GET  /                    embedded status page, with a settings panel
//   GET  /api/v1/status       JSON snapshot
//   GET  /api/v1/settings     name, broker, port, user, whether a password /
//                             token is stored; budget, fan mode + thresholds
//   POST /api/v1/settings     any subset of {"name","mqtt_host","mqtt_port",
//                             "mqtt_user","mqtt_pass","token","budget_w",
//                             "fan_mode","fan_on_w","fan_off_w","fan_on_ma"};
//                             saved to flash at once; budget/fan apply live,
//                             name/broker take a reboot (reboot_required)
//   POST /api/v1/port/<n>     {"action":"enable"|"disable"|"hard_reset"|"src_cap"}
//   POST /api/v1/fan          {"on":true|false} or {"mode":"auto"}
//   POST /api/v1/budget       {"watts":N}
//   POST /api/v1/reboot       plain reboot shortly after the response
//   POST /api/v1/update       OTA: body = firmware image (uf2 or bin); writes
//                             the inactive A/B slot, then reboots into a trial
//
// POST requires "Authorization: Bearer <token>" when an API token is set.
// /settings always needs a bearer: the token, or — while no token is stored —
// the one-shot setup secret Improv puts in its redirect URL (?s=...); a
// request on the secret must set a token, which retires the secret.

void http_init(void);

// Mint the setup secret (good for 10 minutes, or until a token exists).
// Improv calls this with the network lock held; returns static storage.
const char *http_setup_secret_issue(uint32_t now_ms);

// A reboot was requested over the API and its delay has elapsed; the main
// loop does the reboot (flushing any debounced settings save first).
bool http_reboot_due(uint32_t now_ms);
