#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "tcpp02.h"

// The port's power path: the converter, the over-voltage threshold that
// follows it, and the TCPP02 with the VBUS switch. Hardware-free: the parts
// through bus.h, the pins through hw.h and dac.h, the readings through
// meter.h.
//
// Calls that move VBUS block until it has arrived or a timeout has passed,
// the way the PD stack expects of its power callbacks.
//
// What holds in every state:
//   - the over-voltage threshold covers the higher of the two voltages for
//     as long as the output is moving between them;
//   - the converter's output is enabled only with the VBUS switch closed, and
//     ramps from 0 V behind it;
//   - the converter runs forced PWM while its output has to come down, and
//     as strapped otherwise.

typedef enum {
    POWER_OK = 0,
    POWER_BUS,       // a part did not answer, or did not take what was written
    POWER_TRIPPED,   // TCPP_EN is low against the drive: the over-voltage comparator
    POWER_TIMEOUT,   // VBUS did not get where it was sent in time
    POWER_NOT_READY, // no converter: EN is low, or it has not been set up since EN rose
} power_result_t;

#define POWER_VSAFE0V_MV          800   // vSafe0V, upper bound
#define POWER_VSAFE5V_MV          5000
#define POWER_SETTLE_TIMEOUT_MS   250   // inside tSrcReady (285 ms)
#define POWER_DISCHARGE_TIMEOUT_MS 600  // inside tSafe0V (650 ms)
#define POWER_FIXED_MARGIN_PCT    110   // converter limit over a fixed contract's current
#define POWER_LIMIT_FLOOR_MA      500

void power_init(void); // state only: out of reset everything is off

// TCPP_EN: threshold for 5 V first, then the line high, then the TCPP02
// found and left in hibernate (CC switches open, VBUS switch open)
power_result_t power_port_up(void);
void           power_port_down(void); // TCPP_EN low: the TCPP02 off, its registers lost

// The converter loses its registers whenever EN is low
power_result_t power_converter_up(void);
void           power_converter_lost(void);
bool           power_converter_ready(void);

power_result_t power_cc_mode(tcpp02_mode_t mode);
power_result_t power_vconn(tcpp02_vconn_t cc);

power_result_t power_vbus_on(void);  // to vSafe5V
power_result_t power_vbus_off(void); // and discharged to vSafe0V
bool           power_vbus_is_on(void);

// A contract's voltage and current. Fixed contracts get the converter limit
// POWER_FIXED_MARGIN_PCT of their current, so the limit never bites inside
// what was agreed; a PPS contract's current is the limit itself.
power_result_t power_set(uint32_t mv, uint32_t ma, bool pps);
uint32_t       power_setpoint_mv(void);

// Everything off at once, nothing waited for: for a fault
void power_shutdown(void);

// BLADE_FAULT_* seen since the last call: the over-voltage trip, the
// TCPP02's flags while FLGn is low, the converter's while FB/INT is low.
// Reading clears the parts' own latches.
uint16_t power_poll_faults(void);
