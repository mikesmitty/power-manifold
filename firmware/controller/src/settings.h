#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "manifold.h"

// Persistent configuration. Stored in the last two 4KB flash sectors as a
// ping-pong pair: saves alternate sectors with an incrementing sequence
// number, so a power failure mid-write leaves the previous copy intact.

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
    uint32_t crc; // must remain last
} settings_t;

extern settings_t g_settings;

void settings_load(void);     // falls back to defaults on empty/corrupt flash
bool settings_save(void);     // core 0 only; engine pauses briefly via flash_safe_execute
void settings_defaults(void);
