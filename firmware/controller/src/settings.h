#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "manifold.h"

// Persistent configuration. Stored as a ping-pong pair of 4KB sectors: saves
// alternate sectors with an incrementing sequence number, so a power failure
// mid-write leaves the previous copy intact. The pair lives at the start of
// the partition table's "data" partition (see flash_map.h), falling back to
// the legacy last-two-sectors-of-flash location on unpartitioned boards.

#define SETTINGS_MAGIC 0x504D4643u // "PMFC"

typedef struct {
    uint32_t magic;
    uint32_t version;  // struct layout version
    uint32_t seq;      // ping-pong sequence
    char     wifi_ssid[33];
    char     wifi_pass[65];
    char     mqtt_host[64]; // empty = MQTT disabled
    uint16_t mqtt_port;
    char     mqtt_user[33];
    char     mqtt_pass[65];
    char     device_name[32]; // hostname / mDNS / MQTT topic id
    char     api_token[33];   // empty = REST mutations unauthenticated
    uint32_t budget_mw;       // chassis power budget
    uint32_t port_limit_ma[NUM_PORTS]; // per-port PDO current ceiling
    uint8_t  port_priority[NUM_PORTS]; // 0 = highest; throttle victims picked from the bottom
    uint8_t  led_brightness;  // 0-255
    // -- added in layout version 2 (older records upgrade on load) --
    uint8_t  fan_auto;        // 1: engine drives the fan from total power
    uint16_t fan_on_w;        // auto: on at/above this
    uint16_t fan_off_w;       // auto: off at/below this (hysteresis band)
    // -- added in layout version 3 --
    uint16_t fan_on_ma;       // auto: also on while any contract exceeds this (0 = off)
    uint32_t crc; // must remain last
} settings_t;

extern settings_t g_settings;

void settings_load(void);     // falls back to defaults on empty/corrupt flash
bool settings_save(void);     // core 0 only; engine pauses briefly via flash_safe_execute
void settings_defaults(void);

// Debounced persistence for remote mutations (MQTT/REST): mark now, and the
// main loop's settings_save_poll flushes once things go quiet for a few
// seconds. Returns 0 when idle, 1 after a successful save, -1 on failure.
void settings_save_later(void);
int  settings_save_poll(uint32_t now_ms);

// One-shot re-home of settings found at the legacy location on a freshly
// partitioned board. Needs flash writes, so call from core 0 once the engine
// is running (flash_safe_execute refuses before core 1 can be parked).
bool settings_migration_pending(void);
bool settings_migrate(void);
