#include "test_support.h"

#include <stdio.h>
#include <string.h>

#include "budget.h"
#include "fan_policy.h"
#include "ipc.h"
#include "port_fsm.h"
#include "settings.h"
#include "tca9539.h"

int mt_failures;
int mt_asserts;
static int mt_tests;
static int mt_failed_tests;

void mt_run(const char *name, void (*fn)(void)) {
    int before = mt_failures;
    printf("  %s\n", name);
    fn();
    mt_tests++;
    if (mt_failures != before) mt_failed_tests++;
}

int mt_summary(void) {
    printf("%d tests, %d assertions, %d failures\n", mt_tests, mt_asserts,
           mt_failures);
    return mt_failed_tests ? 1 : 0;
}

// ---- link-time stand-ins for the engine's environment ----------------------

settings_t g_settings;

#define EVT_CAP 256
static engine_evt_t evts[EVT_CAP];
static int n_evts;

bool ipc_evt_push(const engine_evt_t *evt) {
    if (n_evts < EVT_CAP) evts[n_evts++] = *evt;
    return true;
}

// ---- fixture ---------------------------------------------------------------

uint32_t now_ms;
telemetry_t tele;

void support_reset(uint32_t budget_mw) {
    memset(&g_settings, 0, sizeof(g_settings));
    g_settings.budget_mw = budget_mw;
    for (int i = 0; i < NUM_PORTS; i++) {
        g_settings.port_limit_ma[i] = 5000;
        g_settings.port_max_mv[i] = PORT_VOLT_MAX_MV;
        g_settings.port_priority[i] = (uint8_t)i;
    }
    g_settings.fan_auto = 1; // firmware defaults (settings_defaults)
    g_settings.fan_on_w = 80;
    g_settings.fan_off_w = 60;
    g_settings.fan_on_ma = 3000;
    sim_reset();
    tca9539_init(); // cold start, as engine_main does: the sim expander is programmed from here
    budget_init(budget_mw);
    port_fsm_init();
    fan_policy_init(g_settings.fan_auto != 0, false);
    evt_clear();
    now_ms = 0;
    memset(&tele, 0, sizeof(tele));
}

void tick(uint32_t n) {
    while (n--) {
        now_ms += 10;
        uint16_t inputs;
        bool have = tca9539_read_inputs(&inputs);
        if (sim_alert_asserted()) port_fsm_alert_sweep(now_ms);
        tele.total_mw = 0;
        for (uint8_t i = 0; i < NUM_PORTS; i++) {
            bool present = have && tca9539_present_from(inputs, i);
            port_fsm_tick(i, present, now_ms, &tele.port[i]);
            tele.total_mw += tele.port[i].power_mw;
        }
        fan_policy_tick(&tele, now_ms);
        tele.fan_on = fan_policy_on();
        tele.fan_auto = fan_policy_auto();
    }
}

uint8_t port_state(uint8_t i) {
    return tele.port[i].state;
}

int evt_count(uint8_t type, uint8_t port) {
    int n = 0;
    for (int i = 0; i < n_evts; i++)
        if (evts[i].type == type && (port == 0xFF || evts[i].port == port)) n++;
    return n;
}

const engine_evt_t *evt_last(uint8_t type, uint8_t port) {
    for (int i = n_evts - 1; i >= 0; i--)
        if (evts[i].type == type && (port == 0xFF || evts[i].port == port))
            return &evts[i];
    return NULL;
}

void evt_clear(void) {
    n_evts = 0;
}
