#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "pins.h"

// TCA9539 16-bit GPIO expander: blade EN outputs, PRES# inputs, fan switch.
// Engine (core 1) only.

// Cold start: pulse EXP_RST#, then outputs all low BEFORE the direction
// registers (spec §6.4). Every EN drops.
bool tca9539_init(void);
// Warm start: when the expander is still programmed the way tca9539_init
// leaves it — this controller rebooted while the backplane's 5 V stayed up —
// adopt its output register untouched and return true. False when it holds
// the power-on state (or does not answer); the caller then runs
// tca9539_init.
bool tca9539_attach(void);
// The same look with the two ways of "false" told apart: an expander that
// answers with something other than our configuration has been reset (cold
// start), one that does not answer may only have been missed, and treating
// that as cold costs every port its power.
typedef enum { TCA9539_OURS, TCA9539_POWER_ON, TCA9539_NO_ANSWER } tca9539_found_t;
tca9539_found_t tca9539_find(void);
// The expander answered and its configuration word is no longer ours (a
// reset leaves the power-on 0xFFFF). A read that fails is not that: the
// remedy (tca9539_recover) resets the part, and a missed read must not.
bool tca9539_config_lost(void);
// Re-apply outputs then direction after an unexpected reset, keeping every EN as it was
bool tca9539_recover(void);
// The output register as last written or adopted: TCA9539_EN_BIT(port) and
// TCA9539_FAN_BIT.
uint16_t tca9539_outputs(void);

bool tca9539_set_en(uint8_t port, bool on);
bool tca9539_set_fan(bool on);
bool tca9539_all_en_off(void); // single write: fault/panic path
bool tca9539_read_inputs(uint16_t *inputs);

// helper over a read_inputs() value; PRES# is active low
static inline bool tca9539_present_from(uint16_t inputs, uint8_t port) {
    return !(inputs & (1u << TCA9539_PRES_BIT(port)));
}
