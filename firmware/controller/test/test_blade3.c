#include "blade.h"
#include "blade_regs.h"
#include "budget.h"
#include "fan_policy.h"
#include "manifold.h"
#include "microtest.h"
#include "port_fsm.h"
#include "settings.h"
#include "tca9539.h"
#include "test_support.h"

// The port engine on gen-3 blades (the STM32G071 register file), alone and
// beside gen-2 blades. Timing as in test_port_fsm.c: one tick to enter
// PROBE, the probe lands in IDLE on the next, attach and contract changes
// one tick after the sim mutates.

static void seat3(uint8_t slot) {
    sim_set_gen(slot, 3);
    sim_set_present(slot, true);
}

// The controller reboots; the backplane (the sim) stays as it was. Mirrors
// engine_main's start-up order (see test_warm.c).
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

static void test_probe_configures_the_blade(void) {
    support_reset(360000);
    seat3(0);
    tick(2);
    MT_ASSERT_EQ(port_state(0), PORT_STATE_IDLE);
    MT_ASSERT_EQ(tele.port[0].gen, 3);
    MT_ASSERT(sim_en(0));
    MT_ASSERT(sim_blade_port_en(0));
    MT_ASSERT_EQ(sim_advertised_ma(0), 5000);
    MT_ASSERT_EQ(sim_advertised_mv(0), PORT_VOLT_MAX_MV);
    MT_ASSERT_EQ(sim_blade_config_writes(0), 1);
    MT_ASSERT_EQ(sim_blade_clear_count(0), 1); // the restart it booted with is cleared
    MT_ASSERT_EQ(sim_blade_faults(0), 0);
    MT_ASSERT_EQ(sim_src_cap_count(0), 0);    // nothing to ask: the blade advertises by itself
    MT_ASSERT_EQ(budget_port_reservation(0), BUDGET_BASE_RESERVE_MW);
}

static void test_port_is_dark_until_configured(void) {
    support_reset(360000);
    sim_set_gen(0, 3);
    sim_attach(0, 20000, 3000); // the sink is already plugged in when the blade is seated
    sim_set_present(0, true);
    tick(1);
    MT_ASSERT_EQ(port_state(0), PORT_STATE_PROBE);
    MT_ASSERT_EQ(sim_contract_mw(0), 0); // EN is up, but nobody has configured the port
    tick(2);
    MT_ASSERT_EQ(port_state(0), PORT_STATE_ACTIVE);
    MT_ASSERT_EQ(sim_contract_mw(0), 60000);
}

static void test_attach_contract_and_telemetry(void) {
    support_reset(360000);
    seat3(0);
    tick(2);
    sim_attach(0, 20000, 3000); // 60 W laptop
    tick(2);
    MT_ASSERT_EQ(port_state(0), PORT_STATE_ACTIVE);
    MT_ASSERT_EQ(budget_port_reservation(0), 60000);
    MT_ASSERT_EQ(tele.port[0].bus_mv, 20000);
    MT_ASSERT_EQ(tele.port[0].current_ma, 2400); // 80 % of the contract
    MT_ASSERT_EQ(tele.port[0].power_mw, 48000);
    MT_ASSERT_EQ(tele.port[0].selected_pdo, 5);
    const engine_evt_t *e = evt_last(EVT_CONTRACT, 0);
    MT_ASSERT(e != NULL);
    MT_ASSERT_EQ(e->arg, 60000);
    MT_ASSERT_EQ(e->code, 5);

    sim_detach(0);
    tick(2);
    MT_ASSERT_EQ(port_state(0), PORT_STATE_IDLE);
    MT_ASSERT_EQ(budget_port_reservation(0), BUDGET_BASE_RESERVE_MW);
    MT_ASSERT_EQ(tele.port[0].power_mw, 0);
}

