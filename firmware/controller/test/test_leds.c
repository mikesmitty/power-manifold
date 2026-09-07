#include <string.h>

#include "led_pattern.h"
#include "manifold.h"
#include "microtest.h"

// The colour policy for the backplane chain, rendered on the host. Pixels
// are compared as channel values at brightness 255 unless a test says so.

static telemetry_t tele;
static led_view_t view;
static led_rgb_t px[NUM_PORTS];

static void reset(uint8_t brightness) {
    memset(&tele, 0, sizeof(tele));
    led_view_init(&view, brightness, LED_BOOT_WHITE);
}

static void set_port(int i, port_state_t s, uint16_t mv) {
    tele.port[i].state = (uint8_t)s;
    tele.port[i].bus_mv = mv;
}

static void render(uint32_t now_ms) {
    led_pattern_render(&view, &tele, now_ms, px);
}

static bool dark(led_rgb_t c) { return !c.r && !c.g && !c.b; }
static bool same(led_rgb_t a, led_rgb_t b) { return a.r == b.r && a.g == b.g && a.b == b.b; }

static void test_port_state_colours(void) {
    reset(255);
    set_port(0, PORT_STATE_ABSENT, 0);
    set_port(1, PORT_STATE_IDLE, 0);
    set_port(2, PORT_STATE_ACTIVE, 5000);
    set_port(3, PORT_STATE_ACTIVE, 20000);
    set_port(4, PORT_STATE_DISABLED, 0);
    set_port(5, PORT_STATE_FAULT, 0);
    render(0);
    MT_ASSERT(px[0].r == px[0].g && px[0].g == px[0].b); // dim white
    MT_ASSERT(px[0].r > 0 && px[0].r < 16);
    MT_ASSERT_EQ(px[1].r, 255); // amber
    MT_ASSERT_EQ(px[1].g, 120);
    MT_ASSERT_EQ(px[1].b, 0);
    MT_ASSERT(px[2].b == 255 && px[2].r == 0); // blue below 19 V
    MT_ASSERT(px[3].g == 220 && px[3].r == 0 && px[3].b < px[3].g); // green at 20 V
    MT_ASSERT(dark(px[4]));
    MT_ASSERT(px[5].r == 255 && px[5].g == 0 && px[5].b == 0); // fault, blink on
    render(100); // 5 Hz: off in the second half of each 200 ms
    MT_ASSERT(dark(px[5]));
    MT_ASSERT_EQ(px[1].r, 255); // solid colours unaffected
}

static void test_probe_blinks_cyan(void) {
    reset(255);
    set_port(0, PORT_STATE_PROBE, 0);
    render(0);
    MT_ASSERT(px[0].r == 0 && px[0].g == 180 && px[0].b == 180);
    render(250); // 2 Hz
    MT_ASSERT(dark(px[0]));
    render(500);
    MT_ASSERT_EQ(px[0].g, 180);
}

static void test_throttled_pulses_active_colour(void) {
    reset(255);
    set_port(0, PORT_STATE_THROTTLED, 20000);
    set_port(1, PORT_STATE_THROTTLED, 9000);
    render(0); // pulse trough: dim but never dark
    MT_ASSERT(!dark(px[0]));
    MT_ASSERT(px[0].r == 0 && px[0].g > px[0].b); // green hue
    MT_ASSERT(px[1].r == 0 && px[1].b > px[1].g); // blue hue
    uint8_t trough = px[0].g;
    render(500); // pulse peak = the solid active colour
    MT_ASSERT_EQ(px[0].g, 220);
    MT_ASSERT(px[0].g > trough);
    render(1000); // back to the trough
    MT_ASSERT_EQ(px[0].g, trough);
}

