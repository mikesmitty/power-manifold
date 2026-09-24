#pragma once
// The two backplane reset lines (MUX_RST#, EXP_RST#), one helper per board.
//
// Pico 2 W carrier (RST_ASSERTED_LEVEL 0): the backplane holds each line up
// with 10k to 5 V, and the TCA9548A/TCA9539 want 0.7 x VCC = 3.5 V for a
// high, so a 3.3 V push-pull high sits UNDER their threshold — on the bench
// (2026-09-23) the expander dropped back to its power-on registers at random
// and went silent whenever a blade segment was open. Released therefore
// means high impedance (pulls off): the 10k takes the line to 5 V, which the
// RP2350 pad tolerates while IOVDD is up. Asserted drives low.
//
// Controller card (RST_ASSERTED_LEVEL 1): the GPIO drives a 2N7002 gate, so
// the pin is a plain push-pull output and a high asserts the reset.
#include "hardware/gpio.h"

#include "pins.h"

static inline void rst_line_setup(uint pin) {
    gpio_init(pin);
#if RST_ASSERTED_LEVEL == 0
    gpio_disable_pulls(pin);
    gpio_set_dir(pin, GPIO_IN); // released
#else
    gpio_put(pin, !RST_ASSERTED_LEVEL); // level first, so it never glitches asserted
    gpio_set_dir(pin, GPIO_OUT);
#endif
}

static inline void rst_line_assert(uint pin) {
#if RST_ASSERTED_LEVEL == 0
    gpio_put(pin, 0);
    gpio_set_dir(pin, GPIO_OUT);
#else
    gpio_put(pin, RST_ASSERTED_LEVEL);
#endif
}

static inline void rst_line_release(uint pin) {
#if RST_ASSERTED_LEVEL == 0
    gpio_set_dir(pin, GPIO_IN);
#else
    gpio_put(pin, !RST_ASSERTED_LEVEL);
#endif
}