static void test_pps_contract_at_21v(void) {
    support_reset(360000);
    seat3(0);
    tick(2);
    sim_attach(0, 21000, 5000); // the 21 V range advertises 4.75 A: 99.75 W
    tick(2);
    MT_ASSERT_EQ(port_state(0), PORT_STATE_ACTIVE);
    MT_ASSERT_EQ(budget_port_reservation(0), 99750); // exact: no 0.5 W register rounding
    MT_ASSERT_EQ(tele.port[0].selected_pdo, 7);
}

static void test_throttle_needs_no_src_cap(void) {
    support_reset(120000); // 120 W for two 100 W asks
    g_settings.port_priority[0] = 0;
    g_settings.port_priority[1] = 1;
    seat3(0);
    seat3(1);
    tick(2);
    sim_attach(1, 20000, 5000); // the lower-priority port fills the budget first
    tick(2);
    MT_ASSERT_EQ(port_state(1), PORT_STATE_ACTIVE);
    MT_ASSERT_EQ(budget_port_reservation(1), 100000);

    sim_attach(0, 20000, 5000);
    tick(3);
    MT_ASSERT_EQ(port_state(0), PORT_STATE_ACTIVE);
    MT_ASSERT_EQ(port_state(1), PORT_STATE_THROTTLED);
    MT_ASSERT(sim_advertised_ma(1) < 5000);
    MT_ASSERT_EQ(sim_src_cap_count(1), 0); // the new ceiling was written; the blade re-advertised
    MT_ASSERT(budget_port_reservation(0) + budget_port_reservation(1) <= 120000);
    MT_ASSERT_EQ(sim_contract_mw(1), budget_port_reservation(1));

    sim_detach(0);
    tick(3);
    MT_ASSERT_EQ(port_state(1), PORT_STATE_ACTIVE); // restored
    MT_ASSERT_EQ(sim_advertised_ma(1), 5000);
    MT_ASSERT_EQ(budget_port_reservation(1), 100000);
}

static void test_fault_is_cleared_after_the_controller_saw_it(void) {
    support_reset(360000);
    seat3(0);
    tick(2);
    sim_attach(0, 20000, 3000);
    tick(2);
    uint32_t en_changes = sim_en_change_count(0);

    sim_set_blade_fault(0, BLADE_FAULT_OT_PLUG); // the blade takes its port down and latches
    MT_ASSERT_EQ(sim_contract_mw(0), 0);
    tick(1);
    MT_ASSERT_EQ(port_state(0), PORT_STATE_FAULT);
    MT_ASSERT(!sim_en(0));
    MT_ASSERT_EQ(sim_en_change_count(0), en_changes + 1);
    MT_ASSERT_EQ(tele.port[0].fault_bits, PORT_FAULT_PLUG_HOT);
    const engine_evt_t *e = evt_last(EVT_FAULT, 0);
    MT_ASSERT(e != NULL);
    MT_ASSERT_EQ(e->code, PORT_FAULT_PLUG_HOT);
    MT_ASSERT_EQ(e->arg, PORT_FAULT_ARG_GEN3 | BLADE_FAULT_OT_PLUG);
    // the controller told the blade it has seen the fault; the condition is
    // still there, so the blade latched it straight back
    MT_ASSERT_EQ(sim_blade_clear_count(0), 2);
    MT_ASSERT(sim_blade_faults(0) & BLADE_FAULT_OT_PLUG);

    // still hot at the end of the cooldown: the re-probe finds it again
    tick_ms(5200);
    MT_ASSERT_EQ(port_state(0), PORT_STATE_FAULT);
    MT_ASSERT(evt_count(EVT_FAULT, 0) >= 2);

    // cooled down: the next re-probe clears the latch and the port comes back
    sim_set_blade_fault(0, 0);
    tick_ms(5200);
    MT_ASSERT_EQ(port_state(0), PORT_STATE_ACTIVE); // sink still attached
    MT_ASSERT_EQ(sim_blade_faults(0), 0);
    MT_ASSERT_EQ(budget_port_reservation(0), 60000);
}

