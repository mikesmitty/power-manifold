#include <string.h>

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
    MT_ASSERT_EQ(sim_advertised_w(0), 100);
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
    sim_attach(0, 21000, 5000); // the blade holds a PPS sink to 100 W: 4.761 A at 21 V
    tick(2);
    MT_ASSERT_EQ(port_state(0), PORT_STATE_ACTIVE);
    MT_ASSERT_EQ(budget_port_reservation(0), 99981); // exact: no 0.5 W register rounding
    MT_ASSERT_EQ(tele.port[0].selected_pdo, 7);
    MT_ASSERT_EQ(tele.port[0].contract, PORT_CONTRACT_PPS);

    sim_detach(0);
    tick(2);
    MT_ASSERT_EQ(tele.port[0].contract, PORT_CONTRACT_NONE);
    sim_attach(0, 20000, 3000);
    tick(2);
    MT_ASSERT_EQ(tele.port[0].contract, PORT_CONTRACT_FIXED);
}

// A sink on the AVS or PPS object moves its own voltage, 0.1 V at a time in
// a sweep. The budget follows every step; only a different object is a
// contract event.
static void test_moves_inside_an_apdo_are_not_events(void) {
    support_reset(360000);
    seat3(0);
    tick(2);
    sim_set_apdo(0, PORT_CONTRACT_AVS);
    sim_attach(0, 9000, 5000);
    tick(2);
    MT_ASSERT_EQ(tele.port[0].contract, PORT_CONTRACT_AVS);
    MT_ASSERT_EQ(evt_count(EVT_CONTRACT, 0), 1);
    const engine_evt_t *e = evt_last(EVT_CONTRACT, 0);
    MT_ASSERT_EQ(e->code, 6);
    MT_ASSERT_EQ(e->arg, 45000);

    for (uint16_t mv = 9100; mv <= 20000; mv += 100) { // up, one request a tick
        sim_attach(0, mv, 5000);
        tick(1);
    }
    for (uint16_t mv = 19900; mv >= 12000; mv -= 100) { // and down
        sim_attach(0, mv, 5000);
        tick(1);
    }
    tick(1);
    MT_ASSERT_EQ(budget_port_reservation(0), 60000); // 12 V at 5 A
    MT_ASSERT_EQ(evt_count(EVT_CONTRACT, 0), 1);

    sim_set_apdo(0, PORT_CONTRACT_FIXED); // the fixed 12 V object
    sim_attach(0, 12000, 3000);
    tick(2);
    MT_ASSERT_EQ(evt_count(EVT_CONTRACT, 0), 2);
    MT_ASSERT_EQ(evt_last(EVT_CONTRACT, 0)->code, 3);
    MT_ASSERT_EQ(evt_last(EVT_CONTRACT, 0)->arg, 36000);

    sim_set_apdo(0, PORT_CONTRACT_PPS); // back to an adjustable object: a new one
    sim_attach(0, 11000, 3000);
    tick(2);
    MT_ASSERT_EQ(evt_count(EVT_CONTRACT, 0), 3);
    MT_ASSERT_EQ(evt_last(EVT_CONTRACT, 0)->code, 7);
    sim_attach(0, 11020, 3000);
    tick(2);
    MT_ASSERT_EQ(budget_port_reservation(0), 33060);
    MT_ASSERT_EQ(evt_count(EVT_CONTRACT, 0), 3);

    // the same object again after the sink was unplugged is a new contract
    sim_detach(0);
    tick(2);
    evt_clear();
    sim_attach(0, 11020, 3000);
    tick(2);
    MT_ASSERT_EQ(evt_count(EVT_CONTRACT, 0), 1);
    MT_ASSERT_EQ(evt_last(EVT_CONTRACT, 0)->code, 7);
}

// With contract_steps on, every step inside the object is an event too
static void test_contract_steps_makes_every_move_an_event(void) {
    support_reset(360000);
    g_settings.contract_steps = 1;
    seat3(0);
    tick(2);
    sim_set_apdo(0, PORT_CONTRACT_AVS);
    sim_attach(0, 9000, 5000);
    tick(2);
    for (uint16_t mv = 9100; mv <= 12000; mv += 100) {
        sim_attach(0, mv, 5000);
        tick(1);
    }
    tick(1);
    MT_ASSERT_EQ(evt_count(EVT_CONTRACT, 0), 31);
    MT_ASSERT_EQ(evt_last(EVT_CONTRACT, 0)->code, 6);
    MT_ASSERT_EQ(evt_last(EVT_CONTRACT, 0)->arg, 60000);

    g_settings.contract_steps = 0; // takes effect at the next step
    sim_attach(0, 12100, 5000);
    tick(2);
    MT_ASSERT_EQ(budget_port_reservation(0), 60500);
    MT_ASSERT_EQ(evt_count(EVT_CONTRACT, 0), 31);
}

