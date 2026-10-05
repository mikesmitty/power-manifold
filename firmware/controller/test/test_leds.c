#include <string.h>

#include "led_pattern.h"
#include "manifold.h"
#include "microtest.h"

// The colour policy for the light bar, rendered on the host. Pixels are
// compared as channel values at brightness 255 unless a test says so. px[]
// is the chain as wired; PX(i) is port index i's pixel, CH the chassis light.

static telemetry_t tele;
static led_view_t view;
static led_rgb_t px[LED_PIXELS];

#define PX(i) px[LED_PORT_PIXEL(i)]
#define CH    px[LED_CHASSIS_PIXEL]

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

// The chassis light's all-clear: a dim green glow
static bool all_clear(led_rgb_t c) { return c.r == 0 && c.g > c.b && c.g < 40; }

static void test_port_state_colours(void) {
    reset(255);
    set_port(0, PORT_STATE_ABSENT, 0);
    set_port(1, PORT_STATE_IDLE, 0);
    set_port(2, PORT_STATE_ACTIVE, 5000);
    set_port(3, PORT_STATE_ACTIVE, 20000);
    set_port(4, PORT_STATE_DISABLED, 0);
    set_port(5, PORT_STATE_FAULT, 0);
    render(0);
    MT_ASSERT(PX(0).r == PX(0).g && PX(0).g == PX(0).b); // dim white
    MT_ASSERT(PX(0).r > 0 && PX(0).r < 16);
    MT_ASSERT_EQ(PX(1).r, 255); // amber
    MT_ASSERT_EQ(PX(1).g, 120);
    MT_ASSERT_EQ(PX(1).b, 0);
    MT_ASSERT(PX(2).b == 255 && PX(2).r == 0); // blue below 19 V
    MT_ASSERT(PX(3).g == 220 && PX(3).r == 0 && PX(3).b < PX(3).g); // green at 20 V
    MT_ASSERT(dark(PX(4)));
    MT_ASSERT(PX(5).r == 255 && PX(5).g == 0 && PX(5).b == 0); // fault, blink on
    render(100); // 5 Hz: off in the second half of each 200 ms
    MT_ASSERT(dark(PX(5)));
    MT_ASSERT_EQ(PX(1).r, 255); // solid colours unaffected
}

static void test_hold_fill(void) {
    reset(255);
    set_port(0, PORT_STATE_ACTIVE, 20000);
    set_port(5, PORT_STATE_FAULT, 0);
    view.hold = 110; // three pixels, the fourth just starting
    render(0);
    MT_ASSERT(CH.b == 255 && CH.r == 0); // blue, full, from the chassis light
    MT_ASSERT(px[2].b == 255);
    MT_ASSERT(px[3].b > 0 && px[3].b < 16);
    MT_ASSERT(dark(px[4]));
    MT_ASSERT(dark(PX(5))); // the fault does not show through
    view.hold = 255;
    render(100);
    for (int i = 0; i < LED_PIXELS; i++) MT_ASSERT(px[i].r == 255 && px[i].g == 0 && px[i].b == 0);
    view.hold = 0;
    render(0);
    MT_ASSERT(PX(0).g == 220); // ports back
    MT_ASSERT(all_clear(CH));
    // shown on a blanked chain, at the identify floor
    view.brightness = 0;
    view.hold = 254;
    render(0);
    MT_ASSERT_EQ(CH.b, 255 * LED_IDENTIFY_FLOOR / 255);
}

static void test_probe_blinks_cyan(void) {
    reset(255);
    set_port(0, PORT_STATE_PROBE, 0);
    render(0);
    MT_ASSERT(PX(0).r == 0 && PX(0).g == 180 && PX(0).b == 180);
    render(250); // 2 Hz
    MT_ASSERT(dark(PX(0)));
    render(500);
    MT_ASSERT_EQ(PX(0).g, 180);
}

