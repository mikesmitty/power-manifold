#include "budget.h"
#include "fan_policy.h"
#include "manifold.h"
#include "microtest.h"
#include "port_fsm.h"
#include "settings.h"
#include "tca9539.h"
#include "test_support.h"

// Protected ports (settings port_protect): power sharing settles the
// port's offer when a device plugs in and never changes it while the device
// runs. Also the PD hard-reset reports. All on gen-3 blades.

static void seat3(uint8_t slot) {
    sim_set_gen(slot, 3);
    sim_set_present(slot, true);
    tick(2);
}

static void plug(uint8_t slot, uint16_t mv, uint32_t ma) {
    sim_attach(slot, mv, ma);
    tick(2);
}

// The controller reboots; the backplane (the sim) stays as it was. Mirrors
// engine_main's start-up order (see test_warm.c).
static void warm_reboot(void) {
    budget_init(g_settings.budget_mw);
    port_fsm_init();
    bool warm = tca9539_attach();
    if (!warm) tca9539_init();
    fan_policy_init(g_settings.fan_auto != 0, (tca9539_outputs() >> TCA9539_FAN_BIT) & 1u);
    evt_clear();
    uint16_t inputs = 0;
    tca9539_read_inputs(&inputs);
    bool present[NUM_PORTS];
    for (uint8_t i = 0; i < NUM_PORTS; i++) present[i] = tca9539_present_from(inputs, i);
    uint8_t powered = warm ? (uint8_t)(tca9539_outputs() & 0x3F) : 0;
    port_fsm_boot_inventory(present, powered, now_ms);
}

static void test_claims_its_offer_at_plugin(void) {
    support_reset(360000);
    g_settings.port_protect = 1u << 0;
    seat3(0);
    MT_ASSERT_EQ(port_state(0), PORT_STATE_IDLE);
    MT_ASSERT_EQ(budget_port_reservation(0), BUDGET_BASE_RESERVE_MW); // nothing extra while empty

    plug(0, 9000, 2000);
    MT_ASSERT_EQ(port_state(0), PORT_STATE_ACTIVE);
    MT_ASSERT_EQ(budget_port_reservation(0), 100000); // everything 5 A up to 21 V offers
    MT_ASSERT_EQ(tele.port[0].contract_mw, 18000);    // the device's contract is what shows
    MT_ASSERT_EQ(evt_count(EVT_THROTTLE, 0), 0);

    sim_detach(0);
    tick(2);
    MT_ASSERT_EQ(budget_port_reservation(0), BUDGET_BASE_RESERVE_MW);
}

static void test_never_turned_down_for_another_port(void) {
    support_reset(180000);
    g_settings.port_protect = 1u << 5; // the lowest priority
    seat3(5);
    plug(5, 20000, 3000);
    uint32_t writes = sim_blade_config_writes(5);

    seat3(0);
    plug(0, 20000, 5000); // wants 100 W, 80 W free
    MT_ASSERT_EQ(port_state(5), PORT_STATE_ACTIVE);
    MT_ASSERT_EQ(budget_port_reservation(5), 100000);
    MT_ASSERT_EQ(sim_blade_config_writes(5), writes); // its offer was never touched
    MT_ASSERT_EQ(port_state(0), PORT_STATE_THROTTLED); // the newcomer takes what is left
    MT_ASSERT_EQ(budget_port_reservation(0), 80000);
}

static void test_device_asks_for_more_without_renegotiation(void) {
    support_reset(360000);
    g_settings.port_protect = 1u << 0;
    seat3(0);
    plug(0, 20000, 1000);
    uint32_t writes = sim_blade_config_writes(0);

    plug(0, 20000, 5000); // the device steps up within the offer
    MT_ASSERT_EQ(port_state(0), PORT_STATE_ACTIVE);
    MT_ASSERT_EQ(sim_contract_mw(0), 100000);
    MT_ASSERT_EQ(sim_blade_config_writes(0), writes);
    MT_ASSERT_EQ(evt_count(EVT_THROTTLE, 0), 0);
}

static void test_trimmed_once_at_plugin(void) {
    support_reset(120000);
    g_settings.port_priority[1] = 0; // a peer: nothing to turn down
    g_settings.port_protect = 1u << 0;
    seat3(1);
    plug(1, 20000, 3000); // 60 W
    seat3(0);
    plug(0, 20000, 5000); // 60 W left for an offer worth 100 W

    MT_ASSERT_EQ(port_state(0), PORT_STATE_ACTIVE);
    MT_ASSERT_EQ(budget_port_reservation(0), 60000);
    MT_ASSERT_EQ(sim_advertised_ma(0), 2857); // 60 W at the 21 V top of the table
    MT_ASSERT(evt_last(EVT_THROTTLE, 0) != NULL);
    uint32_t writes = sim_blade_config_writes(0);

    sim_detach(1); // power frees up; the running device keeps its offer
    tick(3);
    MT_ASSERT_EQ(sim_advertised_ma(0), 2857);
    MT_ASSERT_EQ(sim_blade_config_writes(0), writes);

    sim_detach(0); // unplugged: back to the full offer for the next device
    tick(2);
    MT_ASSERT_EQ(sim_advertised_ma(0), 5000);
    MT_ASSERT_EQ(budget_port_reservation(0), BUDGET_BASE_RESERVE_MW);
}

