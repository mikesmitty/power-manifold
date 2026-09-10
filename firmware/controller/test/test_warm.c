#include "budget.h"
#include "fan_policy.h"
#include "manifold.h"
#include "microtest.h"
#include "port_fsm.h"
#include "settings.h"
#include "tca9539.h"
#include "test_support.h"

// Warm start: the controller reboots while the backplane's 5 V stays up, so
// the expander keeps its registers and every powered blade stays powered.
// The engine adopts that state instead of resetting it (engine.c); these
// tests drive the same sequence against the sim.

// The controller reboots; the backplane (the sim) stays as it was. Mirrors
// engine_main's start-up order. Returns the adopted set.
static uint8_t warm_reboot(void) {
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
    return port_fsm_boot_inventory(present, powered, now_ms);
}

static void test_cold_after_power_cycle(void) {
    support_reset(360000);
    MT_ASSERT(tca9539_attach()); // programmed by the fixture's cold init: adoptable
    sim_reset();                 // power cycle
    MT_ASSERT(!tca9539_attach()); // power-on state: nothing to adopt
    MT_ASSERT(tca9539_init());
    MT_ASSERT(tca9539_attach());
    MT_ASSERT_EQ(tca9539_outputs(), 0);
}

static void test_adopts_powered_ports(void) {
    support_reset(360000);
    for (uint8_t i = 0; i < 3; i++) sim_set_present(i, true);
    tick(2);
    sim_attach(0, 20000, 3000); // 60 W laptop
    sim_attach(2, 9000, 2000);  // 18 W phone
    tick(2);
    MT_ASSERT_EQ(port_state(0), PORT_STATE_ACTIVE);
    MT_ASSERT_EQ(budget_port_reservation(0), 60000);
    uint32_t en0 = sim_en_change_count(0), en1 = sim_en_change_count(1), en2 = sim_en_change_count(2);
    uint32_t caps0 = sim_src_cap_count(0), caps2 = sim_src_cap_count(2);

    MT_ASSERT_EQ(warm_reboot(), 0x07);
    tick(1);
    MT_ASSERT_EQ(port_state(0), PORT_STATE_PROBE);
    MT_ASSERT(sim_en(0));
    tick(1);
    MT_ASSERT_EQ(port_state(0), PORT_STATE_IDLE);
    tick(1);
    MT_ASSERT_EQ(port_state(0), PORT_STATE_ACTIVE);
    MT_ASSERT_EQ(port_state(1), PORT_STATE_ABSENT); // adopted in turn, 50 ms apart
    tick_ms(150);
    MT_ASSERT_EQ(port_state(1), PORT_STATE_IDLE);
    MT_ASSERT_EQ(port_state(2), PORT_STATE_ACTIVE);
    // no power cut, no renegotiation: the contracts rode through
    MT_ASSERT_EQ(sim_en_change_count(0), en0);
    MT_ASSERT_EQ(sim_en_change_count(1), en1);
    MT_ASSERT_EQ(sim_en_change_count(2), en2);
    MT_ASSERT_EQ(sim_src_cap_count(0), caps0);
    MT_ASSERT_EQ(sim_src_cap_count(2), caps2);
    MT_ASSERT_EQ(sim_contract_mw(0), 60000);
    MT_ASSERT_EQ(budget_port_reservation(0), 60000);
    MT_ASSERT_EQ(budget_port_reservation(1), BUDGET_BASE_RESERVE_MW);
    MT_ASSERT_EQ(budget_port_reservation(2), 18000);
    MT_ASSERT_EQ(tele.port[0].bus_mv, 20000);
    const engine_evt_t *e = evt_last(EVT_CONTRACT, 0);
    MT_ASSERT(e != NULL);
    MT_ASSERT_EQ(e->arg, 60000);
    MT_ASSERT_EQ(evt_count(EVT_FAULT, 0xFF), 0);
    MT_ASSERT_EQ(evt_count(EVT_PROBE_FAIL, 0xFF), 0);
}

static void test_adopted_skip_stagger(void) {
    support_reset(360000);
    sim_set_present(0, true);
    sim_set_present(1, true);
    tick(2);
    MT_ASSERT(sim_en(1));
    // two more blades the old firmware never saw (seated with the chassis
    // off, then a reboot before the first presence read: same thing)
    sim_set_present(2, true);
    sim_set_present(3, true);
    uint32_t en0 = sim_en_change_count(0), en1 = sim_en_change_count(1);

    MT_ASSERT_EQ(warm_reboot(), 0x03);
    tick(2);
    MT_ASSERT_EQ(port_state(0), PORT_STATE_IDLE);   // adopted at once...
    MT_ASSERT_EQ(port_state(1), PORT_STATE_ABSENT); // ...port 2 at t = 50
    MT_ASSERT_EQ(port_state(2), PORT_STATE_ABSENT); // the cold queue opens after both: t = 100
    tick_ms(100); // t = 120
    MT_ASSERT_EQ(port_state(1), PORT_STATE_IDLE);
    MT_ASSERT_EQ(port_state(2), PORT_STATE_IDLE);
    MT_ASSERT_EQ(port_state(3), PORT_STATE_ABSENT); // its boot slot is t = 350
    tick_ms(250); // t = 370
    MT_ASSERT_EQ(port_state(3), PORT_STATE_IDLE);
    MT_ASSERT_EQ(sim_en_change_count(0), en0);
    MT_ASSERT_EQ(sim_en_change_count(1), en1);
}

