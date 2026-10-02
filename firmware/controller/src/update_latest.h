#pragma once

#include <stdbool.h>
#include <stddef.h>

// The newest release this controller has heard of, and where to pull it
// from. Two things feed it: the controller's own check against the update
// source (net/update_check.h) and the retained MQTT pointer (mqtt.h); both
// hand over the same {"version":"x.y.z","url":"http://…"} object. Home
// Assistant's update entity and the console's `update latest` read it.
//
// Nothing here is trusted. The pointer only names an image; whether that
// image installs is decided by its signature and version (update.h).
//
// Pure code, host-tested. Core 0; the lwIP callbacks that offer pointers run
// under the lwIP lock, so main-loop readers take net_lock() around a read.

#define UPDATE_LATEST_VERSION_MAX 16  // with the NUL
#define UPDATE_LATEST_URL_MAX     160

// Takes a pointer when it names a release newer than the one already known
// (or nothing is known yet). False when it is malformed or no newer.
bool update_latest_offer(const char *version, const char *url);
bool update_latest_offer_json(const char *json);

const char *update_latest_version(void); // "" until a pointer is taken
const char *update_latest_url(void);
bool        update_latest_newer_than(const char *running); // known, and newer than x.y.z
unsigned    update_latest_seq(void);     // changes whenever a pointer is taken
void        update_latest_clear(void);

// The update source setting: "" (no check) or a plain http:// base URL short
// enough to store, without a trailing slash.
#define UPDATE_SOURCE_DEFAULT "http://fw.powermanifold.io"
bool update_source_valid(const char *base, size_t cap);
// <base>/controller/<board>/latest.json
bool update_source_pointer_url(const char *base, const char *board, char *out, size_t cap);
