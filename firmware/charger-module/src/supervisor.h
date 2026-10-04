#pragma once

// The blade's own loop: EN, the controller's registers, faults, telemetry,
// and whether the port may run. One pass per turn of the PD stack's loop.
//
// The port is armed while all of this holds, and dark otherwise:
//   - the slot's EN is high and the converter has been set up since it rose,
//   - the controller has enabled the port and given it a current ceiling,
//   - no fault that pulls ALERT# is latched.
// A fault takes TCPP_EN low as well; the line comes up again when the
// controller clears the faults.

void supervisor_init(void);
void supervisor_run(void);

// What the PD Status message says about temperature: over once an
// over-temperature fault is latched, warning while either thermometer is
// warm (blade.h), normal otherwise.
typedef enum {
    SUPERVISOR_TEMP_NORMAL,
    SUPERVISOR_TEMP_WARNING,
    SUPERVISOR_TEMP_OVER,
} supervisor_temp_t;
supervisor_temp_t supervisor_temperature(void);

// The stack asked for VBUS and did not get it
void supervisor_power_failed(void);
// The stack did not start
void supervisor_stack_failed(void);
