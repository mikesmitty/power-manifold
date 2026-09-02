#include "budget.h"
#include "manifold.h"
#include "microtest.h"
#include "port_fsm.h"
#include "test_support.h"

// Timing notes: seating a blade costs one tick to enter PROBE and one probe
// attempt per tick after that; a successful probe lands in IDLE on the second
// tick. Attach/contract changes land one tick after the sim mutates.

static void test_probe_to_idle(void) {
    support_reset(360000);
    sim_set_present(0, true);
    tick(2);
    MT_ASSERT_EQ(port_state(0), PORT_STATE_IDLE);
    MT_ASSERT(sim_en(0));
    MT_ASSERT_EQ(budget_port_reservation(0), BUDGET_BASE_RESERVE_MW);
    MT_ASSERT_EQ(sim_advertised_ma(0), 5000);
    MT_ASSERT_EQ(sim_ina_alert_ma(0), 6250); // 125% of the 5A port limit
    MT_ASSERT(evt_count(EVT_STATE_CHANGE, 0) >= 2); // absent->probe->idle
}

static void test_probe_failure_faults_then_recovers(void) {
    support_reset(360000);
    sim_set_probe_ok(0, true, false); // MPQ4242 not answering
    sim_set_present(0, true);
    tick(5); // 1 to enter probe + 3 attempts + 1 settle
    MT_ASSERT_EQ(port_state(0), PORT_STATE_FAULT);
    MT_ASSERT(!sim_en(0));
    const engine_evt_t *e = evt_last(EVT_PROBE_FAIL, 0);
    MT_ASSERT(e != NULL);
    MT_ASSERT_EQ(e->code, 3); // PROBE_FAIL_MPQ4242 (port_fsm.c)

    sim_set_probe_ok(0, true, true);
    tick_ms(5100); // cooldown is 5s
    MT_ASSERT_EQ(port_state(0), PORT_STATE_IDLE);
}

static void test_attach_contract(void) {
    support_reset(360000);
    sim_set_present(0, true);
    tick(2);
    sim_attach(0, 20000, 3000); // 60W laptop
    tick(2);
    MT_ASSERT_EQ(port_state(0), PORT_STATE_ACTIVE);
    MT_ASSERT_EQ(budget_port_reservation(0), 60000);
    MT_ASSERT_EQ(tele.port[0].bus_mv, 20000);
    const engine_evt_t *e = evt_last(EVT_CONTRACT, 0);
    MT_ASSERT(e != NULL);
    MT_ASSERT_EQ(e->arg, 60000);
    MT_ASSERT_EQ(e->code, 5); // PDO5: 20V
}

static void test_attach_12v_contract(void) {
    support_reset(360000);
    sim_set_present(0, true);
    tick(2);
    sim_attach(0, 12000, 3000); // 36W 12V trigger
    tick(2);
    MT_ASSERT_EQ(port_state(0), PORT_STATE_ACTIVE);
    MT_ASSERT_EQ(budget_port_reservation(0), 36000);
    MT_ASSERT_EQ(tele.port[0].bus_mv, 12000);
    const engine_evt_t *e = evt_last(EVT_CONTRACT, 0);
    MT_ASSERT(e != NULL);
    MT_ASSERT_EQ(e->arg, 36000);
    MT_ASSERT_EQ(e->code, 3); // PDO3: 12V
}

static void test_attach_15v_contract(void) {
    support_reset(360000);
    sim_set_present(0, true);
    tick(2);
    sim_attach(0, 15000, 3000); // 45W 15V (e.g. Switch dock)
    tick(2);
    MT_ASSERT_EQ(port_state(0), PORT_STATE_ACTIVE);
    MT_ASSERT_EQ(budget_port_reservation(0), 45000);
    MT_ASSERT_EQ(tele.port[0].bus_mv, 15000);
    const engine_evt_t *e = evt_last(EVT_CONTRACT, 0);
    MT_ASSERT(e != NULL);
    MT_ASSERT_EQ(e->arg, 45000);
    MT_ASSERT_EQ(e->code, 4); // PDO4: 15V
}

static void test_small_contract_reserves_base(void) {
    support_reset(360000);
    sim_set_present(0, true);
    tick(2);
    sim_attach(0, 5000, 1000); // 5W trickle
    tick(2);
    MT_ASSERT_EQ(port_state(0), PORT_STATE_ACTIVE);
    MT_ASSERT_EQ(budget_port_reservation(0), BUDGET_BASE_RESERVE_MW);
}

