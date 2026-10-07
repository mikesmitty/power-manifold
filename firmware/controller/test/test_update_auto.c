#include <string.h>

#include "microtest.h"
#include "update_auto.h"

#define DAY_MS  (24u * 60u * 60u * 1000u)
#define MIDNIGHT 1788480000u // 00:00 UTC
#define AT(h, m) (MIDNIGHT + (h) * 3600u + (m) * 60u)

// A release with a two-day wait, known for three days, the clock set, 03:30 UTC, nothing tried yet.
static update_auto_in_t due(void) {
    update_auto_in_t in = {
        .enabled = true, .latest = "0.17.0", .skip = "",
        .now_ms = 3 * DAY_MS + 5000, .seen_ms = 5000, .wait_days = 2, .last_try_ms = 0,
        .epoch = AT(3, 30), .tz_offset_min = 0, .postpone_until = 0,
    };
    return in;
}

static void test_installs_in_the_night_window(void) {
    update_auto_in_t in = due();
    MT_ASSERT(update_auto_decide(&in) == UPDATE_AUTO_INSTALL);
    in.epoch = AT(3, 0);
    MT_ASSERT(update_auto_decide(&in) == UPDATE_AUTO_INSTALL);
    in.epoch = AT(4, 59);
    MT_ASSERT(update_auto_decide(&in) == UPDATE_AUTO_INSTALL);
    in.epoch = AT(5, 0);
    MT_ASSERT(update_auto_decide(&in) == UPDATE_AUTO_WINDOW);
    in.epoch = AT(2, 59);
    MT_ASSERT(update_auto_decide(&in) == UPDATE_AUTO_WINDOW);
    in.epoch = AT(14, 0);
    MT_ASSERT(update_auto_decide(&in) == UPDATE_AUTO_WINDOW);
}

// The window is local time: 03:30 at UTC-4 is 07:30 UTC.
static void test_window_is_local_time(void) {
    update_auto_in_t in = due();
    in.tz_offset_min = -240;
    MT_ASSERT(update_auto_decide(&in) == UPDATE_AUTO_WINDOW);
    in.epoch = AT(7, 30);
    MT_ASSERT(update_auto_decide(&in) == UPDATE_AUTO_INSTALL);
}

static void test_waits_its_days_after_a_release_appears(void) {
    update_auto_in_t in = due();
    in.seen_ms = in.now_ms - 2 * DAY_MS + 1;
    MT_ASSERT(update_auto_decide(&in) == UPDATE_AUTO_HOLD);
    in.seen_ms = in.now_ms - 2 * DAY_MS;
    MT_ASSERT(update_auto_decide(&in) == UPDATE_AUTO_INSTALL);
    in.wait_days = 7;
    MT_ASSERT(update_auto_decide(&in) == UPDATE_AUTO_HOLD);
    in.seen_ms = in.now_ms - 7 * DAY_MS;
    MT_ASSERT(update_auto_decide(&in) == UPDATE_AUTO_INSTALL);
}

static void test_wait_days_range(void) {
    unsigned seen = 0;
    for (uint32_t r = 0; r < 1000; r++) {
        uint8_t d = update_auto_wait_days(r * 2654435761u);
        MT_ASSERT(d >= 1 && d <= 7);
        seen |= 1u << d;
    }
    MT_ASSERT(seen == 0xFEu); // every day from 1 to 7 comes up
    MT_ASSERT(update_auto_wait_days(0xFFFFFFFFu) >= 1);
}

// Without a clock there is no window: the release installs once its wait is over.
static void test_no_clock_installs_after_the_hold(void) {
    update_auto_in_t in = due();
    in.epoch = 0;
    MT_ASSERT(update_auto_decide(&in) == UPDATE_AUTO_INSTALL);
    in.seen_ms = in.now_ms - 1000;
    MT_ASSERT(update_auto_decide(&in) == UPDATE_AUTO_HOLD);
}

static void test_off_none_and_skipped(void) {
    update_auto_in_t in = due();
    in.enabled = false;
    MT_ASSERT(update_auto_decide(&in) == UPDATE_AUTO_OFF);
    in = due();
    in.latest = "";
    MT_ASSERT(update_auto_decide(&in) == UPDATE_AUTO_NONE);
    in = due();
    in.skip = "0.17.0";
    MT_ASSERT(update_auto_decide(&in) == UPDATE_AUTO_SKIPPED);
    // a skip names one version; the next release installs
    in.latest = "0.17.1";
    MT_ASSERT(update_auto_decide(&in) == UPDATE_AUTO_INSTALL);
}

static void test_postponed(void) {
    update_auto_in_t in = due();
    in.postpone_until = in.epoch + 1;
    MT_ASSERT(update_auto_decide(&in) == UPDATE_AUTO_POSTPONED);
    in.postpone_until = in.epoch;
    MT_ASSERT(update_auto_decide(&in) == UPDATE_AUTO_INSTALL);
    // after a restart, until the clock is set again
    in.postpone_until = in.epoch + 1;
    in.epoch = 0;
    MT_ASSERT(update_auto_decide(&in) == UPDATE_AUTO_POSTPONED);
}

static void test_failed_attempt_waits_for_the_next_night(void) {
    update_auto_in_t in = due();
    in.last_try_ms = in.now_ms - 60000;
    MT_ASSERT(update_auto_decide(&in) == UPDATE_AUTO_RETRY);
    in.last_try_ms = in.now_ms - UPDATE_AUTO_RETRY_MS;
    MT_ASSERT(update_auto_decide(&in) == UPDATE_AUTO_INSTALL);
}

static void test_names(void) {
    MT_ASSERT(!strcmp(update_auto_name(UPDATE_AUTO_WINDOW), "scheduled"));
    MT_ASSERT(!strcmp(update_auto_name(UPDATE_AUTO_HOLD), "waiting"));
    MT_ASSERT(!strcmp(update_auto_name(UPDATE_AUTO_INSTALL), "installing"));
}

void run_update_auto_tests(void) {
    mt_run("update_auto: installs between 03:00 and 05:00", test_installs_in_the_night_window);
    mt_run("update_auto: the window is local time", test_window_is_local_time);
    mt_run("update_auto: waits its days after a release appears", test_waits_its_days_after_a_release_appears);
    mt_run("update_auto: the wait is 1 to 7 days", test_wait_days_range);
    mt_run("update_auto: no clock, installs after the wait", test_no_clock_installs_after_the_hold);
    mt_run("update_auto: off, nothing newer, skipped version", test_off_none_and_skipped);
    mt_run("update_auto: postponed", test_postponed);
    mt_run("update_auto: a failed attempt waits for the next night", test_failed_attempt_waits_for_the_next_night);
    mt_run("update_auto: state names", test_names);
}
