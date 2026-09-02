#include "fan_policy.h"
#include "manifold.h"
#include "microtest.h"
#include "port_fsm.h"
#include "settings.h"
#include "test_support.h"

// Auto fan policy: the fixture's tick() runs fan_policy_tick after the port
// FSMs, like engine_main. The sim reports measured draw as load_pct of the
// contract current (80% by default), so a 20V/3A contract shows 48W.
// Defaults from support_reset: on >= 80W, off <= 60W, any contract over 3A.

static void seat_and_attach(uint8_t slot, uint16_t mv, uint32_t ma) {
    sim_set_present(slot, true);
    tick(2);
    sim_attach(slot, mv, ma);
    tick(2);
}

static void test_contract_ma_derivation(void) {
    port_telemetry_t p = {.attached = true, .bus_mv = 20000, .contract_mw = 100000};
    MT_ASSERT_EQ(fan_policy_contract_ma(&p), 5000);
    p.bus_mv = 5000;
    p.contract_mw = 15000;
    MT_ASSERT_EQ(fan_policy_contract_ma(&p), 3000);
    p.attached = false; // idle port: the base reserve is not a contract
    MT_ASSERT_EQ(fan_policy_contract_ma(&p), 0);
    p.attached = true;
    p.bus_mv = 0; // no reading yet: assumed vSafe5V
    MT_ASSERT_EQ(fan_policy_contract_ma(&p), 3000);
}

static void test_power_hysteresis(void) {
    support_reset(360000);
    seat_and_attach(0, 20000, 3000); // 60W contract, 48W measured
    MT_ASSERT_EQ(tele.total_mw, 48000);
    MT_ASSERT(!sim_fan());

    seat_and_attach(1, 20000, 3000); // 96W measured: above on_w
    MT_ASSERT_EQ(tele.total_mw, 96000);
    MT_ASSERT(sim_fan());

    sim_detach(1); // back to 48W: below off_w, but the hold keeps it on
    tick(2);
    MT_ASSERT_EQ(tele.total_mw, 48000);
    MT_ASSERT(sim_fan());
    tick_ms(FAN_MIN_HOLD_MS);
    MT_ASSERT(!sim_fan());
}

static void test_five_amp_contract_trips_at_low_power(void) {
    support_reset(360000);
    seat_and_attach(0, 5000, 5000); // 25W contract, 20W measured
    MT_ASSERT_EQ(fan_policy_contract_ma(&tele.port[0]), 5000);
    MT_ASSERT(tele.total_mw < 80000);
    MT_ASSERT(sim_fan());
}

static void test_high_voltage_five_amp_contract_trips_when_idle_draw(void) {
    support_reset(360000);
    sim_set_load_pct(2, 5); // sink negotiated 100W but is barely drawing
    seat_and_attach(2, 20000, 5000);
    MT_ASSERT_EQ(tele.total_mw, 5000);
    MT_ASSERT(sim_fan());
}

static void test_three_amp_contracts_do_not_trip(void) {
    support_reset(360000);
    seat_and_attach(0, 5000, 3000); // the everyday phone contract: 15W
    MT_ASSERT_EQ(fan_policy_contract_ma(&tele.port[0]), 3000);
    MT_ASSERT(!sim_fan());
    sim_set_load_pct(1, 50);
    seat_and_attach(1, 20000, 3000); // 60W contract, 30W measured
    MT_ASSERT_EQ(fan_policy_contract_ma(&tele.port[1]), 3000);
    MT_ASSERT(!sim_fan());
}

static void test_just_over_three_amps_trips(void) {
    support_reset(360000);
    sim_set_load_pct(0, 20);
    seat_and_attach(0, 20000, 3250); // 65W laptop contract, 13W measured
    MT_ASSERT_EQ(fan_policy_contract_ma(&tele.port[0]), 3250);
    MT_ASSERT(sim_fan());
}

static void test_tolerance_tracks_bus_reading(void) {
    support_reset(360000);
    // a nominal 20V/3A contract on a bus reading 19.5V derives to 3077mA:
    // over 3000, but within PD's ±5% band, so it must not count
    telemetry_t t = {0};
    t.port[3].attached = true;
    t.port[3].bus_mv = 19500;
    t.port[3].contract_mw = 60000;
    MT_ASSERT_EQ(fan_policy_contract_ma(&t.port[3]), 3077);
    fan_policy_tick(&t, 10);
    MT_ASSERT(!sim_fan());
    // a nominal 20V/3.25A contract on a bus reading 20.5V still counts
    t.port[3].bus_mv = 20500;
    t.port[3].contract_mw = 65000;
    MT_ASSERT_EQ(fan_policy_contract_ma(&t.port[3]), 3171);
    fan_policy_tick(&t, 20);
    MT_ASSERT(sim_fan());
}

