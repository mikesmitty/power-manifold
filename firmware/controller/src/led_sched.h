#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "settings.h"

// LED brightness schedule, core 0. The base brightness (settings
// led_brightness) gives way to led_dim during the night window — local
// time is SNTP's UTC plus tz_offset_min, and until the clock is known the
// window is ignored — and after led_idle_min minutes without a port event.
// Hardware-free: main.c calls led_sched_update every pass and pushes the
// level to the engine when it changes.

typedef enum { LED_MODE_NORMAL, LED_MODE_NIGHT, LED_MODE_IDLE } led_mode_t;

// A front-panel tap brings full brightness back for this long, night
// window or not; then the schedule has its say again.
#define LED_WAKE_MS (30 * 1000)

void       led_sched_activity(uint32_t now_ms); // a port event: idle timer restarts
void       led_sched_wake(uint32_t now_ms);     // someone is at the box: normal for LED_WAKE_MS
led_mode_t led_sched_update(const settings_t *s, uint32_t epoch, uint32_t now_ms);
led_mode_t led_sched_current(void);             // the last update's verdict
uint8_t    led_sched_level(const settings_t *s, led_mode_t mode);
const char *led_mode_name(led_mode_t m);        // "normal" / "night" / "idle"

// [start, end) in minutes after midnight, wrapping past it; start == end = never
bool led_sched_in_window(uint16_t start, uint16_t end, uint16_t minute);

// "HH:MM" <-> minutes, and the window as "HH:MM-HH:MM" ("" = none)
bool   hhmm_parse(const char *s, uint16_t *minutes);
size_t hhmm_format(char *out, size_t cap, uint16_t minutes);
bool   night_parse(const char *s, uint16_t *start, uint16_t *end);
size_t night_format(char *out, size_t cap, uint16_t start, uint16_t end);
