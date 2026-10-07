#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// The controller's own look for a newer release: a small GET of
//   <update_url>/controller/<board>/latest.json
// shortly after the network comes up and once a day after that, handed to
// update_latest.h. It only learns that a release exists; installing one is
// asked for (Home Assistant's update entity, the console's `update latest`)
// and passes the same signature and version checks as any other image.
// An empty update_url setting turns the check off. Core 0, main loop.

void update_check_poll(uint32_t now_ms);
// Ask now instead of at the next scheduled time (the console).
bool update_check_now(char *err, size_t errlen);
// For the status JSON: "off", "never", "checking", "ok" or "failed", and the
// seconds since the last check finished (0 before the first).
const char *update_check_state(uint32_t now_ms, uint32_t *age_s);
// One line for `info`: off / not yet run / when it last worked or failed.
void update_check_status(uint32_t now_ms, char *out, size_t cap);