static void test_turns_down_lower_priority_at_plugin(void) {
    support_reset(120000);
    g_settings.port_protect = 1u << 0;
    seat3(1);
    plug(1, 20000, 3000); // 60 W, lower priority, not protected
    seat3(0);
    plug(0, 20000, 5000);

    MT_ASSERT_EQ(budget_port_reservation(0), 100000);
    MT_ASSERT_EQ(sim_advertised_ma(0), 5000);
    MT_ASSERT_EQ(port_state(1), PORT_STATE_THROTTLED);
    MT_ASSERT_EQ(budget_port_reservation(1), 20000);
}

static void test_setting_waits_for_the_next_device(void) {
    support_reset(360000);
    seat3(0);
    plug(0, 20000, 3000);
    MT_ASSERT_EQ(budget_port_reservation(0), 60000);

    g_settings.port_protect = 1u << 0;
    tick(2);
    MT_ASSERT_EQ(budget_port_reservation(0), 60000);

    sim_detach(0);
    tick(2);
    plug(0, 20000, 3000);
    MT_ASSERT_EQ(budget_port_reservation(0), 100000);
}

static void test_bus_ceiling_still_applies(void) {
    support_reset(360000);
    g_settings.port_protect = 1u << 0;
    seat3(0);
    plug(0, 20000, 5000);

    port_fsm_set_ceiling(3000); // the DC bus sags: a safety rule, for every port
    tick(2);
    MT_ASSERT_EQ(sim_advertised_ma(0), 3000);
    port_fsm_set_ceiling(0);
    tick(2);
    MT_ASSERT_EQ(sim_advertised_ma(0), 5000);
}

static void test_warm_restart_keeps_the_running_offer(void) {
    support_reset(120000);
    g_settings.port_priority[1] = 0;
    g_settings.port_protect = 1u << 0;
    seat3(1);
    plug(1, 20000, 3000);
    seat3(0);
    plug(0, 20000, 5000); // trimmed to 2857 mA
    uint32_t writes = sim_blade_config_writes(0);

    warm_reboot();
    tick(10); // adoption is staggered 50 ms apart
    MT_ASSERT_EQ(port_state(0), PORT_STATE_ACTIVE);
    MT_ASSERT_EQ(sim_advertised_ma(0), 2857); // not rewritten to the 5 A setting
    MT_ASSERT_EQ(sim_blade_config_writes(0), writes);
    MT_ASSERT_EQ(budget_port_reservation(0), 59997); // 2857 mA at 21 V
    MT_ASSERT_EQ(port_state(1), PORT_STATE_ACTIVE);
}

static void test_hard_resets_are_reported(void) {
    support_reset(360000);
    seat3(0);
    plug(0, 20000, 3000);

    sim_pd_hard_reset(0, true);
    tick(1);
    const engine_evt_t *e = evt_last(EVT_PD_RESET, 0);
    MT_ASSERT(e != NULL);
    MT_ASSERT_EQ(e->code, PD_RESET_RECEIVED);
    MT_ASSERT_EQ(e->arg, PD_RESET_NO_READV); // no new offer since the device plugged in

    g_settings.port_limit_ma[0] = 3000; // a new offer goes out
    tick(1);
    tick_ms(200);
    sim_pd_hard_reset(0, false);
    tick(1);
    e = evt_last(EVT_PD_RESET, 0);
    MT_ASSERT_EQ(e->code, PD_RESET_SENT);
    MT_ASSERT(e->arg >= 200 && e->arg <= 220);

    evt_clear();
    for (int n = 0; n < 6; n++) sim_pd_hard_reset(0, true); // a loop between two polls
    tick(1);
    MT_ASSERT_EQ(evt_count(EVT_PD_RESET, 0), 4);
}

static void test_counts_seen_before_the_probe_are_not_reported(void) {
    support_reset(360000);
    sim_set_gen(0, 3);
    sim_pd_hard_reset(0, true); // counted by the blade before the controller looked
    sim_set_present(0, true);
    tick(4);
    MT_ASSERT_EQ(evt_count(EVT_PD_RESET, 0), 0);
}

void run_protect_tests(void) {
    mt_run("protect: claims its offer when a device plugs in", test_claims_its_offer_at_plugin);
    mt_run("protect: never turned down for another port", test_never_turned_down_for_another_port);
    mt_run("protect: the device asks for more without renegotiation",
           test_device_asks_for_more_without_renegotiation);
    mt_run("protect: trimmed once at plug-in, then left alone", test_trimmed_once_at_plugin);
    mt_run("protect: lower priority is turned down at plug-in", test_turns_down_lower_priority_at_plugin);
    mt_run("protect: the setting waits for the next device", test_setting_waits_for_the_next_device);
    mt_run("protect: the bus ceiling still applies", test_bus_ceiling_still_applies);
    mt_run("protect: a warm restart keeps the running offer", test_warm_restart_keeps_the_running_offer);
    mt_run("pd reset: hard resets are reported", test_hard_resets_are_reported);
    mt_run("pd reset: counts before the probe are a baseline",
           test_counts_seen_before_the_probe_are_not_reported);
}
