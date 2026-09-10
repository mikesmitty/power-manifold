#include "engine.h"

#include <string.h>

#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "pico/flash.h"
#include "pico/stdlib.h"

#include "budget.h"
#include "fan_policy.h"
#include "ipc.h"
#include "leds.h"
#include "manifold.h"
#include "pins.h"
#include "port_fsm.h"
#include "settings.h"
#include "stack_probe.h"
#include "tca9539.h"
#include "tca9548a.h"

#ifdef PWRMAN_FAKE_BLADES
#include "sim/sim_blades.h"
#include "sim/sim_inject.h"
#include "sim/sim_scenario.h"
#endif

#define TICK_MS 10 // 100 Hz supervisory rate
#define PRESENCE_REFRESH_TICKS 10 // full presence re-read every 100ms

// EVT_PROBE_FAIL port value for chassis-level (non-port) problems
#define CHASSIS_EVT_PORT 0xFF

static volatile bool alert_irq;
static volatile bool exp_irq;

static bool present[NUM_PORTS];
static uint8_t exp_fail_streak;

volatile uint8_t engine_stage;
volatile uint32_t engine_stage_us;
#define STAGE(n) do { engine_stage_us = time_us_32(); engine_stage = (n); } while (0)

// Delivered energy, integrated at the tick rate in mW·ms (µJ). uint64 is
// centuries of headroom; the mWh views in telemetry wrap after ~4.9 years
// of continuous 100W, which resets HA's total_increasing meters harmlessly.
static uint64_t port_energy_uj[NUM_PORTS];
static uint64_t total_energy_uj;

#ifndef PWRMAN_FAKE_BLADES
static void gpio_irq_handler(uint gpio, uint32_t events) {
    (void)events;
    if (gpio == PIN_ALERT_N) alert_irq = true;
    else if (gpio == PIN_EXP_INT_N) exp_irq = true;
}
#endif