static void test_charged_shows_magenta(void) {
    reset(255);
    set_port(0, PORT_STATE_ACTIVE, 20000);
    set_port(1, PORT_STATE_THROTTLED, 9000);
    set_port(2, PORT_STATE_ACTIVE, 5000);
    tele.port[0].charged = tele.port[1].charged = true;
    render(0);
    MT_ASSERT(px[0].r == 255 && px[0].g == 0 && px[0].b == 200); // not the 20 V green
    MT_ASSERT(same(px[1], px[0]));                                // and no throttle pulse
    MT_ASSERT(px[2].b == 255 && px[2].r == 0);                    // still charging: blue
    render(500);
    MT_ASSERT(same(px[1], px[0])); // solid
    reset(0); // master brightness 0 hides it like every non-fault colour
    set_port(0, PORT_STATE_ACTIVE, 20000);
    tele.port[0].charged = true;
    render(0);
    MT_ASSERT(dark(px[0]));
}

static void test_master_brightness_scales(void) {
    reset(51); // 20%
    set_port(0, PORT_STATE_IDLE, 0);
    render(0);
    MT_ASSERT_EQ(px[0].r, 51);
    MT_ASSERT_EQ(px[0].g, 24); // 120 * 51 / 255
    MT_ASSERT_EQ(px[0].b, 0);
}

static void test_brightness_zero_keeps_faults(void) {
    reset(0);
    set_port(0, PORT_STATE_IDLE, 0);
    set_port(1, PORT_STATE_ACTIVE, 20000);
    set_port(2, PORT_STATE_FAULT, 0);
    render(0);
    MT_ASSERT(dark(px[0]));
    MT_ASSERT(dark(px[1]));
    MT_ASSERT_EQ(px[2].r, LED_FAULT_FLOOR);
    MT_ASSERT(px[2].g == 0 && px[2].b == 0);
    render(100);
    MT_ASSERT(dark(px[2])); // still blinking
}

static void all_idle(void) {
    for (int i = 0; i < NUM_PORTS; i++) set_port(i, PORT_STATE_IDLE, 0);
}

static void test_boot_sweep_white(void) {
    reset(255);
    all_idle();
    view.boot_pending = true;
    view.boot_start_ms = 1000;
    render(1000);
    MT_ASSERT(px[0].r == 255 && px[0].g == 255 && px[0].b == 255);
    for (int i = 1; i < NUM_PORTS; i++) MT_ASSERT(dark(px[i]));
    render(1000 + 2 * LED_BOOT_STEP_MS - 1); // pixels 0 and 1 reached
    MT_ASSERT(!dark(px[1]));
    MT_ASSERT(dark(px[2]));
    render(1000 + (NUM_PORTS - 1) * LED_BOOT_STEP_MS); // all lit, then held
    for (int i = 0; i < NUM_PORTS; i++) MT_ASSERT_EQ(px[i].r, 255);
    MT_ASSERT(view.boot_pending);
    render(1000 + LED_BOOT_SWEEP_MS); // over: ports show through
    MT_ASSERT(!view.boot_pending);
    MT_ASSERT(px[0].r == 255 && px[0].g == 120 && px[0].b == 0);
}

static void test_boot_sweep_rainbow(void) {
    reset(255);
    all_idle();
    view.boot_style = LED_BOOT_RAINBOW;
    view.boot_pending = true;
    view.boot_start_ms = 0;
    render((NUM_PORTS - 1) * LED_BOOT_STEP_MS);
    MT_ASSERT(px[0].r == 255 && px[0].g == 0); // red first
    for (int i = 0; i < NUM_PORTS; i++) {
        MT_ASSERT(!dark(px[i]));
        for (int j = 0; j < i; j++) MT_ASSERT(!same(px[i], px[j]));
    }
}

static void test_boot_sweep_dark_at_zero_brightness(void) {
    reset(0);
    set_port(0, PORT_STATE_FAULT, 0);
    view.boot_pending = true;
    view.boot_start_ms = 0;
    render(LED_BOOT_STEP_MS * 3);
    for (int i = 0; i < NUM_PORTS; i++) MT_ASSERT(dark(px[i]));
    render(LED_BOOT_SWEEP_MS); // sweep over: the fault floor returns
    MT_ASSERT_EQ(px[0].r, LED_FAULT_FLOOR);
}

