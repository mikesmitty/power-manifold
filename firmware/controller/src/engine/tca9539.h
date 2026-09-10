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
