#pragma once

// Minimal HTTP server (core 0, lwIP raw API) — the broker-independent local
// management path.
//
//   GET  /                    embedded status page
//   GET  /api/v1/status       JSON snapshot
//   POST /api/v1/port/<n>     {"action":"enable"|"disable"|"hard_reset"|"src_cap"}
//   POST /api/v1/fan          {"on":true|false}
//   POST /api/v1/update       OTA: body = firmware image (uf2 or bin); writes
//                             the inactive A/B slot, then reboots into a trial
//
// POST requires "Authorization: Bearer <token>" when an API token is set.

void http_init(void);
