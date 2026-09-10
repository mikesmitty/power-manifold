#include <string.h>

#include "civil_time.h"
#include "led_sched.h"
#include "microtest.h"
#include "settings.h"

#define EPOCH_2026_09_04_21_40Z 1788558000u // 2026-09-04 21:40:00 UTC

static void test_window(void) {
    MT_ASSERT(!led_sched_in_window(0, 0, 500));        // none
    MT_ASSERT(!led_sched_in_window(600, 600, 600));    // equal = none
    MT_ASSERT(led_sched_in_window(600, 720, 600));     // [10:00, 12:00)
    MT_ASSERT(led_sched_in_window(600, 720, 719));
    MT_ASSERT(!led_sched_in_window(600, 720, 720));
    MT_ASSERT(!led_sched_in_window(600, 720, 599));
    MT_ASSERT(led_sched_in_window(1320, 420, 1330));   // 22:00-07:00, at 22:10
    MT_ASSERT(led_sched_in_window(1320, 420, 0));      // midnight
    MT_ASSERT(led_sched_in_window(1320, 420, 419));
    MT_ASSERT(!led_sched_in_window(1320, 420, 420));
    MT_ASSERT(!led_sched_in_window(1320, 420, 720));
}

static void test_local_time(void) {
    char t[24];
    civil_format(t, sizeof(t), EPOCH_2026_09_04_21_40Z, 0);
    MT_ASSERT(!strcmp(t, "2026-09-04 21:40"));
    civil_format(t, sizeof(t), EPOCH_2026_09_04_21_40Z, -240); // EDT
    MT_ASSERT(!strcmp(t, "2026-09-04 17:40"));
    civil_format(t, sizeof(t), EPOCH_2026_09_04_21_40Z, 180); // UTC+3 rolls the date
    MT_ASSERT(!strcmp(t, "2026-09-05 00:40"));
    MT_ASSERT_EQ(civil_local_minute(EPOCH_2026_09_04_21_40Z, 0), 21 * 60 + 40);
    MT_ASSERT_EQ(civil_local_minute(EPOCH_2026_09_04_21_40Z, -240), 17 * 60 + 40);
    MT_ASSERT_EQ(civil_local_minute(EPOCH_2026_09_04_21_40Z, 180), 40);
}

static void test_hhmm(void) {
    uint16_t m, s, e;
    char t[16];
    MT_ASSERT(hhmm_parse("22:05", &m));
    MT_ASSERT_EQ(m, 22 * 60 + 5);
    MT_ASSERT(hhmm_parse("00:00", &m));
    MT_ASSERT_EQ(m, 0);
    MT_ASSERT(!hhmm_parse("24:00", &m));
    MT_ASSERT(!hhmm_parse("7:30", &m));
    MT_ASSERT(!hhmm_parse("07:60", &m));
    MT_ASSERT(!hhmm_parse("07-30", &m));
    hhmm_format(t, sizeof(t), 7 * 60 + 5);
    MT_ASSERT(!strcmp(t, "07:05"));
    MT_ASSERT(night_parse("22:00-07:00", &s, &e));
    MT_ASSERT_EQ(s, 1320);
    MT_ASSERT_EQ(e, 420);
    MT_ASSERT(night_parse("", &s, &e));
    MT_ASSERT(s == e);
    MT_ASSERT(!night_parse("22:00", &s, &e));
    MT_ASSERT(!night_parse("22:00-7:00", &s, &e));
    night_format(t, sizeof(t), 1320, 420);
    MT_ASSERT(!strcmp(t, "22:00-07:00"));
    MT_ASSERT_EQ(night_format(t, sizeof(t), 0, 0), 0);
    MT_ASSERT_EQ(t[0], '\0');
}

