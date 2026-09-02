#include "fan_policy.h"

#include "settings.h"
#include "tca9539.h"

static bool     fan_on;
static bool     auto_mode;
static uint32_t hold_until_ms;

void fan_policy_init(bool auto_mode_) {
    fan_on = false; // expander init drives every output low
    auto_mode = auto_mode_;
    hold_until_ms = 0;
}

void fan_policy_set_manual(bool on) {
    auto_mode = false;
    if (tca9539_set_fan(on)) fan_on = on;
}

void fan_policy_set_auto(void) {
    auto_mode = true;
    hold_until_ms = 0; // let the policy act immediately
}

bool fan_policy_on(void) {
    return fan_on;
}

bool fan_policy_auto(void) {
    return auto_mode;
}

uint32_t fan_policy_contract_ma(const port_telemetry_t *p) {
    if (!p->attached) return 0;
    // No bus reading yet: assume vSafe5V, like the throttle path does. That
    // over-estimates the current, which errs toward running the fan.
    uint32_t mv = p->bus_mv ? p->bus_mv : 5000;
    return (p->contract_mw * 1000u + mv / 2) / mv;
}

// The rule is "over": a nominal 3A contract (the everyday 5V/3A one) must
// not trip a 3000mA setting. The derived current wobbles with the bus
// reading — USB PD lets a fixed source sit within ±5% of nominal, and the
// MPQ4242 floors the contract to 0.5W — so a 3A contract on a bus reading
// 19.5V derives to 3077mA. Require more than 5% over the setting: 3A never
// counts on an in-spec bus, 3.25A always does.
static bool any_high_current_contract(const telemetry_t *t) {
    uint32_t trip_ma = g_settings.fan_on_ma;
    if (!trip_ma) return false; // 0 disables the current rule
    for (int i = 0; i < NUM_PORTS; i++) {
        if (fan_policy_contract_ma(&t->port[i]) * 20u > trip_ma * 21u)
            return true;
    }
    return false;
}

void fan_policy_tick(const telemetry_t *t, uint32_t now_ms) {
    if (!auto_mode) return;

    bool hot_port = any_high_current_contract(t);
    bool want;
    if (!fan_on)
        want = hot_port || t->total_mw >= (uint32_t)g_settings.fan_on_w * 1000u;
    else // stays on until both the power band and every contract are clear
        want = hot_port || t->total_mw > (uint32_t)g_settings.fan_off_w * 1000u;

    if (want == fan_on) return;
    if ((int32_t)(now_ms - hold_until_ms) < 0) return;
    if (!tca9539_set_fan(want)) return;
    fan_on = want;
    hold_until_ms = now_ms + FAN_MIN_HOLD_MS;
}