static void test_ocp_trip_through_the_alert_line(void) {
    support_reset(360000);
    seat3(0);
    tick(2);
    sim_attach(0, 20000, 3000);
    tick(2);
    sim_trip_ocp(0);
    MT_ASSERT(sim_alert_asserted());
    tick(1);
    MT_ASSERT_EQ(port_state(0), PORT_STATE_FAULT);
    const engine_evt_t *e = evt_last(EVT_FAULT, 0);
    MT_ASSERT(e != NULL);
    MT_ASSERT_EQ(e->code, PORT_FAULT_OCP);
    MT_ASSERT_EQ(e->arg, PORT_FAULT_ARG_GEN3 | BLADE_FAULT_OCP_VBUS);
    MT_ASSERT(!sim_alert_asserted()); // a one-off trip: cleared, ALERT# released
    tick_ms(5100);
    tick(2);
    MT_ASSERT_EQ(port_state(0), PORT_STATE_ACTIVE);
}

static void test_blade_restart_reconfigures_without_an_en_cut(void) {
    support_reset(360000);
    seat3(0);
    tick(2);
    sim_attach(0, 20000, 3000);
    tick(2);
    MT_ASSERT_EQ(budget_port_reservation(0), 60000);
    uint32_t en_changes = sim_en_change_count(0);

    sim_blade_restart(0); // watchdog on the blade: configuration gone, port off
    tick(1);
    MT_ASSERT_EQ(port_state(0), PORT_STATE_PROBE);
    MT_ASSERT_EQ(budget_port_reservation(0), BUDGET_BASE_RESERVE_MW);
    MT_ASSERT_EQ(evt_count(EVT_FAULT, 0), 0); // not a fault
    tick(2);
    MT_ASSERT_EQ(port_state(0), PORT_STATE_ACTIVE);
    MT_ASSERT_EQ(budget_port_reservation(0), 60000);
    MT_ASSERT_EQ(sim_en_change_count(0), en_changes);
    MT_ASSERT_EQ(sim_blade_config_writes(0), 2);
    MT_ASSERT_EQ(sim_blade_faults(0), 0); // the restart flag went with the re-probe's clear
}

static void test_silent_blade_is_no_answer(void) {
    support_reset(360000);
    sim_set_gen(0, 3);
    sim_set_blade_ok(0, false);
    sim_set_present(0, true);
    tick(5); // 1 to enter probe + 3 attempts + 1 settle
    MT_ASSERT_EQ(port_state(0), PORT_STATE_FAULT);
    MT_ASSERT(!sim_en(0));
    const engine_evt_t *e = evt_last(EVT_PROBE_FAIL, 0);
    MT_ASSERT(e != NULL);
    MT_ASSERT_EQ(e->code, PROBE_FAIL_NONE);

    sim_set_blade_ok(0, true);
    tick_ms(5100);
    MT_ASSERT_EQ(port_state(0), PORT_STATE_IDLE);
    MT_ASSERT_EQ(tele.port[0].gen, 3);
}

static void test_warm_adoption_leaves_a_matching_config_alone(void) {
    support_reset(360000);
    seat3(0);
    tick(2);
    sim_attach(0, 20000, 3000);
    tick(2);
    MT_ASSERT_EQ(port_state(0), PORT_STATE_ACTIVE);
    uint32_t writes = sim_blade_config_writes(0);
    uint32_t en_changes = sim_en_change_count(0);

    MT_ASSERT_EQ(warm_reboot(), 0x01);
    tick(3);
    MT_ASSERT_EQ(port_state(0), PORT_STATE_ACTIVE);
    MT_ASSERT_EQ(budget_port_reservation(0), 60000);
    MT_ASSERT_EQ(sim_blade_config_writes(0), writes); // nothing rewritten
    MT_ASSERT_EQ(sim_blade_clear_count(0), 1);         // nothing cleared either
    MT_ASSERT_EQ(sim_en_change_count(0), en_changes);
    MT_ASSERT_EQ(tele.port[0].gen, 3);
}