static void test_boot_policy_switches_off(void) {
    support_reset(360000);
    sim_set_present(0, true);
    sim_set_present(1, true);
    sim_set_present(2, true);
    tick(2);
    g_settings.port_boot[0] = PORT_BOOT_OFF;
    g_settings.port_boot[1] = PORT_BOOT_LAST;
    g_settings.port_off_mask = 1u << 1; // port 2 was switched off before the reboot
    g_settings.port_boot[2] = PORT_BOOT_LAST; // port 3 was not

    MT_ASSERT_EQ(warm_reboot(), 0x04);
    MT_ASSERT(!sim_en(0)); // dropped in the inventory, before the first tick
    MT_ASSERT(!sim_en(1));
    MT_ASSERT(sim_en(2));
    tick(3);
    MT_ASSERT_EQ(port_state(0), PORT_STATE_DISABLED);
    MT_ASSERT_EQ(port_state(1), PORT_STATE_DISABLED);
    MT_ASSERT_EQ(port_state(2), PORT_STATE_IDLE);
    MT_ASSERT_EQ(budget_port_reservation(0), 0);
    const engine_evt_t *e = evt_last(EVT_STATE_CHANGE, 0);
    MT_ASSERT(e != NULL);
    MT_ASSERT_EQ(e->code, PORT_STATE_DISABLED);
}

static void test_stale_en_dropped(void) {
    support_reset(360000);
    sim_set_present(0, true);
    tick(2);
    // EN left on with no blade behind it (nothing the FSM does, but the
    // expander could hold it after a brown-out on the blade side)
    sim_set_present(0, false);
    tca9539_set_en(0, true);
    MT_ASSERT(sim_en(0));
    MT_ASSERT_EQ(warm_reboot(), 0);
    MT_ASSERT(!sim_en(0));
    tick(2);
    MT_ASSERT_EQ(port_state(0), PORT_STATE_ABSENT);
}

static void test_latched_ocp_is_a_fault(void) {
    support_reset(360000);
    sim_set_present(0, true);
    tick(2);
    sim_attach(0, 20000, 3000);
    tick(2);
    sim_trip_ocp(0); // latched while nobody was watching
    MT_ASSERT_EQ(warm_reboot(), 0x01);
    tick(2);
    MT_ASSERT_EQ(port_state(0), PORT_STATE_FAULT);
    MT_ASSERT(!sim_en(0));
    MT_ASSERT_EQ(budget_port_reservation(0), 0);
    const engine_evt_t *e = evt_last(EVT_FAULT, 0);
    MT_ASSERT(e != NULL);
    MT_ASSERT_EQ(e->arg, 1); // INA226 alert flagged
    tick_ms(5100); // the usual cooldown and (cold) re-probe
    tick(2);
    MT_ASSERT_EQ(port_state(0), PORT_STATE_ACTIVE);
}

static void test_mpq_fault_bits_are_a_fault(void) {
    support_reset(360000);
    sim_set_present(0, true);
    tick(2);
    sim_attach(0, 20000, 3000);
    tick(2);
    sim_set_mpq_fault(0, MPQ_FAULT_OTW1);
    MT_ASSERT_EQ(warm_reboot(), 0x01);
    tick(2);
    MT_ASSERT_EQ(port_state(0), PORT_STATE_FAULT);
    MT_ASSERT(!sim_en(0));
    const engine_evt_t *e = evt_last(EVT_FAULT, 0);
    MT_ASSERT(e != NULL);
    MT_ASSERT(e->code & MPQ_FAULT_OTW1);
}

