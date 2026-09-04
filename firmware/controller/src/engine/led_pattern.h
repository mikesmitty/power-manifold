#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "manifold.h"

// Status LED patterns for the backplane chain, one pixel per slot. This is
// the whole colour policy, kept free of hardware so the host suite can pin
// it; leds.c owns the PIO and just pushes what this renders.
//
// Layers, highest priority first:
//   identify   Improv "which box is this": blue halves alternating at 2 Hz
//   boot       one-shot sweep 1..6 at power-up (white or rainbow) — doubles
//              as a chain-order check on a fresh chassis
//   comet      chassis condition overlay: a two-pixel comet crosses the
//              chain once every LED_COMET_PERIOD_MS, blue while the BLE
//              provisioning window is open, white while no link has an
//              address; port colours stay visible underneath
//   ports      per-slot state colours (spec §6.1)
//
// Master brightness 0 blanks everything except a faulted port, which keeps
// blinking at LED_FAULT_FLOOR so a dark rack still shows a trip.

typedef struct { uint8_t r, g, b; } led_rgb_t;

typedef struct {
    uint8_t  brightness;        // 0-255
    uint8_t  chassis;           // LED_CHASSIS_* flags (manifold.h)
    uint8_t  boot_style;        // LED_BOOT_* (manifold.h)
    bool     boot_pending;      // sweep in progress since boot_start_ms
    uint32_t boot_start_ms;
    bool     identify_pending;  // identify pattern until identify_until_ms
    uint32_t identify_until_ms;
} led_view_t;

#define LED_BOOT_STEP_MS     150                          // per pixel
#define LED_BOOT_HOLD_MS     300                          // all lit, then release
#define LED_BOOT_SWEEP_MS    (NUM_PORTS * LED_BOOT_STEP_MS + LED_BOOT_HOLD_MS)
#define LED_COMET_PERIOD_MS  3000
#define LED_COMET_STEP_MS    60                           // per pixel
#define LED_FAULT_FLOOR      16                           // brightness used for faults at 0
#define LED_IDENTIFY_FLOOR   32

void led_view_init(led_view_t *v, uint8_t brightness, uint8_t boot_style);

// Render the chain for now_ms. Clears the view's one-shots once they expire.
void led_pattern_render(led_view_t *v, const telemetry_t *t, uint32_t now_ms,
                        led_rgb_t out[NUM_PORTS]);
