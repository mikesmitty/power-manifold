#pragma once

#include <stdbool.h>
#include <stddef.h>

// Pull-style OTA: fetch a firmware image over plain HTTP (lwIP http client)
// and stream it into the update core; on success the usual trial reboot is
// scheduled. Plain http:// only — TLS is out of scope on this stack; what
// makes an image trustworthy is its signature (update.h), not the transport,
// so any static file server works. One pull at a time; the update core's own
// exclusivity covers a racing push.
//
// allow: UPDATE_ALLOW_* for this pull. Zero from anything the network can
// reach; only the console passes more.

bool ota_pull_start(const char *url, unsigned allow, char *err, size_t errlen);
bool ota_pull_busy(void);
