#include "led_pattern.h"

#include <string.h>

static const led_rgb_t COL_OFF     = {0, 0, 0};
static const led_rgb_t COL_WHITE   = {255, 255, 255};
static const led_rgb_t COL_CYAN    = {0, 180, 180};
static const led_rgb_t COL_AMBER   = {255, 120, 0};
static const led_rgb_t COL_BLUE    = {0, 60, 255};
static const led_rgb_t COL_GREEN   = {0, 220, 40};
static const led_rgb_t COL_RED     = {255, 0, 0};

// Boot sweep hues, one per slot, so the rainbow reads left to right
static const led_rgb_t RAINBOW[NUM_PORTS] = {
    {255, 0, 0}, {255, 140, 0}, {220, 220, 0}, {0, 200, 40}, {0, 90, 255}, {170, 0, 255},
};

void led_view_init(led_view_t *v, uint8_t brightness, uint8_t boot_style) {
    memset(v, 0, sizeof(*v));
    v->brightness = brightness;
    v->boot_style = boot_style;
}

// c * num / den, per channel
static led_rgb_t scale(led_rgb_t c, uint32_t num, uint32_t den) {
    led_rgb_t o = {
        (uint8_t)((uint32_t)c.r * num / den),
        (uint8_t)((uint32_t)c.g * num / den),
        (uint8_t)((uint32_t)c.b * num / den),
    };
    return o;
}

static uint32_t blink(uint32_t now_ms, uint32_t period_ms) {
    return (now_ms % period_ms) < period_ms / 2 ? 255 : 0;
}

// triangle wave that never goes fully dark, so a pulsing port stays lit
static uint32_t pulse(uint32_t now_ms, uint32_t period_ms) {
    uint32_t ph = now_ms % period_ms;
    uint32_t half = period_ms / 2;
    uint32_t v = ph < half ? ph : period_ms - ph;
    return 30 + (225 * v) / half;
}

static led_rgb_t active_colour(const port_telemetry_t *p) {
    return p->bus_mv >= 19000 ? COL_GREEN : COL_BLUE;
}

// Per-slot state colour at full brightness; *level is the 0-255 modulation
// (blink/pulse/dim) to apply on top.
static led_rgb_t port_colour(const port_telemetry_t *p, uint32_t now_ms, uint32_t *level) {
    *level = 255;
    switch ((port_state_t)p->state) {
    case PORT_STATE_ABSENT:
        *level = 5;
        return COL_WHITE; // dim white: slot empty, chain alive
    case PORT_STATE_PROBE:
        *level = blink(now_ms, 500); // 2 Hz
        return COL_CYAN;
    case PORT_STATE_IDLE:
        return COL_AMBER;
    case PORT_STATE_ACTIVE:
        return active_colour(p);
    case PORT_STATE_THROTTLED:
        *level = pulse(now_ms, 1000); // 1 Hz: delivering, but clamped
        return active_colour(p);
    case PORT_STATE_FAULT:
        *level = blink(now_ms, 200); // 5 Hz
        return COL_RED;
    case PORT_STATE_DISABLED:
    default:
        return COL_OFF;
    }
}

static void render_ports(const led_view_t *v, const telemetry_t *t, uint32_t now_ms,
                         led_rgb_t out[NUM_PORTS]) {
    for (int i = 0; i < NUM_PORTS; i++) {
        const port_telemetry_t *p = &t->port[i];
        uint32_t level;
        led_rgb_t c = port_colour(p, now_ms, &level);
        uint32_t bright = v->brightness;
        if (bright == 0) {
            if (p->state != PORT_STATE_FAULT) { out[i] = COL_OFF; continue; }
            bright = LED_FAULT_FLOOR;
        }
        out[i] = scale(scale(c, level, 255), bright, 255);
    }
}

static bool render_identify(led_view_t *v, uint32_t now_ms, led_rgb_t out[NUM_PORTS]) {
    if (!v->identify_pending) return false;
    if ((int32_t)(now_ms - v->identify_until_ms) >= 0) {
        v->identify_pending = false;
        return false;
    }
    // alternate halves of the chain in blue, 2 Hz: unlike any port state
    uint32_t bright = v->brightness < LED_IDENTIFY_FLOOR ? LED_IDENTIFY_FLOOR : v->brightness;
    for (int i = 0; i < NUM_PORTS; i++) {
        bool lit = ((now_ms / 250) + (uint32_t)i) & 1;
        out[i] = lit ? scale(COL_BLUE, bright, 255) : COL_OFF;
    }
    return true;
}

static bool render_boot(led_view_t *v, uint32_t now_ms, led_rgb_t out[NUM_PORTS]) {
    if (!v->boot_pending) return false;
    uint32_t elapsed = now_ms - v->boot_start_ms;
    if (elapsed >= LED_BOOT_SWEEP_MS) {
        v->boot_pending = false;
        return false;
    }
    if (v->brightness == 0) { // dark rack: skip the show, keep the timing
        for (int i = 0; i < NUM_PORTS; i++) out[i] = COL_OFF;
        return true;
    }
    uint32_t reached = elapsed / LED_BOOT_STEP_MS; // pixels 0..reached are lit
    for (int i = 0; i < NUM_PORTS; i++) {
        if ((uint32_t)i > reached) { out[i] = COL_OFF; continue; }
        led_rgb_t c = v->boot_style == LED_BOOT_RAINBOW ? RAINBOW[i] : COL_WHITE;
        out[i] = scale(c, v->brightness, 255);
    }
    return true;
}

static void overlay_comet(const led_view_t *v, uint32_t now_ms, led_rgb_t out[NUM_PORTS]) {
    if (v->brightness == 0) return;
    led_rgb_t c;
    if (v->chassis & LED_CHASSIS_BLE_OPEN) c = COL_BLUE;       // actionable: shown first
    else if (v->chassis & LED_CHASSIS_NET_DOWN) c = COL_WHITE;
    else return;
    uint32_t ph = now_ms % LED_COMET_PERIOD_MS;
    uint32_t head = ph / LED_COMET_STEP_MS;
    if (head >= NUM_PORTS) return; // between passes
    out[head] = scale(c, v->brightness, 255);
    if (head > 0) out[head - 1] = scale(out[head], 1, 3); // short tail
}

void led_pattern_render(led_view_t *v, const telemetry_t *t, uint32_t now_ms,
                        led_rgb_t out[NUM_PORTS]) {
    if (render_identify(v, now_ms, out)) return;
    if (render_boot(v, now_ms, out)) return;
    render_ports(v, t, now_ms, out);
    overlay_comet(v, now_ms, out);
}