static void test_ble_comet_crosses_the_chain(void) {
    reset(255);
    all_idle();
    view.chassis = LED_CHASSIS_BLE_OPEN;
    render(0);
    MT_ASSERT(px[0].r == 0 && px[0].g == 60 && px[0].b == 255); // head
    MT_ASSERT_EQ(px[1].r, 255);                                   // amber beneath
    render(LED_COMET_STEP_MS);
    MT_ASSERT_EQ(px[1].b, 255);                                   // head moved on
    MT_ASSERT(px[0].b == 85 && px[0].g == 20 && px[0].r == 0);    // tail at 1/3
    MT_ASSERT_EQ(px[2].r, 255);
    render(NUM_PORTS * LED_COMET_STEP_MS); // pass finished: nothing overlaid
    for (int i = 0; i < NUM_PORTS; i++) MT_ASSERT_EQ(px[i].r, 255);
    render(LED_COMET_PERIOD_MS); // next pass
    MT_ASSERT_EQ(px[0].b, 255);
}

static void test_net_down_comet_is_white_and_ble_wins(void) {
    reset(255);
    all_idle();
    view.chassis = LED_CHASSIS_NET_DOWN;
    render(0);
    MT_ASSERT(px[0].r == 255 && px[0].g == 255 && px[0].b == 255);
    view.chassis = LED_CHASSIS_NET_DOWN | LED_CHASSIS_BLE_OPEN;
    render(0);
    MT_ASSERT(px[0].r == 0 && px[0].b == 255);
    view.chassis = 0;
    render(0);
    MT_ASSERT(px[0].r == 255 && px[0].g == 120);
}

static void test_comet_hidden_at_zero_brightness(void) {
    reset(0);
    all_idle();
    view.chassis = LED_CHASSIS_BLE_OPEN;
    render(0);
    for (int i = 0; i < NUM_PORTS; i++) MT_ASSERT(dark(px[i]));
}

static void test_identify_overrides_everything(void) {
    reset(255);
    all_idle();
    view.chassis = LED_CHASSIS_BLE_OPEN;
    view.boot_pending = true;
    view.boot_start_ms = 0;
    view.identify_pending = true;
    view.identify_until_ms = 5000;
    render(0); // odd pixels lit in blue, even dark
    MT_ASSERT(dark(px[0]));
    MT_ASSERT(px[1].b == 255 && px[1].r == 0);
    render(250); // halves swap at 2 Hz
    MT_ASSERT_EQ(px[0].b, 255);
    MT_ASSERT(dark(px[1]));
    render(5000); // expired: boot has long finished, ports show
    MT_ASSERT(!view.identify_pending);
    MT_ASSERT(!view.boot_pending);
    MT_ASSERT(px[0].r == 255 && px[0].g == 120);
}

static void test_identify_visible_when_dimmed_to_zero(void) {
    reset(0);
    all_idle();
    view.identify_pending = true;
    view.identify_until_ms = 1000;
    render(0);
    MT_ASSERT_EQ(px[1].b, LED_IDENTIFY_FLOOR);
}

void run_led_tests(void) {
    mt_run("leds: port state colours", test_port_state_colours);
    mt_run("leds: probe blinks cyan", test_probe_blinks_cyan);
    mt_run("leds: throttled pulses the active colour", test_throttled_pulses_active_colour);
    mt_run("leds: a charged sink shows magenta", test_charged_shows_magenta);
    mt_run("leds: master brightness scales", test_master_brightness_scales);
    mt_run("leds: brightness 0 keeps faults", test_brightness_zero_keeps_faults);
    mt_run("leds: boot sweep white", test_boot_sweep_white);
    mt_run("leds: boot sweep rainbow", test_boot_sweep_rainbow);
    mt_run("leds: boot sweep dark at brightness 0", test_boot_sweep_dark_at_zero_brightness);
    mt_run("leds: BLE comet crosses the chain", test_ble_comet_crosses_the_chain);
    mt_run("leds: net-down comet white, BLE wins", test_net_down_comet_is_white_and_ble_wins);
    mt_run("leds: comet hidden at brightness 0", test_comet_hidden_at_zero_brightness);
    mt_run("leds: identify overrides everything", test_identify_overrides_everything);
    mt_run("leds: identify visible at brightness 0", test_identify_visible_when_dimmed_to_zero);
}
