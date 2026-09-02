#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Bus seam under the W6100 driver: the SPI transactions, the two GPIOs and
// a millisecond clock. w6100_hw_pico.c implements it on SPI0; the host
// tests implement it as an emulated chip.

void w6100_hw_init(void);
void w6100_hw_reset(bool asserted);          // RSTn (active low)
void w6100_hw_select(bool selected);         // CSn (active low)
void w6100_hw_write(const uint8_t *src, size_t n);
void w6100_hw_read(uint8_t *dst, size_t n);
void w6100_hw_delay_ms(uint32_t ms);
uint32_t w6100_hw_ms(void);
