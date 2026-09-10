#include "button.h"
#include "button_hw.h"
#include "microtest.h"

// The front-panel button's gestures against a scripted level: debounce,
// short and long presses, the hold-to-reset with its progress, and a board
// without a button.

static bool emu_fitted, emu_level;
bool button_hw_init(void) { return emu_fitted; }
bool button_hw_pressed(void) { return emu_level; }

static uint32_t now;
static int n_short, n_long, n_very_long;

static void start(bool fitted) {
    emu_fitted = fitted;
    emu_level = false;
    now = 0;
    n_short = n_long = n_very_long = 0;
    button_init();
}

// the main loop's cadence: a poll every 2 ms, counting what fires
static void run_ms(uint32_t ms) {
    for (uint32_t t = 0; t < ms; t += 2) {
        now += 2;
        switch (button_poll(now)) {
        case BUTTON_SHORT: n_short++; break;
        case BUTTON_LONG: n_long++; break;
        case BUTTON_VERY_LONG: n_very_long++; break;
        default: break;
        }
    }
}

static void press(void) { emu_level = true; }
static void release(void) { emu_level = false; }

static void test_not_fitted(void) {
    start(false);
    MT_ASSERT(!button_fitted());
    press();
    run_ms(20000);
    MT_ASSERT_EQ(n_short + n_long + n_very_long, 0);
    MT_ASSERT_EQ(button_hold_progress(), 0);
    button_inject(BUTTON_SHORT); // the console can still exercise the handlers
    run_ms(2);
    MT_ASSERT_EQ(n_short, 1);
}

static void test_short_press(void) {
    start(true);
    press();
    run_ms(200);
    MT_ASSERT(button_held());
    MT_ASSERT_EQ(n_short, 0); // nothing until release
    release();
    run_ms(20);
    MT_ASSERT_EQ(n_short, 0); // debouncing the release
    run_ms(20);
    MT_ASSERT_EQ(n_short, 1);
    MT_ASSERT(!button_held());
    run_ms(1000);
    MT_ASSERT_EQ(n_short, 1);
    MT_ASSERT_EQ(n_long + n_very_long, 0);
}

static void test_bounce_ignored(void) {
    start(true);
    for (int i = 0; i < 5; i++) { // 10 ms glitches
        press();
        run_ms(10);
        release();
        run_ms(10);
    }
    run_ms(200);
    MT_ASSERT_EQ(n_short + n_long + n_very_long, 0);
    MT_ASSERT(!button_held());
    // a bouncy press still counts once it settles
    press();
    run_ms(10);
    release();
    run_ms(4);
    press();
    run_ms(100);
    MT_ASSERT(button_held());
    release();
    run_ms(50);
    MT_ASSERT_EQ(n_short, 1);
}

static void test_long_press(void) {
    start(true);
    press();
    run_ms(4000);
    MT_ASSERT_EQ(n_long, 0); // fires on release, not at the mark
    release();
    run_ms(50);
    MT_ASSERT_EQ(n_long, 1);
    MT_ASSERT_EQ(n_short, 0);
    MT_ASSERT_EQ(button_hold_progress(), 0);
}

static void test_hold_progress(void) {
    start(true);
    press();
    run_ms(2900);
    MT_ASSERT_EQ(button_hold_progress(), 0);
    run_ms(200); // t = 3.1 s: just past the long-press point
    MT_ASSERT(button_hold_progress() >= 1 && button_hold_progress() < 10);
    run_ms(3400); // t = 6.5 s: halfway to the reset
    MT_ASSERT(button_hold_progress() > 120 && button_hold_progress() < 136);
    run_ms(3400); // t = 9.9 s
    MT_ASSERT(button_hold_progress() > 245 && button_hold_progress() < 255);
    MT_ASSERT_EQ(n_very_long, 0);
    release();
    run_ms(50);
    MT_ASSERT_EQ(n_long, 1);
    MT_ASSERT_EQ(button_hold_progress(), 0);
}

static void test_very_long_press(void) {
    start(true);
    press();
    run_ms(9990);
    MT_ASSERT_EQ(n_very_long, 0);
    run_ms(20);
    MT_ASSERT_EQ(n_very_long, 1); // fires while still held
    MT_ASSERT_EQ(button_hold_progress(), 255);
    run_ms(5000);
    MT_ASSERT_EQ(n_very_long, 1); // once
    release();
    run_ms(50);
    MT_ASSERT_EQ(n_long + n_short, 0); // the release after a reset is nothing
    MT_ASSERT_EQ(button_hold_progress(), 0);
    // and the next press starts over
    press();
    run_ms(100);
    release();
    run_ms(50);
    MT_ASSERT_EQ(n_short, 1);
}

void run_button_tests(void) {
    mt_run("button: not fitted", test_not_fitted);
    mt_run("button: short press on release", test_short_press);
    mt_run("button: bounces are ignored", test_bounce_ignored);
    mt_run("button: long press on release", test_long_press);
    mt_run("button: hold progress toward the reset", test_hold_progress);
    mt_run("button: hold to the end fires once, while held", test_very_long_press);
}
