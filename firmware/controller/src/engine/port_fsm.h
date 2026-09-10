#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "manifold.h"

// Per-port supervisory state machine. Runs on core 1 only; every entry point
// assumes it may select the port's mux channel and talk to blade ICs.

void port_fsm_init(void);
// Once, with the first presence read. `powered` is the EN set found on the
// expander at a warm start (bit N = port N; 0 on a cold start): those blades
// keep their power and are adopted — supervised from here on without an EN
// cut, unless the boot policy says off or no blade is seated behind the bit.
// The remaining seated blades are brought up one at a time in priority
// order, BOOT_STAGGER_MS apart, so six sinks do not inrush and negotiate on
// the DC input at once. Blades seated later are not paced. Returns the
// adopted set.
uint8_t port_fsm_boot_inventory(const bool *present, uint8_t powered, uint32_t now_ms);
void port_fsm_tick(uint8_t port, bool present, uint32_t now_ms,
                   port_telemetry_t *out);
void port_fsm_cmd(uint8_t port, const engine_cmd_t *cmd);

// GLOBAL_ALERT# asserted: sweep powered ports, kill the offender(s)
void port_fsm_alert_sweep(uint32_t now_ms);
