#pragma once

#include <stdbool.h>
#include <stdint.h>

// Front-panel button (the controller card's SW3 on GP22, to ground through
// 1k with 100 nF across the input; the Pico 2 W carrier has no button, but
// GP22 to ground stands in for one on the bench). Polled from the main
// loop on core 0, debounced here. Three gestures:
//   short press            released before BUTTON_LONG_MS: wake — a dimmed
//                          or blanked chain shows at full brightness for a
//                          while (led_sched.h), and a brief flash says the
//                          press registered
//   long press             released after BUTTON_LONG_MS: open the BLE
//                          provisioning window
//   hold to the end        BUTTON_VERY_LONG_MS without release: factory
//                          reset (defaults saved, reboot); fires while
//                          still held, the release is ignored
// From BUTTON_LONG_MS on, button_hold_progress() runs 1..254 toward the
// reset so the chain can fill up as a warning (led_pattern.h), then 255 as
// it fires. Hardware-free above button_hw.h.

typedef enum {
    BUTTON_NONE = 0,
    BUTTON_SHORT,
    BUTTON_LONG,
    BUTTON_VERY_LONG,
} button_action_t;

#define BUTTON_DEBOUNCE_MS  30
#define BUTTON_LONG_MS      3000
#define BUTTON_VERY_LONG_MS 10000

void button_init(void);
bool button_fitted(void);

// One gesture per call at most; BUTTON_NONE otherwise.
button_action_t button_poll(uint32_t now_ms);
// The same on a level the caller supplies (host tests, no hardware)
button_action_t button_update(bool pressed, uint32_t now_ms);

bool    button_held(void);          // debounced level
uint8_t button_hold_progress(void); // 0 until a long hold, then 1..254, 255 as the reset fires

// Queue a gesture for the next button_poll, as if pressed (console `button`)
void button_inject(button_action_t action);