static void test_throttled_pulses_active_colour(void) {
    reset(255);
    set_port(0, PORT_STATE_THROTTLED, 20000);
    set_port(1, PORT_STATE_THROTTLED, 9000);
    render(0); // pulse trough: dim but never dark
    MT_ASSERT(!dark(PX(0)));
    MT_ASSERT(PX(0).r == 0 && PX(0).g > PX(0).b); // green hue
    MT_ASSERT(PX(1).r == 0 && PX(1).b > PX(1).g); // blue hue
    uint8_t trough = PX(0).g;
    render(500); // pulse peak = the solid active colour
    MT_ASSERT_EQ(PX(0).g, 220);
    MT_ASSERT(PX(0).g > trough);
    render(1000); // back to the trough
    MT_ASSERT_EQ(PX(0).g, trough);
}

static void test_charged_shows_magenta(void) {
    reset(255);
    set_port(0, PORT_STATE_ACTIVE, 20000);
    set_port(1, PORT_STATE_THROTTLED, 9000);
    set_port(2, PORT_STATE_ACTIVE, 5000);
    tele.port[0].charged = tele.port[1].charged = true;
    render(0);
    MT_ASSERT(PX(0).r == 255 && PX(0).g == 0 && PX(0).b == 200); // not the 20 V green
    MT_ASSERT(same(PX(1), PX(0)));                                // and no throttle pulse
    MT_ASSERT(PX(2).b == 255 && PX(2).r == 0);                    // still charging: blue
    render(500);
    MT_ASSERT(same(PX(1), PX(0))); // solid
    reset(0); // master brightness 0 hides it like every non-fault colour
    set_port(0, PORT_STATE_ACTIVE, 20000);
    tele.port[0].charged = true;
    render(0);
    MT_ASSERT(dark(PX(0)));
}

static void test_master_brightness_scales(void) {
    reset(51); // 20%
    set_port(0, PORT_STATE_IDLE, 0);
    render(0);
    MT_ASSERT_EQ(PX(0).r, 51);
    MT_ASSERT_EQ(PX(0).g, 24); // 120 * 51 / 255
    MT_ASSERT_EQ(PX(0).b, 0);
}

static void test_brightness_zero_keeps_faults(void) {
    reset(0);
    set_port(0, PORT_STATE_IDLE, 0);
    set_port(1, PORT_STATE_ACTIVE, 20000);
    set_port(2, PORT_STATE_FAULT, 0);
    render(0);
    MT_ASSERT(dark(PX(0)));
    MT_ASSERT(dark(PX(1)));
    MT_ASSERT_EQ(PX(2).r, LED_FAULT_FLOOR);
    MT_ASSERT(PX(2).g == 0 && PX(2).b == 0);
    render(100);
    MT_ASSERT(dark(PX(2))); // still blinking
}

static void all_idle(void) {
    for (int i = 0; i < NUM_PORTS; i++) set_port(i, PORT_STATE_IDLE, 0);
}

static void test_boot_sweep_white(void) {
    reset(255);
    all_idle();
    view.boot_pending = true;
    view.boot_start_ms = 1000;
    render(1000); // the chassis light first
    MT_ASSERT(CH.r == 255 && CH.g == 255 && CH.b == 255);
    for (int i = 1; i < LED_PIXELS; i++) MT_ASSERT(dark(px[i]));
    render(1000 + 2 * LED_BOOT_STEP_MS - 1); // then port 1
    MT_ASSERT(!dark(PX(0)));
    MT_ASSERT(dark(PX(1)));
    render(1000 + (LED_PIXELS - 1) * LED_BOOT_STEP_MS); // all lit, then held
    for (int i = 0; i < LED_PIXELS; i++) MT_ASSERT_EQ(px[i].r, 255);
    MT_ASSERT(view.boot_pending);
    render(1000 + LED_BOOT_SWEEP_MS); // over: ports show through
    MT_ASSERT(!view.boot_pending);
    MT_ASSERT(PX(0).r == 255 && PX(0).g == 120 && PX(0).b == 0);
    MT_ASSERT(all_clear(CH)); // nothing to report
}

