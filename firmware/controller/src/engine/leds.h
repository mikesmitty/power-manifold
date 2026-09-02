#pragma once

#include <stdint.h>

#include "manifold.h"

// Backplane WS2812C chain (6 pixels, one per slot) behind front-panel light
// pipes. Driven by PIO from core 1.

void leds_init(void);
void leds_set_brightness(uint8_t brightness);
void leds_render(const telemetry_t *t, uint32_t now_ms);
// Override the chain with an attention pattern until until_ms (Improv
// "identify": which box is the one being provisioned)
void leds_identify(uint32_t until_ms);
