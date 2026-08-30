#include "leds.h"

#include "hardware/pio.h"

#include "pins.h"
#include "ws2812.pio.h"

#define FRAME_INTERVAL_MS 20 // 50 Hz; WS2812 latch needs >50us idle between frames

static PIO pio;
static uint sm;
static uint offset;
static uint8_t brightness = 48;
static uint32_t last_frame_ms;

typedef struct { uint8_t r, g, b; } rgb_t;

static const rgb_t COL_OFF    = {0, 0, 0};
static const rgb_t COL_WHITE  = {32, 32, 32};
static const rgb_t COL_CYAN   = {0, 180, 180};
static const rgb_t COL_AMBER  = {255, 120, 0};
static const rgb_t COL_BLUE   = {0, 60, 255};
static const rgb_t COL_GREEN  = {0, 220, 40};
static const rgb_t COL_YELLOW = {255, 200, 0};
static const rgb_t COL_RED    = {255, 0, 0};

void leds_init(void) {
    // dynamic claim coexists with the cyw43 driver's own PIO usage
    hard_assert(pio_claim_free_sm_and_add_program_for_gpio_range(
        &ws2812_program, &pio, &sm, &offset, PIN_LED_DATA, 1, true));
    ws2812_program_init(pio, sm, offset, PIN_LED_DATA, 800000, false);
}

void leds_set_brightness(uint8_t b) {
    brightness = b;
}

static void put_pixel(rgb_t c, uint8_t scale) {
    uint32_t r = ((uint32_t)c.r * brightness * scale) / (255u * 255u);
    uint32_t g = ((uint32_t)c.g * brightness * scale) / (255u * 255u);
    uint32_t b = ((uint32_t)c.b * brightness * scale) / (255u * 255u);
    uint32_t grb = (g << 16) | (r << 8) | b;
    pio_sm_put_blocking(pio, sm, grb << 8u);
}

static uint8_t blink(uint32_t now_ms, uint32_t period_ms) {
    return (now_ms % period_ms) < period_ms / 2 ? 255 : 0;
}

static uint8_t pulse(uint32_t now_ms, uint32_t period_ms) {
    uint32_t ph = now_ms % period_ms;
    uint32_t half = period_ms / 2;
    uint32_t v = ph < half ? ph : period_ms - ph;
    return (uint8_t)(30 + (225 * v) / half); // never fully dark
}

void leds_render(const telemetry_t *t, uint32_t now_ms) {
    if (now_ms - last_frame_ms < FRAME_INTERVAL_MS) return;
    last_frame_ms = now_ms;

    for (int i = 0; i < NUM_PORTS; i++) {
        const port_telemetry_t *p = &t->port[i];
        rgb_t c;
        uint8_t scale = 255;
        switch ((port_state_t)p->state) {
        case PORT_STATE_ABSENT:
            c = COL_WHITE; scale = 40; break;
        case PORT_STATE_PROBE:
            c = COL_CYAN; scale = blink(now_ms, 500); break; // 2 Hz
        case PORT_STATE_IDLE:
            c = COL_AMBER; break;
        case PORT_STATE_ACTIVE:
            c = p->bus_mv >= 19000 ? COL_GREEN : COL_BLUE; break;
        case PORT_STATE_THROTTLED:
            c = COL_YELLOW; scale = pulse(now_ms, 1000); break; // 1 Hz
        case PORT_STATE_FAULT:
            c = COL_RED; scale = blink(now_ms, 200); break; // 5 Hz
        case PORT_STATE_DISABLED:
        default:
            c = COL_OFF; break;
        }
        put_pixel(c, scale);
    }
}
