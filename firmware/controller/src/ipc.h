#pragma once

#include "manifold.h"

// Inter-core plumbing. Core 1 (engine) publishes telemetry and events and
// consumes commands; core 0 (network/UI) does the reverse. Nothing on core 0
// may touch I2C or the expander directly.

void ipc_init(void);

// command queue (core 0 -> core 1)
bool ipc_cmd_push(const engine_cmd_t *cmd);
bool ipc_cmd_pop(engine_cmd_t *cmd);

// event queue (core 1 -> core 0)
bool ipc_evt_push(const engine_evt_t *evt);
bool ipc_evt_pop(engine_evt_t *evt);

// telemetry snapshot: seqlock so the engine's writer never blocks
void ipc_snapshot_publish(const telemetry_t *t); // core 1 only
void ipc_snapshot_read(telemetry_t *t);          // core 0

// engine heartbeat; core 0 feeds the hardware watchdog only while this is fresh
void ipc_engine_heartbeat(void);
bool ipc_engine_alive(void);
