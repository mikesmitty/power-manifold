#pragma once

#include <stddef.h>
#include <stdint.h>

// Serial seam under the UPS poller: lad_hw_pico.c is UART0 on the controller
// card's UPS header (pins.h); the host tests implement it as an emulated
// LAD supply. Both directions are non-blocking from the caller's view.

void lad_hw_init(void);
void lad_hw_write(const uint8_t *src, size_t n); // a whole request; fits the TX FIFO
size_t lad_hw_read(uint8_t *dst, size_t cap);    // whatever has arrived since the last call