static void test_warm_adoption_rewrites_a_changed_config(void) {
    support_reset(360000);
    seat3(0);
    tick(2);
    sim_attach(0, 20000, 5000);
    tick(2);
    MT_ASSERT_EQ(budget_port_reservation(0), 100000);
    uint32_t writes = sim_blade_config_writes(0);

    g_settings.port_limit_ma[0] = 3000; // the setting changed before the reboot took
    MT_ASSERT_EQ(warm_reboot(), 0x01);
    tick(3);
    MT_ASSERT_EQ(port_state(0), PORT_STATE_ACTIVE);
    MT_ASSERT_EQ(sim_blade_config_writes(0), writes + 1);
    MT_ASSERT_EQ(sim_advertised_ma(0), 3000);
    MT_ASSERT_EQ(budget_port_reservation(0), 60000); // the blade renegotiated on its own
}

static void test_warm_adoption_acts_on_a_latched_fault(void) {
    support_reset(360000);
    seat3(0);
    tick(2);
    sim_attach(0, 20000, 3000);
    tick(2);
    sim_set_blade_fault(0, BLADE_FAULT_CONV_OCP); // latched while nobody was watching
    sim_set_blade_fault(0, 0);                    // ...and the condition has passed

    MT_ASSERT_EQ(warm_reboot(), 0x01);
    tick(2);
    MT_ASSERT_EQ(port_state(0), PORT_STATE_FAULT);
    MT_ASSERT(!sim_en(0));
    const engine_evt_t *e = evt_last(EVT_FAULT, 0);
    MT_ASSERT(e != NULL);
    MT_ASSERT_EQ(e->code, PORT_FAULT_CONVERTER);
    MT_ASSERT_EQ(e->arg, PORT_FAULT_ARG_GEN3 | BLADE_FAULT_CONV_OCP);
    MT_ASSERT_EQ(sim_blade_faults(0), 0); // cleared once seen
    tick_ms(5100);
    tick(2);
    MT_ASSERT_EQ(port_state(0), PORT_STATE_ACTIVE);
}

static void test_limit_and_cap_commands(void) {
    support_reset(360000);
    seat3(0);
    tick(2);
    sim_attach(0, 20000, 5000);
    tick(2);
    MT_ASSERT_EQ(budget_port_reservation(0), 100000);

    g_settings.port_limit_ma[0] = 3000;
    engine_cmd_t c = {.op = CMD_PORT_LIMIT, .port = 0, .arg = 3000};
    port_fsm_cmd(0, &c);
    tick(2);
    MT_ASSERT_EQ(sim_advertised_ma(0), 3000);
    MT_ASSERT_EQ(sim_advertised_mv(0), PORT_VOLT_MAX_MV); // the cap rode along unchanged
    MT_ASSERT_EQ(budget_port_reservation(0), 60000);
    MT_ASSERT_EQ(sim_src_cap_count(0), 0);

    g_settings.port_max_mv[0] = 9000;
    c = (engine_cmd_t){.op = CMD_PORT_VOLT, .port = 0, .arg = 9000};
    port_fsm_cmd(0, &c);
    tick(2);
    MT_ASSERT_EQ(sim_advertised_mv(0), 9000);
    MT_ASSERT_EQ(sim_advertised_ma(0), 3000); // and the ceiling stayed
    MT_ASSERT_EQ(tele.port[0].bus_mv, 9000);
    MT_ASSERT_EQ(budget_port_reservation(0), 27000);

    c = (engine_cmd_t){.op = CMD_PORT_SRC_CAP, .port = 0};
    port_fsm_cmd(0, &c);
    c = (engine_cmd_t){.op = CMD_PORT_HARD_RESET, .port = 0};
    port_fsm_cmd(0, &c);
    MT_ASSERT_EQ(sim_src_cap_count(0), 1);
    MT_ASSERT_EQ(sim_hard_reset_count(0), 1);
}

