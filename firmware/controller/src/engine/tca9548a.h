#pragma once

#include <stdbool.h>
#include <stdint.h>

// TCA9548A 8-channel I2C mux. Channel N carries slot N+1. Engine (core 1) only.

void tca9548a_init(void);
bool tca9548a_select(uint8_t channel); // exclusive: selects one, deselects rest
bool tca9548a_deselect_all(void);
void tca9548a_hw_reset(void); // pulse MUX_RST#, clears a hung downstream bus
