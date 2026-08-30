#pragma once

#include <stdint.h>

#include "manifold.h"

// Backplane WS2812C chain (6 pixels, one per slot) behind front-panel light
// pipes. Driven by PIO from core 1.

void leds_init(void);
void leds_set_brightness(uint8_t brightness);
void leds_render(const telemetry_t *t, uint32_t now_ms);
