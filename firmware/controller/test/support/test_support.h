#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "manifold.h"
#include "sim/sim_blades.h"

// Shared fixture: fresh settings/sim/budget/fsm state, an engine-equivalent
// tick loop, and captured engine events.

extern uint32_t now_ms;
extern telemetry_t tele; // per-port telemetry from the last tick

void support_reset(uint32_t budget_mw);

// Run n 10ms engine ticks: presence from the sim expander, alert sweep on the
// sim's wire-OR line, port_fsm_tick for every port, then the chassis total
// and the fan policy — the same order engine_main() uses.
void tick(uint32_t n);
static inline void tick_ms(uint32_t ms) { tick(ms / 10); }

uint8_t port_state(uint8_t i);

// captured events (port = 0xFF matches any port)
int evt_count(uint8_t type, uint8_t port);
const engine_evt_t *evt_last(uint8_t type, uint8_t port);
void evt_clear(void);
