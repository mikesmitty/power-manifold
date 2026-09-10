#include "button.h"

#include "button_hw.h"

static bool fitted;
static bool raw_last, stable;   // last sampled level, debounced level (true = pressed)
static uint32_t raw_since_ms;   // the level has read raw_last since
static uint32_t press_ms;       // when the current press started (raw)
static bool fired;              // BUTTON_VERY_LONG went off during this press
static uint8_t progress;
static button_action_t injected;

void button_init(void) {
    fitted = button_hw_init();
    raw_last = stable = false;
    raw_since_ms = press_ms = 0;
    fired = false;
    progress = 0;
    injected = BUTTON_NONE;
}

bool button_fitted(void) { return fitted; }
bool button_held(void) { return stable; }
uint8_t button_hold_progress(void) { return progress; }

void button_inject(button_action_t action) { injected = action; }

button_action_t button_update(bool pressed, uint32_t now_ms) {
    if (injected != BUTTON_NONE) {
        button_action_t a = injected;
        injected = BUTTON_NONE;
        return a;
    }
    if (pressed != raw_last) {
        raw_last = pressed;
        raw_since_ms = now_ms;
    }
    if (pressed != stable) {
        if (now_ms - raw_since_ms < BUTTON_DEBOUNCE_MS) return BUTTON_NONE; // still bouncing
        stable = pressed;
        if (stable) {
            press_ms = raw_since_ms; // the physical press, not the debounce point
            fired = false;
            progress = 0;
            return BUTTON_NONE;
        }
        progress = 0;
        if (fired) return BUTTON_NONE; // the reset already went off
        return now_ms - press_ms >= BUTTON_LONG_MS ? BUTTON_LONG : BUTTON_SHORT;
    }
    if (stable && !fired) {
        uint32_t held = now_ms - press_ms;
        if (held >= BUTTON_VERY_LONG_MS) {
            fired = true;
            progress = 255;
            return BUTTON_VERY_LONG;
        }
        progress = held < BUTTON_LONG_MS
            ? 0
            : (uint8_t)(1 + ((held - BUTTON_LONG_MS) * 253u) / (BUTTON_VERY_LONG_MS - BUTTON_LONG_MS));
    }
    return BUTTON_NONE;
}

button_action_t button_poll(uint32_t now_ms) {
    if (!fitted) {
        button_action_t a = injected;
        injected = BUTTON_NONE;
        return a;
    }
    return button_update(button_hw_pressed(), now_ms);
}
