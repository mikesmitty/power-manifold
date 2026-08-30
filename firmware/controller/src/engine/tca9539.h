#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "pins.h"

// TCA9539 16-bit GPIO expander: blade EN outputs, PRES# inputs, fan switch.
// Engine (core 1) only.

bool tca9539_init(void);
bool tca9539_set_en(uint8_t port, bool on);
bool tca9539_set_fan(bool on);
bool tca9539_all_en_off(void); // single write: fault/panic path
bool tca9539_read_inputs(uint16_t *inputs);

// helper over a read_inputs() value; PRES# is active low
static inline bool tca9539_present_from(uint16_t inputs, uint8_t port) {
    return !(inputs & (1u << TCA9539_PRES_BIT(port)));
}
