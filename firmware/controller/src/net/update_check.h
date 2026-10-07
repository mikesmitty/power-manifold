#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// The controller's own look for a newer release: a small GET of
//   <update_url>/controller/<board>/latest.json
// shortly after the network comes up and once a day after that, handed to
// update_latest.h. Installing a release is asked for (Home Assistant's
// update entity, the web page, the console's `update latest`) or happens by
// itself (update_auto.h), and passes the same signature and version checks
// as any other image.
// An empty update_url setting turns the check off. Core 0, main loop.

void update_check_poll(uint32_t now_ms);
// Ask now instead of at the next scheduled time (the console).
bool update_check_now(char *err, size_t errlen);
// For the status JSON: "off", "never", "checking", "ok" or "failed", and the
// seconds since the last check finished (0 before the first).
const char *update_check_state(uint32_t now_ms, uint32_t *age_s);
// One line for `info`: off / not yet run / when it last worked or failed.
void update_check_status(uint32_t now_ms, char *out, size_t cap);

// Automatic installs (update_auto.h). Core 0, main loop.
void update_auto_poll(uint32_t now_ms);
// At boot and once a trial image is committed: a restart that went into an
// image other than the one now running was rolled back, and that version
// is not installed automatically again.
void update_auto_settle(void);
// Put automatic installs off for UPDATE_AUTO_POSTPONE_S (needs the clock),
// or skip the newest known release. Install still works either way.
bool update_auto_postpone(char *err, size_t errlen);
bool update_auto_skip(char *err, size_t errlen);
// For the status JSON, under the lwIP lock like update_latest.h: the
// update_auto_name() state and, while waiting, the seconds left.
const char *update_auto_state(uint32_t now_ms, uint32_t *wait_s);
