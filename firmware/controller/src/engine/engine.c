#include "engine.h"

#include <string.h>

#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "pico/flash.h"
#include "pico/stdlib.h"

#include "budget.h"
#include "ipc.h"
#include "leds.h"
#include "manifold.h"
#include "pins.h"
#include "port_fsm.h"
#include "settings.h"
#include "tca9539.h"
#include "tca9548a.h"

#define TICK_MS 10 // 100 Hz supervisory rate
#define PRESENCE_REFRESH_TICKS 10 // full presence re-read every 100ms

// EVT_PROBE_FAIL port value for chassis-level (non-port) problems
#define CHASSIS_EVT_PORT 0xFF

static volatile bool alert_irq;
static volatile bool exp_irq;

static bool present[NUM_PORTS];
static bool fan_on;
static uint8_t exp_fail_streak;

static void gpio_irq_handler(uint gpio, uint32_t events) {
    (void)events;
    if (gpio == PIN_ALERT_N) alert_irq = true;
    else if (gpio == PIN_EXP_INT_N) exp_irq = true;
}

static void refresh_presence(void) {
    uint16_t inputs;
    if (!tca9539_read_inputs(&inputs)) {
        // I2C trouble at the expander is chassis-level: after a few misses,
        // reset the mux (frees a hung downstream segment) and re-init the
        // expander, which drops every EN — safe-side behavior by design.
        if (++exp_fail_streak >= 5) {
            exp_fail_streak = 0;
            tca9548a_hw_reset();
            tca9539_init();
            engine_evt_t e = {.type = EVT_PROBE_FAIL, .port = CHASSIS_EVT_PORT};
            ipc_evt_push(&e);
        }
        return;
    }
    exp_fail_streak = 0;
    for (uint8_t i = 0; i < NUM_PORTS; i++)
        present[i] = tca9539_present_from(inputs, i);
}

static void dispatch_cmd(const engine_cmd_t *cmd) {
    switch ((cmd_op_t)cmd->op) {
    case CMD_SET_BUDGET:
        budget_set_total(cmd->arg);
        break;
    case CMD_FAN:
        if (tca9539_set_fan(cmd->arg != 0)) fan_on = cmd->arg != 0;
        break;
    case CMD_LED_BRIGHTNESS:
        leds_set_brightness((uint8_t)cmd->arg);
        break;
    default:
        if (cmd->port < NUM_PORTS) port_fsm_cmd(cmd->port, cmd);
        break;
    }
}

void engine_main(void) {
    // allow core 0 to write flash (settings) while this core is parked
    flash_safe_execute_core_init();

    i2c_init(I2C_BUS, I2C_BAUD);
    gpio_set_function(PIN_I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(PIN_I2C_SCL, GPIO_FUNC_I2C);
    // pull-ups live on the backplane; no on-chip pulls

    gpio_init(PIN_ALERT_N);
    gpio_set_dir(PIN_ALERT_N, GPIO_IN);
    gpio_init(PIN_EXP_INT_N);
    gpio_set_dir(PIN_EXP_INT_N, GPIO_IN);

    tca9548a_init();
    tca9539_init(); // outputs low FIRST, then direction (spec §6.4)
    leds_init();
    leds_set_brightness(g_settings.led_brightness);

    budget_init(g_settings.budget_mw);
    port_fsm_init();

    gpio_set_irq_enabled_with_callback(PIN_ALERT_N, GPIO_IRQ_EDGE_FALL, true,
                                       gpio_irq_handler);
    gpio_set_irq_enabled(PIN_EXP_INT_N, GPIO_IRQ_EDGE_FALL, true);

    refresh_presence();

    uint32_t tick = 0;
    absolute_time_t next = get_absolute_time();
    for (;;) {
        next = delayed_by_ms(next, TICK_MS);
        uint32_t now_ms = to_ms_since_boot(get_absolute_time());

        engine_cmd_t cmd;
        while (ipc_cmd_pop(&cmd)) dispatch_cmd(&cmd);

        // Fault line first: it is wire-OR'd, so sweep all powered ports.
        // Level-check as well as the IRQ flag in case an edge was missed.
        if (alert_irq || !gpio_get(PIN_ALERT_N)) {
            alert_irq = false;
            port_fsm_alert_sweep(now_ms);
        }

        if (exp_irq || (tick % PRESENCE_REFRESH_TICKS) == 0) {
            exp_irq = false;
            refresh_presence();
        }

        telemetry_t t;
        memset(&t, 0, sizeof(t));
        for (uint8_t i = 0; i < NUM_PORTS; i++)
            port_fsm_tick(i, present[i], now_ms, &t.port[i]);

        for (uint8_t i = 0; i < NUM_PORTS; i++) t.total_mw += t.port[i].power_mw;
        t.reserved_mw = budget_reserved();
        t.budget_mw = budget_total();
        t.fan_on = fan_on;
        t.alert_active = !gpio_get(PIN_ALERT_N);

        ipc_snapshot_publish(&t);
        leds_render(&t, now_ms);
        ipc_engine_heartbeat();

        tick++;
        sleep_until(next);
    }
}
