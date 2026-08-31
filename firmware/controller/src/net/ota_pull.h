#pragma once

#include <stdbool.h>
#include <stddef.h>

// Pull-style OTA: fetch a firmware image over plain HTTP (lwIP http client)
// and stream it into the update core; on success the usual trial reboot is
// scheduled. Plain http:// only — TLS is out of scope on this stack, so the
// image is served from the LAN (any static file server works). One pull at a
// time; the update core's own exclusivity covers a racing push.

bool ota_pull_start(const char *url, char *err, size_t errlen);
bool ota_pull_busy(void);