static void test_boot_sweep_rainbow(void) {
    reset(255);
    all_idle();
    view.boot_style = LED_BOOT_RAINBOW;
    view.boot_pending = true;
    view.boot_start_ms = 0;
    render((LED_PIXELS - 1) * LED_BOOT_STEP_MS);
    MT_ASSERT(CH.r == 255 && CH.g == 0); // red first
    for (int i = 0; i < LED_PIXELS; i++) {
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
    for (int i = 0; i < LED_PIXELS; i++) MT_ASSERT(dark(px[i]));
    // sweep over: the fault floor returns, read at the start of a blink
    render((LED_BOOT_SWEEP_MS / 200 + 1) * 200);
    MT_ASSERT_EQ(PX(0).r, LED_FAULT_FLOOR);
}

static void test_chassis_light_breathes_blue_for_ble(void) {
    reset(255);
    all_idle();
    render(0);
    MT_ASSERT(all_clear(CH)); // nothing to report
    view.chassis = LED_CHASSIS_BLE_OPEN;
    render(LED_CHASSIS_PERIOD_MS / 2); // peak of the breath
    MT_ASSERT(CH.r == 0 && CH.g == 60 && CH.b == 255);
    for (int i = 0; i < NUM_PORTS; i++) MT_ASSERT_EQ(PX(i).r, 255); // ports untouched
    render(0); // trough: dim but never dark
    MT_ASSERT(CH.r == 0 && CH.b > 0 && CH.b < 255);
    MT_ASSERT_EQ(PX(0).r, 255);
}

static void test_net_down_is_white_and_ble_wins(void) {
    reset(255);
    all_idle();
    uint32_t peak = LED_CHASSIS_PERIOD_MS / 2;
    view.chassis = LED_CHASSIS_NET_DOWN;
    render(peak);
    MT_ASSERT(CH.r == 255 && CH.g == 255 && CH.b == 255);
    view.chassis = LED_CHASSIS_NET_DOWN | LED_CHASSIS_BLE_OPEN;
    render(peak);
    MT_ASSERT(CH.r == 0 && CH.b == 255);
    view.chassis = 0;
    render(peak);
    MT_ASSERT(all_clear(CH));
    MT_ASSERT(PX(0).r == 255 && PX(0).g == 120);
}

static void test_setup_is_magenta_between_ble_and_net_down(void) {
    reset(255);
    all_idle();
    uint32_t peak = LED_CHASSIS_PERIOD_MS / 2;
    view.chassis = LED_CHASSIS_SETUP_OPEN;
    render(peak);
    MT_ASSERT(CH.r == 255 && CH.g == 0 && CH.b == 200); // magenta
    MT_ASSERT_EQ(PX(0).r, 255);                          // port 1 still amber
    view.chassis = LED_CHASSIS_SETUP_OPEN | LED_CHASSIS_NET_DOWN;
    render(peak);
    MT_ASSERT(CH.r == 255 && CH.g == 0 && CH.b == 200); // setup beats net down
    view.chassis = LED_CHASSIS_SETUP_OPEN | LED_CHASSIS_BLE_OPEN;
    render(peak);
    MT_ASSERT(CH.r == 0 && CH.b == 255);                // Bluetooth beats setup
}

static void test_chassis_light_dark_at_zero_brightness(void) {
    reset(0);
    all_idle();
    view.chassis = LED_CHASSIS_BLE_OPEN;
    render(LED_CHASSIS_PERIOD_MS / 2);
    for (int i = 0; i < LED_PIXELS; i++) MT_ASSERT(dark(px[i]));
}

static void test_chassis_light_glows_dim_green(void) {
    reset(255);
    all_idle();
    render(0);
    MT_ASSERT(all_clear(CH));
    MT_ASSERT_EQ(CH.g, 220 * LED_CHASSIS_OK_MAX / 255); // capped on a bright chain
    render(LED_CHASSIS_PERIOD_MS / 2); // steady, no breath
    MT_ASSERT_EQ(CH.g, 220 * LED_CHASSIS_OK_MAX / 255);
    reset(48); // the default brightness: still capped
    all_idle();
    render(0);
    MT_ASSERT_EQ(CH.g, 220 * LED_CHASSIS_OK_MAX / 255);
    reset(4); // dimmed for the night: as bright as a port, not dark
    set_port(0, PORT_STATE_ACTIVE, 20000);
    render(0);
    MT_ASSERT(!dark(CH));
    MT_ASSERT(same(CH, PX(0)));
    reset(0); // dark at brightness 0
    render(0);
    MT_ASSERT(dark(CH));
}

static void test_bus_fault_blinks_red_over_everything(void) {
    reset(255);
    all_idle();
    view.chassis = LED_CHASSIS_BUS_FAULT | LED_CHASSIS_BLE_OPEN | LED_CHASSIS_NET_DOWN;
    render(0);
    MT_ASSERT(CH.r == 255 && CH.g == 0 && CH.b == 0); // red, blink on
    MT_ASSERT_EQ(PX(0).r, 255);                       // ports untouched
    render(100);                                      // 5 Hz, like a port fault
    MT_ASSERT(dark(CH));
    view.chassis = LED_CHASSIS_BLE_OPEN; // the bus came back: Bluetooth shows again
    render(LED_CHASSIS_PERIOD_MS / 2);
    MT_ASSERT(CH.r == 0 && CH.b == 255);
    // a dark chain still shows it, at the fault floor
    view.brightness = 0;
    view.chassis = LED_CHASSIS_BUS_FAULT;
    render(0);
    MT_ASSERT(CH.r == LED_FAULT_FLOOR && CH.g == 0 && CH.b == 0);
    MT_ASSERT(dark(PX(0)));
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
    MT_ASSERT(dark(px[LED_PIXELS - 1]));
    render(250); // and they swap at 2 Hz
    MT_ASSERT_EQ(px[0].b, 255);
    MT_ASSERT(dark(px[1]));
    render(5000); // expired: boot has long finished, ports show
    MT_ASSERT(!view.identify_pending);
    MT_ASSERT(!view.boot_pending);
    MT_ASSERT(PX(0).r == 255 && PX(0).g == 120);
}

static void test_ack_flash(void) {
    reset(255);
    all_idle();
    view.identify_pending = true; // the ack sits above identify...
    view.identify_until_ms = 5000;
    view.ack_pending = true;
    view.ack_until_ms = 200;
    render(0);
    for (int i = 0; i < LED_PIXELS; i++) MT_ASSERT(px[i].r == 255 && px[i].g == 255 && px[i].b == 255);
    render(200); // ...and is over in a moment
    MT_ASSERT(!view.ack_pending);
    MT_ASSERT(dark(px[0]) || px[0].b == 255); // identify again
    view.hold = 100; // ...but under the hold fill
    view.ack_pending = true;
    view.ack_until_ms = 1000;
    render(300);
    MT_ASSERT(px[0].b == 255 && px[0].r == 0);
    view.hold = 0;
    // visible on a blanked chain
    view.brightness = 0;
    render(400);
    MT_ASSERT_EQ(px[3].r, LED_IDENTIFY_FLOOR);
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
    mt_run("leds: button hold fills the chain", test_hold_fill);
    mt_run("leds: probe blinks cyan", test_probe_blinks_cyan);
    mt_run("leds: throttled pulses the active colour", test_throttled_pulses_active_colour);
    mt_run("leds: a charged sink shows magenta", test_charged_shows_magenta);
    mt_run("leds: master brightness scales", test_master_brightness_scales);
    mt_run("leds: brightness 0 keeps faults", test_brightness_zero_keeps_faults);
    mt_run("leds: boot sweep white", test_boot_sweep_white);
    mt_run("leds: boot sweep rainbow", test_boot_sweep_rainbow);
    mt_run("leds: boot sweep dark at brightness 0", test_boot_sweep_dark_at_zero_brightness);
    mt_run("leds: chassis light breathes blue for BLE", test_chassis_light_breathes_blue_for_ble);
    mt_run("leds: chassis light white for no network, BLE wins", test_net_down_is_white_and_ble_wins);
    mt_run("leds: chassis light magenta for setup, between BLE and net down",
           test_setup_is_magenta_between_ble_and_net_down);
    mt_run("leds: chassis light dark at brightness 0", test_chassis_light_dark_at_zero_brightness);
    mt_run("leds: chassis light glows dim green when all is well", test_chassis_light_glows_dim_green);
    mt_run("leds: bus fault blinks red over everything", test_bus_fault_blinks_red_over_everything);
    mt_run("leds: identify overrides everything", test_identify_overrides_everything);
    mt_run("leds: identify visible at brightness 0", test_identify_visible_when_dimmed_to_zero);
    mt_run("leds: acknowledge flash", test_ack_flash);
}