// On a fixed object a new current is a new contract, as before
static void test_a_fixed_objects_new_current_is_an_event(void) {
    support_reset(360000);
    seat3(0);
    tick(2);
    sim_attach(0, 20000, 3000);
    tick(2);
    sim_attach(0, 20000, 1500);
    tick(2);
    MT_ASSERT_EQ(evt_count(EVT_CONTRACT, 0), 2);
    MT_ASSERT_EQ(evt_last(EVT_CONTRACT, 0)->code, 5);
    MT_ASSERT_EQ(evt_last(EVT_CONTRACT, 0)->arg, 30000);
}

// The budget turns a port down in watts: the sink gets the same power
// whichever voltage it picks, up to 5 A
static void test_a_throttled_port_offers_its_watts_at_every_voltage(void) {
    support_reset(120000);
    g_settings.port_priority[0] = 0;
    g_settings.port_priority[1] = 1;
    seat3(0);
    seat3(1);
    tick(2);
    sim_attach(1, 9000, 5000); // 45 W at 9 V
    tick(2);
    sim_attach(0, 20000, 5000); // 100 W: port 2 keeps what is left
    tick(3);
    MT_ASSERT_EQ(port_state(1), PORT_STATE_THROTTLED);
    MT_ASSERT_EQ(sim_advertised_w(1), 20);
    MT_ASSERT_EQ(sim_contract_mw(1), 19998); // 20 W at 9 V: 2.222 A, not 20 W at 20 V's 1 A
    MT_ASSERT(budget_reserved() <= 120000);
}

// While the DC bus sags every port is held to 60 W, whichever voltage its
// sink uses
static void test_the_bus_cap_is_a_power(void) {
    support_reset(600000);
    seat3(0);
    tick(2);
    sim_attach(0, 9000, 5000);
    tick(2);
    MT_ASSERT_EQ(sim_contract_mw(0), 45000);
    port_fsm_set_ceiling(60);
    tick(2);
    MT_ASSERT_EQ(sim_advertised_w(0), 60);
    MT_ASSERT_EQ(sim_contract_mw(0), 45000); // 9 V at 5 A is within 60 W: nothing to give up
    sim_detach(0);
    sim_attach(0, 20000, 5000);
    tick(2);
    MT_ASSERT_EQ(sim_contract_mw(0), 60000);
    port_fsm_set_ceiling(0);
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
    MT_ASSERT(sim_advertised_w(1) < 100);
    MT_ASSERT_EQ(sim_src_cap_count(1), 0); // the new ceiling was written; the blade re-advertised
    MT_ASSERT(budget_port_reservation(0) + budget_port_reservation(1) <= 120000);
    MT_ASSERT_EQ(sim_contract_mw(1), budget_port_reservation(1));

    sim_detach(0);
    tick(3);
    MT_ASSERT_EQ(port_state(1), PORT_STATE_ACTIVE); // restored
    MT_ASSERT_EQ(sim_advertised_w(1), 100);
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

    // still hot at the end of each cooldown: the blade holds the fault, so
    // the port stays dark and nothing new is reported; the latch is cleared
    // for the next look
    int states = evt_count(EVT_STATE_CHANGE, 0);
    tick_ms(5200);
    MT_ASSERT_EQ(port_state(0), PORT_STATE_FAULT);
    MT_ASSERT_EQ(sim_blade_clear_count(0), 3);
    tick_ms(5200);
    MT_ASSERT_EQ(port_state(0), PORT_STATE_FAULT);
    MT_ASSERT_EQ(sim_blade_clear_count(0), 4);
    MT_ASSERT(!sim_en(0));
    MT_ASSERT_EQ(sim_en_change_count(0), en_changes + 1);
    MT_ASSERT_EQ(evt_count(EVT_FAULT, 0), 1);
    MT_ASSERT_EQ(evt_count(EVT_STATE_CHANGE, 0), states);

    // cooled down: the next look still finds what the blade latched before
    // it cooled, and the one after finds the latch clear and tries the port
    sim_set_blade_fault(0, 0);
    tick_ms(5200);
    MT_ASSERT_EQ(port_state(0), PORT_STATE_FAULT);
    tick_ms(5200);
    MT_ASSERT_EQ(port_state(0), PORT_STATE_ACTIVE); // sink still attached
    MT_ASSERT_EQ(evt_count(EVT_FAULT, 0), 1);
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

    g_settings.port_limit_w[0] = 60; // the setting changed before the reboot took
    MT_ASSERT_EQ(warm_reboot(), 0x01);
    tick(3);
    MT_ASSERT_EQ(port_state(0), PORT_STATE_ACTIVE);
    MT_ASSERT_EQ(sim_blade_config_writes(0), writes + 1);
    MT_ASSERT_EQ(sim_advertised_w(0), 60);
    MT_ASSERT_EQ(budget_port_reservation(0), 60000); // the blade renegotiated on its own
}

