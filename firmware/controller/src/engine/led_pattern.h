#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "manifold.h"

// Status LED patterns for the front-panel light bar: the chassis light
// first in the chain, then one pixel per slot. This is the whole colour
// policy, kept free of hardware so the host suite can pin it; leds.c owns
// the PIO and just pushes what this renders.
//
// Layers, highest priority first:
//   hold       the front-panel button held past its long-press point: the
//              chain fills in blue toward the factory reset, all red as it
//              fires (button.h); above everything, the user is at the box
//   ack        a short press registered: the whole chain white for a moment
//   identify   Improv "which box is this": alternate pixels blue, swapping
//              at 2 Hz
//   boot       one-shot sweep along the chain at power-up (white or
//              rainbow), the chassis light first — doubles as a
//              chain-order check on a fresh chassis
//   chassis    the chassis light blinks red like a faulted port while the
//              DC bus is past its low or high flag; otherwise it breathes
//              blue while the BLE provisioning window is open, amber while
//              settings are open for first-time setup (no API token yet),
//              red while no link has an address; with nothing to report
//              it glows green at the master brightness, capped at
//              LED_CHASSIS_OK_MAX, so it stays dim on a bright chain and
//              dims no further than the other lights on a dimmed one
//   ports      per-slot state colours (spec §6.1); a sink that has finished
//              charging shows white, as idle does, whatever its contract state
//
// Master brightness 0 blanks everything except a faulted port or a bus
// fault, which keep blinking at LED_FAULT_FLOOR so a dark rack still shows
// a trip.

typedef struct { uint8_t r, g, b; } led_rgb_t;

typedef struct {
    uint8_t  brightness;        // 0-255
    uint8_t  chassis;           // LED_CHASSIS_* flags (manifold.h)
    uint8_t  boot_style;        // LED_BOOT_* (manifold.h)
    bool     boot_pending;      // sweep in progress since boot_start_ms
    uint32_t boot_start_ms;
    bool     identify_pending;  // identify pattern until identify_until_ms
    uint32_t identify_until_ms;
    uint8_t  hold;              // button hold progress (button_hold_progress), 0 = none
    bool     ack_pending;       // white flash until ack_until_ms
    uint32_t ack_until_ms;
} led_view_t;

// Chain order: the chassis light, then ports 1 to 6
#define LED_PIXELS           (NUM_PORTS + 1)
#define LED_CHASSIS_PIXEL    0
#define LED_PORT_PIXEL(i)    ((i) + 1)                    // port index 0-5

#define LED_BOOT_STEP_MS     150                          // per pixel
#define LED_BOOT_HOLD_MS     300                          // all lit, then release
#define LED_BOOT_SWEEP_MS    (LED_PIXELS * LED_BOOT_STEP_MS + LED_BOOT_HOLD_MS)
#define LED_CHASSIS_PERIOD_MS 2000                        // one breath of the chassis light
#define LED_CHASSIS_OK_MAX   32                           // brightness ceiling of the all-clear glow
#define LED_FAULT_FLOOR      16                           // brightness used for faults at 0
#define LED_IDENTIFY_FLOOR   32

void led_view_init(led_view_t *v, uint8_t brightness, uint8_t boot_style);

// Render the chain for now_ms. Clears the view's one-shots once they expire.
void led_pattern_render(led_view_t *v, const telemetry_t *t, uint32_t now_ms,
                        led_rgb_t out[LED_PIXELS]);
