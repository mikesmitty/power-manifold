#include "led_sched.h"

#include <stdio.h>
#include <string.h>

#include "civil_time.h"

static uint32_t last_activity_ms; // boot counts as activity
static led_mode_t current;

void led_sched_activity(uint32_t now_ms) {
    last_activity_ms = now_ms;
}

bool led_sched_in_window(uint16_t start, uint16_t end, uint16_t minute) {
    if (start == end) return false;
    if (start < end) return minute >= start && minute < end;
    return minute >= start || minute < end; // wraps past midnight
}

led_mode_t led_sched_update(const settings_t *s, uint32_t epoch, uint32_t now_ms) {
    led_mode_t m = LED_MODE_NORMAL;
    if (epoch && led_sched_in_window(s->led_night_start, s->led_night_end,
                                     civil_local_minute(epoch, s->tz_offset_min)))
        m = LED_MODE_NIGHT;
    else if (s->led_idle_min && now_ms - last_activity_ms >= (uint32_t)s->led_idle_min * 60000u)
        m = LED_MODE_IDLE;
    current = m;
    return m;
}

led_mode_t led_sched_current(void) {
    return current;
}

uint8_t led_sched_level(const settings_t *s, led_mode_t mode) {
    return mode == LED_MODE_NORMAL ? s->led_brightness : s->led_dim;
}

const char *led_mode_name(led_mode_t m) {
    switch (m) {
    case LED_MODE_NIGHT: return "night";
    case LED_MODE_IDLE:  return "idle";
    default:             return "normal";
    }
}

bool hhmm_parse(const char *s, uint16_t *minutes) {
    if (strlen(s) != 5 || s[2] != ':') return false;
    for (int i = 0; i < 5; i++)
        if (i != 2 && (s[i] < '0' || s[i] > '9')) return false;
    unsigned h = (unsigned)(s[0] - '0') * 10 + (unsigned)(s[1] - '0');
    unsigned m = (unsigned)(s[3] - '0') * 10 + (unsigned)(s[4] - '0');
    if (h > 23 || m > 59) return false;
    *minutes = (uint16_t)(h * 60 + m);
    return true;
}

size_t hhmm_format(char *out, size_t cap, uint16_t minutes) {
    int n = snprintf(out, cap, "%02u:%02u", minutes / 60 % 24, minutes % 60);
    return n < 0 ? 0 : (size_t)n < cap ? (size_t)n : cap - 1;
}

bool night_parse(const char *s, uint16_t *start, uint16_t *end) {
    if (!s[0]) { // none
        *start = *end = 0;
        return true;
    }
    if (strlen(s) != 11 || s[5] != '-') return false;
    char a[6], b[6];
    memcpy(a, s, 5); a[5] = '\0';
    memcpy(b, s + 6, 5); b[5] = '\0';
    return hhmm_parse(a, start) && hhmm_parse(b, end);
}

size_t night_format(char *out, size_t cap, uint16_t start, uint16_t end) {
    if (!cap) return 0;
    if (start == end) {
        out[0] = '\0';
        return 0;
    }
    char a[6], b[6];
    hhmm_format(a, sizeof(a), start);
    hhmm_format(b, sizeof(b), end);
    int n = snprintf(out, cap, "%s-%s", a, b);
    return n < 0 ? 0 : (size_t)n < cap ? (size_t)n : cap - 1;
}