static void test_mixed_chassis_sheds_across_generations(void) {
    support_reset(120000);
    g_settings.port_priority[0] = 0; // gen 2, the boss
    g_settings.port_priority[1] = 1; // gen 3
    sim_set_present(0, true);
    seat3(1);
    tick(2);
    MT_ASSERT_EQ(tele.port[0].gen, 2);
    MT_ASSERT_EQ(tele.port[1].gen, 3);

    sim_attach(1, 20000, 5000);
    tick(2);
    MT_ASSERT_EQ(budget_port_reservation(1), 100000);
    sim_attach(0, 20000, 5000);
    tick(3);
    MT_ASSERT_EQ(port_state(0), PORT_STATE_ACTIVE);
    MT_ASSERT_EQ(port_state(1), PORT_STATE_THROTTLED);
    MT_ASSERT(budget_port_reservation(0) + budget_port_reservation(1) <= 120000);
    MT_ASSERT_EQ(sim_src_cap_count(1), 0);

    // and the other way round: the gen-3 port outranks a gen-2 one
    support_reset(120000);
    g_settings.port_priority[0] = 1;
    g_settings.port_priority[1] = 0;
    sim_set_present(0, true);
    seat3(1);
    tick(2);
    sim_attach(0, 20000, 5000);
    tick(2);
    sim_attach(1, 20000, 5000);
    tick(3);
    MT_ASSERT_EQ(port_state(1), PORT_STATE_ACTIVE);
    MT_ASSERT_EQ(port_state(0), PORT_STATE_THROTTLED);
    MT_ASSERT_EQ(sim_src_cap_count(0), 1); // the gen-2 part had to be asked to re-advertise
}

static void test_a_pulled_blade_forgets_its_generation(void) {
    support_reset(360000);
    seat3(0);
    tick(2);
    MT_ASSERT_EQ(tele.port[0].gen, 3);
    sim_set_present(0, false);
    tick(1);
    MT_ASSERT_EQ(port_state(0), PORT_STATE_ABSENT);
    MT_ASSERT_EQ(tele.port[0].gen, 0);
    sim_set_gen(0, 2); // a gen-2 blade goes into the same slot
    sim_set_present(0, true);
    tick(2);
    MT_ASSERT_EQ(port_state(0), PORT_STATE_IDLE);
    MT_ASSERT_EQ(tele.port[0].gen, 2);
    MT_ASSERT_EQ(sim_ina_alert_ma(0), 6250);
}

void run_blade3_tests(void) {
    mt_run("blade3: probe configures the blade", test_probe_configures_the_blade);
    mt_run("blade3: port is dark until configured", test_port_is_dark_until_configured);
    mt_run("blade3: attach, contract, telemetry, detach", test_attach_contract_and_telemetry);
    mt_run("blade3: PPS contract at 21 V", test_pps_contract_at_21v);
    mt_run("blade3: throttle needs no src_cap", test_throttle_needs_no_src_cap);
    mt_run("blade3: fault is cleared after the controller saw it", test_fault_is_cleared_after_the_controller_saw_it);
    mt_run("blade3: OCP trip through the alert line", test_ocp_trip_through_the_alert_line);
    mt_run("blade3: blade restart reconfigures without an EN cut", test_blade_restart_reconfigures_without_an_en_cut);
    mt_run("blade3: silent blade is 'no answer'", test_silent_blade_is_no_answer);
    mt_run("blade3: warm adoption leaves a matching config alone", test_warm_adoption_leaves_a_matching_config_alone);
    mt_run("blade3: warm adoption rewrites a changed config", test_warm_adoption_rewrites_a_changed_config);
    mt_run("blade3: warm adoption acts on a latched fault", test_warm_adoption_acts_on_a_latched_fault);
    mt_run("blade3: limit and cap commands", test_limit_and_cap_commands);
    mt_run("blade3: mixed chassis sheds across generations", test_mixed_chassis_sheds_across_generations);
    mt_run("blade3: a pulled blade forgets its generation", test_a_pulled_blade_forgets_its_generation);
}
