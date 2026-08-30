#pragma once

#include <stdbool.h>
#include <stdint.h>

// MQTT management surface (core 0): telemetry out, commands in, Home
// Assistant MQTT discovery so ports appear as entities with zero HA-side
// configuration. Disabled entirely when no broker host is configured.
//
// Topics (base = pwrman/<device_name>):
//   base/availability          LWT: online/offline, retained
//   base/status                1 Hz chassis JSON, retained
//   base/port/N/telemetry      1 Hz per-port JSON (N = 1-based)
//   base/port/N/set            command: ON/OFF (admin enable)
//   base/fan/set               command: ON/OFF
//   base/event                 fault/contract/state events, QoS 1

void mqtt_poll(uint32_t now_ms);
bool mqtt_is_connected(void);
