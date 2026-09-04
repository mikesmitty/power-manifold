#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "manifold.h"

// MQTT management surface (core 0): telemetry out, commands in, Home
// Assistant MQTT discovery so ports appear as entities with zero HA-side
// configuration. Disabled entirely when no broker host is configured.
//
// Topics (base = pwrman/<device_name>):
//   base/availability          LWT: online/offline, retained
//   base/status                1 Hz chassis JSON, retained
//   base/port/N/telemetry      1 Hz per-port JSON (N = 1-based)
//   base/port/N/set            command: ON/OFF (admin enable), hard_reset, src_cap
//   base/port/N/priority/set   command: 0-255 (0 = highest)
//   base/port/N/limit/set      command: advertised current ceiling, mA (500-5000)
//   base/fan/set               command: auto/on/off (legacy ON/OFF accepted)
//   base/budget/set            command: chassis budget in watts
//   base/event                 fault/contract/state events, QoS 1
//   base/update/latest         in, retained: {"version":"x.y.z","url":"http://..."}
//   base/update/state          out, retained: installed/latest for HA's update entity
//   base/update/set            command: "install" -> pull the latest url (OTA)

void mqtt_poll(uint32_t now_ms);
bool mqtt_is_connected(void);

// Publish one engine event to base/event; dropped when the broker is down.
// Called from the main loop's event drain (see main.c), which also feeds the
// persistent fault log so logging never depends on broker health.
void mqtt_event(const engine_evt_t *e);

// A port label changed: re-run discovery so Home Assistant picks up the new
// entity names (unique ids are unchanged, so entity ids stay put).
void mqtt_names_changed(void);