static void test_modes(void) {
    settings_t s;
    memset(&s, 0, sizeof(s));
    s.led_brightness = 48;
    s.led_dim = 4;
    s.tz_offset_min = -240;   // EDT: the epoch below is 17:40 local
    s.led_night_start = 1320; // 22:00
    s.led_night_end = 420;    // 07:00
    s.led_idle_min = 30;
    uint32_t now = 1000;
    led_sched_activity(now);
    MT_ASSERT_EQ(led_sched_update(&s, EPOCH_2026_09_04_21_40Z, now), LED_MODE_NORMAL);
    MT_ASSERT_EQ(led_sched_level(&s, LED_MODE_NORMAL), 48);
    // 22:10 local: night
    MT_ASSERT_EQ(led_sched_update(&s, EPOCH_2026_09_04_21_40Z + 4 * 3600 + 30 * 60, now), LED_MODE_NIGHT);
    MT_ASSERT_EQ(led_sched_level(&s, LED_MODE_NIGHT), 4);
    MT_ASSERT_EQ(led_sched_current(), LED_MODE_NIGHT);
    // no clock yet: the window cannot apply, idle still can
    MT_ASSERT_EQ(led_sched_update(&s, 0, now), LED_MODE_NORMAL);
    now += 30 * 60000u;
    MT_ASSERT_EQ(led_sched_update(&s, 0, now), LED_MODE_IDLE);
    led_sched_activity(now); // a port event wakes it
    MT_ASSERT_EQ(led_sched_update(&s, 0, now), LED_MODE_NORMAL);
    // idle disabled
    s.led_idle_min = 0;
    now += 24 * 3600000u;
    MT_ASSERT_EQ(led_sched_update(&s, 0, now), LED_MODE_NORMAL);
    // night wins over idle for the reported mode
    s.led_idle_min = 1;
    MT_ASSERT_EQ(led_sched_update(&s, EPOCH_2026_09_04_21_40Z + 6 * 3600, now), LED_MODE_NIGHT);
    MT_ASSERT(!strcmp(led_mode_name(LED_MODE_IDLE), "idle"));
}

static void test_wake(void) {
    settings_t s;
    memset(&s, 0, sizeof(s));
    s.led_brightness = 48;
    s.led_dim = 4;
    s.tz_offset_min = -240;
    s.led_night_start = 1320; // 22:00-07:00
    s.led_night_end = 420;
    s.led_idle_min = 30;
    uint32_t night = EPOCH_2026_09_04_21_40Z + 4 * 3600 + 30 * 60; // 22:10 local
    uint32_t now = 1000;
    led_sched_activity(now);
    MT_ASSERT_EQ(led_sched_update(&s, night, now), LED_MODE_NIGHT);
    // a tap outranks the night window for LED_WAKE_MS, then it is back
    led_sched_wake(now);
    MT_ASSERT_EQ(led_sched_update(&s, night, now), LED_MODE_NORMAL);
    MT_ASSERT_EQ(led_sched_update(&s, night, now + LED_WAKE_MS - 1), LED_MODE_NORMAL);
    MT_ASSERT_EQ(led_sched_update(&s, night, now + LED_WAKE_MS), LED_MODE_NIGHT);
    // and over idle dimming, where it also restarts the idle clock
    now += 40 * 60000u;
    MT_ASSERT_EQ(led_sched_update(&s, 0, now), LED_MODE_IDLE);
    led_sched_wake(now);
    MT_ASSERT_EQ(led_sched_update(&s, 0, now), LED_MODE_NORMAL);
    now += LED_WAKE_MS;
    MT_ASSERT_EQ(led_sched_update(&s, 0, now), LED_MODE_NORMAL); // not idle again for 30 min
    now += 30 * 60000u;
    MT_ASSERT_EQ(led_sched_update(&s, 0, now), LED_MODE_IDLE);
}

void run_led_sched_tests(void) {
    mt_run("led schedule: night window, wrapping midnight", test_window);
    mt_run("led schedule: local time from UTC + offset", test_local_time);
    mt_run("led schedule: HH:MM and HH:MM-HH:MM text", test_hhmm);
    mt_run("led schedule: night, idle, activity, no clock", test_modes);
    mt_run("led schedule: a tap wakes it from night and idle", test_wake);
}