static void test_detach_returns_to_idle(void) {
    support_reset(360000);
    sim_set_present(0, true);
    tick(2);
    sim_attach(0, 20000, 3000);
    tick(2);
    sim_detach(0);
    tick(2);
    MT_ASSERT_EQ(port_state(0), PORT_STATE_IDLE);
    MT_ASSERT_EQ(budget_port_reservation(0), BUDGET_BASE_RESERVE_MW);
    MT_ASSERT_EQ(sim_advertised_ma(0), 5000);
}

static void test_ocp_faults_then_recovers(void) {
    support_reset(360000);
    sim_set_present(0, true);
    tick(2);
    sim_attach(0, 20000, 3000);
    tick(2);

    sim_trip_ocp(0);
    tick(1);
    MT_ASSERT_EQ(port_state(0), PORT_STATE_FAULT);
    MT_ASSERT(!sim_en(0));
    MT_ASSERT_EQ(budget_port_reservation(0), 0);
    const engine_evt_t *e = evt_last(EVT_FAULT, 0);
    MT_ASSERT(e != NULL);
    MT_ASSERT_EQ(e->arg, 1); // INA226 alert flagged

    tick_ms(5100); // cooldown, re-probe, sink still attached
    tick(2);
    MT_ASSERT_EQ(port_state(0), PORT_STATE_ACTIVE);
    MT_ASSERT_EQ(budget_port_reservation(0), 60000);
}

static void test_mpq_fault_via_poll(void) {
    support_reset(360000);
    sim_set_present(0, true);
    tick(2);
    sim_attach(0, 20000, 3000);
    tick(2);

    sim_set_mpq_fault(0, MPQ_FAULT_OTW1);
    tick(1);
    MT_ASSERT_EQ(port_state(0), PORT_STATE_FAULT);
    const engine_evt_t *e = evt_last(EVT_FAULT, 0);
    MT_ASSERT(e != NULL);
    MT_ASSERT(e->code & MPQ_FAULT_OTW1);

    sim_set_mpq_fault(0, 0);
    tick_ms(5100);
    tick(2);
    MT_ASSERT_EQ(port_state(0), PORT_STATE_ACTIVE);
}

static void test_admin_disable_enable(void) {
    support_reset(360000);
    sim_set_present(0, true);
    tick(2);

    engine_cmd_t off = {.op = CMD_PORT_DISABLE, .port = 0};
    port_fsm_cmd(0, &off);
    tick(1);
    MT_ASSERT_EQ(port_state(0), PORT_STATE_DISABLED);
    MT_ASSERT(!sim_en(0));
    MT_ASSERT_EQ(budget_port_reservation(0), 0);

    engine_cmd_t on = {.op = CMD_PORT_ENABLE, .port = 0};
    port_fsm_cmd(0, &on);
    tick(3);
    MT_ASSERT_EQ(port_state(0), PORT_STATE_IDLE);
}

static void test_unseat_powers_down(void) {
    support_reset(360000);
    sim_set_present(0, true);
    tick(2);
    sim_attach(0, 20000, 3000);
    tick(2);

    sim_set_present(0, false);
    tick(1);
    MT_ASSERT_EQ(port_state(0), PORT_STATE_ABSENT);
    MT_ASSERT(!sim_en(0));
    MT_ASSERT_EQ(budget_port_reservation(0), 0);
}

void run_port_fsm_tests(void) {
    mt_run("fsm: probe to idle", test_probe_to_idle);
    mt_run("fsm: probe failure faults then recovers",
           test_probe_failure_faults_then_recovers);
    mt_run("fsm: attach lands a contract", test_attach_contract);
    mt_run("fsm: attach 12V lands PDO3", test_attach_12v_contract);
    mt_run("fsm: attach 15V lands PDO4", test_attach_15v_contract);
    mt_run("fsm: small contract reserves base", test_small_contract_reserves_base);
    mt_run("fsm: detach returns to idle", test_detach_returns_to_idle);
    mt_run("fsm: OCP faults then recovers", test_ocp_faults_then_recovers);
    mt_run("fsm: MPQ fault via poll", test_mpq_fault_via_poll);
    mt_run("fsm: admin disable/enable", test_admin_disable_enable);
    mt_run("fsm: unseat powers down", test_unseat_powers_down);
}
