#pragma once

#include <stdbool.h>
#include <stdint.h>

// Bus-sag current cap. When the DC bus is pulled down by the load (a long
// input lead at 30 A, a battery near empty, a 24 V supply trimmed low) and
// reaches the backplane's under-voltage threshold, a little under 18 V, the
// backplane switches the whole bus off: every port and the controller with
// it. This module acts before that point. While the bus is under 20 V it
// caps every port's advertised current at 3 A, so no sink can hold a 5 A
// PDO or APDO and no port delivers more than 60 W. Sinks that are attached
// renegotiate to a smaller contract; no port is switched off. The numbers
// are fixed, not settings: this is a safety cut-off.
//
// Hardware-free. Core 0 calls bus_cap_poll with the bus monitor's reading
// (vin.h) and, when the cap changes, sends the engine CMD_SET_CEILING.

#define BUS_CAP_MA      3000  // the per-port current limit while the cap is on
#define BUS_CAP_ON_MV   20000 // the cap goes on when the bus reads below this
#define BUS_CAP_OFF_MV  20500 // and comes off once the bus has stayed at or above this
#define BUS_CAP_HOLD_MS 30000 // for this long without a dip

void bus_cap_init(void);
// Call once per main-loop pass with the bus monitor's reading in mV; 0
// means no reading, and nothing changes. Returns true when the cap went on
// or off during this call.
//
// The first reading after bus_cap_init is compared with BUS_CAP_OFF_MV
// rather than BUS_CAP_ON_MV: a chassis that powers up with its bus between
// 20.0 and 20.5 V starts with the cap on, and the cap comes off the same
// way it would after a sag, once the bus has held 20.5 V for 30 s. This is
// for a chassis that was just switched off by the backplane's under-voltage
// cut: when the bus recovers it powers up again, and without this it would
// put the same load back on the same supply.
bool bus_cap_poll(uint32_t now_ms, uint32_t vin_mv);
bool bus_cap_on(void);
uint32_t bus_cap_ma(void); // BUS_CAP_MA while on, 0 otherwise