static void test_changed_settings_reconfigure(void) {
    support_reset(360000);
    sim_set_present(0, true);
    sim_set_present(1, true);
    tick(2);
    sim_attach(0, 20000, 3000);
    tick(2);
    MT_ASSERT_EQ(sim_contract_mw(0), 60000);
    uint32_t caps0 = sim_src_cap_count(0), caps1 = sim_src_cap_count(1);
    uint32_t en0 = sim_en_change_count(0);
    // settings changed underneath (an import, say): the adopted blade is
    // brought in line and, with a sink attached, re-advertised
    g_settings.port_limit_ma[0] = 1000;
    g_settings.port_max_mv[1] = 9000;
    MT_ASSERT_EQ(warm_reboot(), 0x03);
    tick_ms(100);
    MT_ASSERT_EQ(port_state(0), PORT_STATE_ACTIVE);
    MT_ASSERT_EQ(sim_advertised_ma(0), 1000);
    MT_ASSERT_EQ(sim_src_cap_count(0), caps0 + 1);
    MT_ASSERT_EQ(sim_contract_mw(0), 20000); // 20 V x 1 A
    MT_ASSERT_EQ(budget_port_reservation(0), 20000);
    MT_ASSERT_EQ(sim_en_change_count(0), en0);
    // the idle one takes the new cap without a src_cap: nobody to tell
    MT_ASSERT_EQ(port_state(1), PORT_STATE_IDLE);
    MT_ASSERT_EQ(sim_advertised_mv(1), 9000);
    MT_ASSERT_EQ(sim_src_cap_count(1), caps1);
}

static void test_silent_blade_switched_off(void) {
    support_reset(360000);
    sim_set_present(0, true);
    tick(2);
    sim_set_probe_ok(0, false, true); // INA226 not answering: cannot be supervised
    MT_ASSERT_EQ(warm_reboot(), 0x01);
    tick(5);
    MT_ASSERT_EQ(port_state(0), PORT_STATE_FAULT);
    MT_ASSERT(!sim_en(0));
    const engine_evt_t *e = evt_last(EVT_PROBE_FAIL, 0);
    MT_ASSERT(e != NULL);
    MT_ASSERT_EQ(e->code, 2); // PROBE_FAIL_INA226
}

static void test_smaller_budget_throttles_not_cuts(void) {
    support_reset(360000);
    sim_set_present(0, true);
    sim_set_present(1, true);
    tick(2);
    sim_attach(0, 20000, 3000);
    sim_attach(1, 20000, 3000);
    tick(2);
    MT_ASSERT_EQ(budget_port_reservation(1), 60000);
    uint32_t en1 = sim_en_change_count(1);
    g_settings.budget_mw = 70000; // the budget no longer holds both
    MT_ASSERT_EQ(warm_reboot(), 0x03);
    tick_ms(100);
    MT_ASSERT_EQ(port_state(0), PORT_STATE_ACTIVE); // priority 0 keeps its 60 W, as at a cold boot
    MT_ASSERT_EQ(budget_port_reservation(0), 60000);
    MT_ASSERT_EQ(port_state(1), PORT_STATE_THROTTLED); // the other is clamped, not cut
    MT_ASSERT_EQ(budget_port_reservation(1), BUDGET_BASE_RESERVE_MW);
    MT_ASSERT(sim_en(1));
    MT_ASSERT_EQ(sim_en_change_count(1), en1);
    MT_ASSERT(budget_reserved() <= 75000); // base reserves are never refused
}

static void test_fan_state_adopted(void) {
    support_reset(360000);
    sim_set_present(0, true);
    tick(2);
    sim_attach(0, 20000, 5000); // 5 A contract: the current rule wants the fan
    tick(2);
    MT_ASSERT(sim_fan());
    sim_detach(0); // ...and now nothing does
    MT_ASSERT_EQ(warm_reboot(), 0x01);
    MT_ASSERT(sim_fan()); // adopted as found
    tick(3);
    MT_ASSERT(!sim_fan()); // the policy sees it is on and switches it off
    MT_ASSERT(!tele.fan_on);
}

void run_warm_tests(void) {
    mt_run("warm: nothing to adopt after a power cycle", test_cold_after_power_cycle);
    mt_run("warm: powered ports are adopted without an EN cut", test_adopts_powered_ports);
    mt_run("warm: adopted ports first at a quick pace, cold ones staggered after", test_adopted_skip_stagger);
    mt_run("warm: boot policy off/last switches an adopted port off", test_boot_policy_switches_off);
    mt_run("warm: EN with no blade behind it is dropped", test_stale_en_dropped);
    mt_run("warm: a latched INA226 alert is a fault", test_latched_ocp_is_a_fault);
    mt_run("warm: MPQ4242 fault bits are a fault", test_mpq_fault_bits_are_a_fault);
    mt_run("warm: changed settings reconfigure and re-advertise", test_changed_settings_reconfigure);
    mt_run("warm: a silent blade is switched off", test_silent_blade_switched_off);
    mt_run("warm: a smaller budget throttles, never cuts", test_smaller_budget_throttles_not_cuts);
    mt_run("warm: the fan state is adopted", test_fan_state_adopted);
}