static void test_stays_on_until_both_rules_clear(void) {
    support_reset(360000);
    sim_set_load_pct(0, 100);
    sim_set_load_pct(1, 100);
    sim_set_load_pct(2, 20);
    seat_and_attach(0, 20000, 2000); // 40W
    seat_and_attach(1, 20000, 2000); // 40W: total 80W trips the power rule
    MT_ASSERT(sim_fan());
    seat_and_attach(2, 5000, 5000);  // 5A contract, 5W measured

    sim_detach(0); // power rule clears (45W), current rule still holds
    tick_ms(FAN_MIN_HOLD_MS + 100);
    MT_ASSERT_EQ(tele.total_mw, 45000);
    MT_ASSERT(sim_fan());

    sim_detach(2); // both clear: 40W and no high-current contract
    tick(2);
    MT_ASSERT(!sim_fan());
}

static void test_stays_on_while_power_rule_holds_after_contract_ends(void) {
    support_reset(360000);
    sim_set_load_pct(0, 100);
    sim_set_load_pct(1, 100);
    seat_and_attach(2, 5000, 5000); // current rule trips at 20W
    MT_ASSERT(sim_fan());
    seat_and_attach(0, 20000, 2000); // 40W
    seat_and_attach(1, 20000, 2000); // 40W: 100W total

    sim_detach(2); // current rule clears; 80W > off_w keeps it on
    tick_ms(FAN_MIN_HOLD_MS + 100);
    MT_ASSERT_EQ(tele.total_mw, 80000);
    MT_ASSERT(sim_fan());

    sim_detach(0); // 40W <= off_w: off
    tick(2);
    MT_ASSERT(!sim_fan());
}

static void test_throttled_port_counts_its_clamped_contract(void) {
    support_reset(60000); // 60W chassis
    sim_set_load_pct(0, 10); // keep the pre-clamp 100W ask under the power rule
    seat_and_attach(0, 20000, 5000); // asks 100W, clamped to 60W = 3A
    MT_ASSERT_EQ(port_state(0), PORT_STATE_THROTTLED);
    MT_ASSERT_EQ(fan_policy_contract_ma(&tele.port[0]), 3000);
    MT_ASSERT_EQ(tele.total_mw, 6000);
    MT_ASSERT(!sim_fan());
}

static void test_zero_setting_disables_current_rule(void) {
    support_reset(360000);
    g_settings.fan_on_ma = 0;
    seat_and_attach(0, 5000, 5000); // 5A contract, 20W measured
    MT_ASSERT(!sim_fan());
}

static void test_hold_blocks_flapping(void) {
    support_reset(360000);
    seat_and_attach(0, 5000, 5000);
    MT_ASSERT(sim_fan());
    sim_detach(0);
    tick(2);
    MT_ASSERT(sim_fan()); // change within the hold is deferred
    tick_ms(FAN_MIN_HOLD_MS);
    MT_ASSERT(!sim_fan());
    sim_attach(0, 5000, 5000);
    tick(2);
    MT_ASSERT(!sim_fan()); // the off->on edge started its own hold
    tick_ms(FAN_MIN_HOLD_MS);
    MT_ASSERT(sim_fan());
}

static void test_manual_override_and_auto_resume(void) {
    support_reset(360000);
    seat_and_attach(0, 5000, 5000);
    MT_ASSERT(sim_fan());

    fan_policy_set_manual(false);
    MT_ASSERT(!sim_fan());
    MT_ASSERT(!fan_policy_auto());
    tick_ms(FAN_MIN_HOLD_MS + 100);
    MT_ASSERT(!sim_fan()); // policy is out of the loop

    fan_policy_set_auto(); // acts on the next tick, no hold
    tick(1);
    MT_ASSERT(sim_fan());
    MT_ASSERT(tele.fan_auto);
}

void run_fan_tests(void) {
    mt_run("fan: contract current derivation", test_contract_ma_derivation);
    mt_run("fan: chassis power hysteresis", test_power_hysteresis);
    mt_run("fan: 5A contract trips at low power",
           test_five_amp_contract_trips_at_low_power);
    mt_run("fan: 20V/5A contract trips with idle draw",
           test_high_voltage_five_amp_contract_trips_when_idle_draw);
    mt_run("fan: 3A contracts do not trip", test_three_amp_contracts_do_not_trip);
    mt_run("fan: 3.25A contract trips", test_just_over_three_amps_trips);
    mt_run("fan: tolerance tracks the bus reading", test_tolerance_tracks_bus_reading);
    mt_run("fan: stays on until both rules clear", test_stays_on_until_both_rules_clear);
    mt_run("fan: power rule holds after the contract ends",
           test_stays_on_while_power_rule_holds_after_contract_ends);
    mt_run("fan: throttled port counts its clamped contract",
           test_throttled_port_counts_its_clamped_contract);
    mt_run("fan: on_ma 0 disables the current rule",
           test_zero_setting_disables_current_rule);
    mt_run("fan: hold blocks flapping", test_hold_blocks_flapping);
    mt_run("fan: manual override, then auto resumes",
           test_manual_override_and_auto_resume);
}
