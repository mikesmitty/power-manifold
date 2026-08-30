#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "manifold.h"

// Per-port supervisory state machine. Runs on core 1 only; every entry point
// assumes it may select the port's mux channel and talk to blade ICs.

void port_fsm_init(void);
void port_fsm_tick(uint8_t port, bool present, uint32_t now_ms,
                   port_telemetry_t *out);
void port_fsm_cmd(uint8_t port, const engine_cmd_t *cmd);

// GLOBAL_ALERT# asserted: sweep powered ports, kill the offender(s)
void port_fsm_alert_sweep(uint32_t now_ms);
