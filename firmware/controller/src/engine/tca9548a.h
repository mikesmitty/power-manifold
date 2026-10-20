#pragma once

#include <stdbool.h>
#include <stdint.h>

// TCA9548A 8-channel I2C mux. Channel N carries slot N+1. Engine (core 1) only.

void tca9548a_init(void);
// Exclusive: selects one, deselects the rest. A device found holding the
// data line, before or after the switch, is clocked free first.
bool tca9548a_select(uint8_t channel);
bool tca9548a_deselect_all(void);
void tca9548a_hw_reset(void); // pulse MUX_RST#, clears a hung downstream bus
// The channel whose device was last clocked free (TCA9548A_UPSTREAM when no
// channel was selected), once; -1 when none was since the last call
int tca9548a_take_freed(void);
#define TCA9548A_UPSTREAM 0xFF
