#include "leds.h"

#include "hardware/pio.h"

#include "led_pattern.h"
#include "pins.h"
#include "ws2812.pio.h"

// PIO writer for the backplane chain. The colour policy lives in
// led_pattern.c (hardware-free, host-tested); this file only keeps the view
// state and pushes frames.

#define FRAME_INTERVAL_MS 20 // 50 Hz; WS2812 latch needs >50us idle between frames

static PIO pio;
static uint sm;
static uint offset;
static led_view_t view;
static uint32_t last_frame_ms;

void leds_init(void) {
    // dynamic claim coexists with the cyw43 driver's own PIO usage
    hard_assert(pio_claim_free_sm_and_add_program_for_gpio_range(
        &ws2812_program, &pio, &sm, &offset, PIN_LED_DATA, 1, true));
    ws2812_program_init(pio, sm, offset, PIN_LED_DATA, 800000, false);
    led_view_init(&view, 48, LED_BOOT_WHITE);
}

void leds_set_brightness(uint8_t b) {
    view.brightness = b;
}

void leds_set_boot_style(uint8_t style) {
    view.boot_style = style;
}

void leds_boot_sweep(uint32_t now_ms) {
    view.boot_start_ms = now_ms;
    view.boot_pending = true;
}

void leds_identify(uint32_t until_ms) {
    view.identify_until_ms = until_ms;
    view.identify_pending = true;
}

void leds_set_chassis(uint8_t flags) {
    view.chassis = flags;
}

static void put_pixel(led_rgb_t c) {
    uint32_t grb = ((uint32_t)c.g << 16) | ((uint32_t)c.r << 8) | c.b;
    pio_sm_put_blocking(pio, sm, grb << 8u);
}

void leds_render(const telemetry_t *t, uint32_t now_ms) {
    if (now_ms - last_frame_ms < FRAME_INTERVAL_MS) return;
    last_frame_ms = now_ms;

    led_rgb_t px[NUM_PORTS];
    led_pattern_render(&view, t, now_ms, px);
    for (int i = 0; i < NUM_PORTS; i++) put_pixel(px[i]);
}