// GLOBAL_ALERT# level; the sim stands in for the wire-OR line in fake builds
static bool alert_line_active(void) {
#ifdef PWRMAN_FAKE_BLADES
    return sim_alert_asserted();
#else
    return !gpio_get(PIN_ALERT_N);
#endif
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

static void dispatch_cmd(const engine_cmd_t *cmd, uint32_t now_ms) {
    switch ((cmd_op_t)cmd->op) {
    case CMD_SET_BUDGET:
        budget_set_total(cmd->arg);
        break;
    case CMD_FAN:
        fan_policy_set_manual(cmd->arg != 0);
        break;
    case CMD_FAN_AUTO:
        fan_policy_set_auto();
        break;
    case CMD_LED_BRIGHTNESS:
        leds_set_brightness((uint8_t)cmd->arg);
        break;
    case CMD_LED_IDENTIFY:
        leds_identify(now_ms + cmd->arg);
        break;
    case CMD_LED_CHASSIS:
        leds_set_chassis((uint8_t)cmd->arg);
        break;
    case CMD_SIM:
#ifdef PWRMAN_FAKE_BLADES
        if (sim_inject_op(cmd->arg) == SIM_SCENARIO) sim_scenario_set_running(sim_inject_value(cmd->arg) != 0);
        else sim_inject(cmd->port, cmd->arg);
#endif
        break;
    default:
        if (cmd->port < NUM_PORTS) port_fsm_cmd(cmd->port, cmd);
        break;
    }
}

void engine_main(void) {
    stack_probe_paint();
    STAGE(ENGINE_STAGE_ENTERED);
    // allow core 0 to write flash (settings) while this core is parked
    flash_safe_execute_core_init();

#ifndef PWRMAN_FAKE_BLADES
    i2c_init(I2C_BUS, I2C_BAUD);
    gpio_set_function(PIN_I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(PIN_I2C_SCL, GPIO_FUNC_I2C);
    gpio_init(PIN_ALERT_N);
    gpio_set_dir(PIN_ALERT_N, GPIO_IN);
    // SDA/SCL and ALERT# have 4.7k external pull-ups on the controller card
    // (or wired on the pcie-breakout for a Pico 2 W). Every RP2350 pad boots
    // with its pull-down enabled and gpio_set_function/gpio_init leave it so;
    // that pull-down would sit across the external pull-up, eating noise
    // margin on every high level. Clear it.
    gpio_disable_pulls(PIN_I2C_SDA);
    gpio_disable_pulls(PIN_I2C_SCL);
    gpio_disable_pulls(PIN_ALERT_N);
    // EXP_INT# is open-drain from the TCA9539 with no pull-up anywhere else;
    // without this the pad default (pull-down) reads it as permanently asserted.
    gpio_init(PIN_EXP_INT_N);
    gpio_set_dir(PIN_EXP_INT_N, GPIO_IN);
    gpio_pull_up(PIN_EXP_INT_N);
#else
    sim_reset();
#endif
    STAGE(ENGINE_STAGE_BUS_INIT);

    tca9548a_init();
    tca9539_init(); // outputs low FIRST, then direction (spec §6.4)
    STAGE(ENGINE_STAGE_MUX_EXP);
    leds_init();
    leds_set_brightness(g_settings.led_brightness);
    leds_set_boot_style(g_settings.led_boot);
    leds_boot_sweep(to_ms_since_boot(get_absolute_time())); // also a chain-order check
    STAGE(ENGINE_STAGE_LEDS);

    budget_init(g_settings.budget_mw);
    port_fsm_init();
    fan_policy_init(g_settings.fan_auto != 0);
    STAGE(ENGINE_STAGE_FSM);

#ifndef PWRMAN_FAKE_BLADES
    gpio_set_irq_enabled_with_callback(PIN_ALERT_N, GPIO_IRQ_EDGE_FALL, true,
                                       gpio_irq_handler);
    gpio_set_irq_enabled(PIN_EXP_INT_N, GPIO_IRQ_EDGE_FALL, true);
#endif
    STAGE(ENGINE_STAGE_IRQS);

#ifdef PWRMAN_FAKE_BLADES
    sim_scenario_tick(to_ms_since_boot(get_absolute_time())); // seat the demo blades first
#endif
    refresh_presence();
    // blades already seated come up one at a time, in priority order
    port_fsm_boot_inventory(present, to_ms_since_boot(get_absolute_time()));
    STAGE(ENGINE_STAGE_PRESENCE);

    uint32_t tick = 0;
    absolute_time_t next = get_absolute_time();
    for (;;) {
        next = delayed_by_ms(next, TICK_MS);
        uint32_t now_ms = to_ms_since_boot(get_absolute_time());

#ifdef PWRMAN_FAKE_BLADES
        sim_scenario_tick(now_ms);
#endif

        engine_cmd_t cmd;
        while (ipc_cmd_pop(&cmd)) dispatch_cmd(&cmd, now_ms);

        // Fault line first: it is wire-OR'd, so sweep all powered ports.
        // Level-check as well as the IRQ flag in case an edge was missed.
        if (alert_irq || alert_line_active()) {
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

        for (uint8_t i = 0; i < NUM_PORTS; i++) {
            t.total_mw += t.port[i].power_mw;
            port_energy_uj[i] += (uint64_t)t.port[i].power_mw * TICK_MS;
            t.port[i].energy_mwh = (uint32_t)(port_energy_uj[i] / 3600000u);
        }
        total_energy_uj += (uint64_t)t.total_mw * TICK_MS;
        t.energy_mwh = (uint32_t)(total_energy_uj / 3600000u);
        t.reserved_mw = budget_reserved();
        t.budget_mw = budget_total();

        fan_policy_tick(&t, now_ms);
        t.fan_on = fan_policy_on();
        t.fan_auto = fan_policy_auto();
        t.alert_active = alert_line_active();

        ipc_snapshot_publish(&t);
        leds_render(&t, now_ms);
        ipc_engine_heartbeat();
        STAGE(ENGINE_STAGE_LOOP);

        tick++;
        sleep_until(next);
    }
}
