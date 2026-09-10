#pragma once

#include <stdbool.h>
#include <stdint.h>

// ADC seam under the bus-voltage monitor (vin.c): vin_hw_pico.c is the
// controller card's GP28/ADC2 behind its 120k/10k divider; the host tests
// feed it raw counts. init returns false on a board with no divider (the
// Pico 2 W carrier), and the monitor then reports "not fitted".

bool vin_hw_init(void);
uint16_t vin_hw_read(void); // one 12-bit conversion, 0-4095