static void test_warm_adoption_acts_on_a_latched_fault(void) {
    support_reset(360000);
    seat3(0);
    tick(2);
    sim_attach(0, 20000, 3000);
    tick(2);
    sim_set_blade_fault(0, BLADE_FAULT_CONV_OCP); // latched while the controller was down
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

    g_settings.port_limit_w[0] = 60;
    engine_cmd_t c = {.op = CMD_PORT_LIMIT, .port = 0, .arg = 60};
    port_fsm_cmd(0, &c);
    tick(2);
    MT_ASSERT_EQ(sim_advertised_w(0), 60);
    MT_ASSERT_EQ(sim_advertised_mv(0), PORT_VOLT_MAX_MV); // the cap rode along unchanged
    MT_ASSERT_EQ(budget_port_reservation(0), 60000);
    MT_ASSERT_EQ(sim_src_cap_count(0), 0);

    g_settings.port_max_mv[0] = 9000;
    c = (engine_cmd_t){.op = CMD_PORT_VOLT, .port = 0, .arg = 9000};
    port_fsm_cmd(0, &c);
    tick(2);
    MT_ASSERT_EQ(sim_advertised_mv(0), 9000);
    MT_ASSERT_EQ(sim_advertised_w(0), 60); // and the power limit stayed
    MT_ASSERT_EQ(tele.port[0].bus_mv, 9000);
    MT_ASSERT_EQ(budget_port_reservation(0), 45000); // 60 W at 9 V would be 6.67 A: 5 A is the most

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

// Switched off, a gen-3 blade is still read: its temperatures stay in
// telemetry while it cools, survive a few missed reads, and go once it has
// stopped answering or left the slot.
static void test_switched_off_blade_is_still_read(void) {
    support_reset(360000);
    seat3(0);
    tick(2);
    sim_attach(0, 20000, 3000);
    sim_set_temps(0, 800, 600);
    tick(2);
    engine_cmd_t c = {.op = CMD_PORT_DISABLE, .port = 0};
    port_fsm_cmd(0, &c);
    tick(1);
    MT_ASSERT_EQ(port_state(0), PORT_STATE_DISABLED);
    MT_ASSERT(!sim_en(0));
    MT_ASSERT_EQ(tele.port[0].temp_conv_dc, 800);
    MT_ASSERT_EQ(tele.port[0].temp_plug_dc, 600);

    sim_set_temps(0, 500, 400); // cooling
    tick(1);
    MT_ASSERT_EQ(tele.port[0].temp_conv_dc, 500);
    MT_ASSERT_EQ(tele.port[0].temp_plug_dc, 400);

    sim_set_blade_ok(0, false); // a few missed reads: the last values stand
    tick(3);
    MT_ASSERT_EQ(tele.port[0].temp_conv_dc, 500);
    tick(20); // not answering at all: nothing to show
    MT_ASSERT_EQ(tele.port[0].temp_conv_dc, PORT_TEMP_NONE);
    sim_set_blade_ok(0, true);
    tick(1);
    MT_ASSERT_EQ(tele.port[0].temp_conv_dc, 500);

    sim_set_present(0, false); // removed: the readings go with the blade
    tick(1);
    MT_ASSERT_EQ(port_state(0), PORT_STATE_ABSENT);
    MT_ASSERT_EQ(tele.port[0].temp_conv_dc, PORT_TEMP_NONE);
    MT_ASSERT_EQ(tele.port[0].gen, 0);
}

// The blade's thermometers are reported in telemetry: modelled from the
// load by the sim, pinned by the script, an open NTC passed through as
// "none", still read while the port is switched off; a gen-2 blade has no
// reading.
static void test_thermometers_reach_telemetry(void) {
    support_reset(360000);
    seat3(0);
    sim_set_present(1, true); // gen 2
    tick(3); // probe lands in IDLE, the first poll follows
    MT_ASSERT_EQ(tele.port[0].temp_conv_dc, 250); // idle: room temperature
    MT_ASSERT_EQ(tele.port[0].temp_plug_dc, 250);
    MT_ASSERT_EQ(tele.port[0].temp_mcu_dc, 300);
    MT_ASSERT_EQ(tele.port[1].temp_conv_dc, PORT_TEMP_NONE);
    MT_ASSERT_EQ(tele.port[1].temp_plug_dc, PORT_TEMP_NONE);
    MT_ASSERT_EQ(tele.port[1].temp_mcu_dc, PORT_TEMP_NONE);

    sim_attach(0, 20000, 5000); // 100 W, drawn at 80 %: 80 W, 4 A
    tick(2);
    MT_ASSERT_EQ(tele.port[0].temp_conv_dc, 450);
    MT_ASSERT_EQ(tele.port[0].temp_plug_dc, 450);

    sim_set_temps(0, 985, PORT_TEMP_NONE); // pinned: the receptacle NTC has fallen off
    tick(1);
    MT_ASSERT_EQ(tele.port[0].temp_conv_dc, 985);
    MT_ASSERT_EQ(tele.port[0].temp_plug_dc, PORT_TEMP_NONE);

    engine_cmd_t c = {.op = CMD_PORT_DISABLE, .port = 0};
    port_fsm_cmd(0, &c);
    tick(1);
    MT_ASSERT_EQ(port_state(0), PORT_STATE_DISABLED);
    MT_ASSERT_EQ(tele.port[0].temp_conv_dc, 985);
    MT_ASSERT_EQ(tele.port[0].temp_plug_dc, PORT_TEMP_NONE);

    sim_model_temps(0);
    c.op = CMD_PORT_ENABLE;
    port_fsm_cmd(0, &c);
    tick(3);
    MT_ASSERT_EQ(port_state(0), PORT_STATE_ACTIVE);
    MT_ASSERT_EQ(tele.port[0].temp_conv_dc, 450);

    // the formatter every surface prints them with
    char text[8];
    port_temp_text(text, sizeof(text), 985, "null");
    MT_ASSERT(!strcmp(text, "98.5"));
    port_temp_text(text, sizeof(text), -5, "null"); // -0.5: the sign is not in the integer part
    MT_ASSERT(!strcmp(text, "-0.5"));
    port_temp_text(text, sizeof(text), -400, "null");
    MT_ASSERT(!strcmp(text, "-40.0"));
    port_temp_text(text, sizeof(text), PORT_TEMP_NONE, "null");
    MT_ASSERT(!strcmp(text, "null"));
}

void run_blade3_tests(void) {
    mt_run("blade3: probe configures the blade", test_probe_configures_the_blade);
    mt_run("blade3: port is dark until configured", test_port_is_dark_until_configured);
    mt_run("blade3: attach, contract, telemetry, detach", test_attach_contract_and_telemetry);
    mt_run("blade3: PPS contract at 21 V", test_pps_contract_at_21v);
    mt_run("blade3: moves inside an APDO are not events", test_moves_inside_an_apdo_are_not_events);
    mt_run("blade3: contract_steps makes every move inside an APDO an event", test_contract_steps_makes_every_move_an_event);
    mt_run("blade3: a fixed object's new current is an event", test_a_fixed_objects_new_current_is_an_event);
    mt_run("blade3: a throttled port offers its watts at every voltage", test_a_throttled_port_offers_its_watts_at_every_voltage);
    mt_run("blade3: the bus cap is a power", test_the_bus_cap_is_a_power);
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
    mt_run("blade3: thermometers reach telemetry", test_thermometers_reach_telemetry);
    mt_run("blade3: a switched-off blade is still read", test_switched_off_blade_is_still_read);
}
