#include "budget.h"
#include "manifold.h"
#include "microtest.h"
#include "port_fsm.h"
#include "settings.h"
#include "test_support.h"

// Throttle victim selection. Default priorities are the port index (0 =
// highest), so port 1 outranks port 6; tests override g_settings.port_priority
// where the scenario needs it.

static void seat_and_attach(uint8_t slot, uint16_t mv, uint32_t ma) {
    sim_set_present(slot, true);
    tick(2);
    sim_attach(slot, mv, ma);
    tick(2);
}

static void test_shed_single_victim(void) {
    support_reset(180000); // 180W chassis
    seat_and_attach(5, 20000, 5000); // lowest priority takes 100W
    MT_ASSERT_EQ(port_state(5), PORT_STATE_ACTIVE);
    MT_ASSERT_EQ(budget_port_reservation(5), 100000);

    seat_and_attach(0, 20000, 5000); // highest priority wants 100W: 20W short
    MT_ASSERT_EQ(port_state(0), PORT_STATE_ACTIVE);
    MT_ASSERT_EQ(budget_port_reservation(0), 100000);
    MT_ASSERT_EQ(port_state(5), PORT_STATE_THROTTLED);
    MT_ASSERT_EQ(budget_port_reservation(5), 80000);
    MT_ASSERT_EQ(sim_advertised_ma(5), 4000); // 80W at 20V
    MT_ASSERT_EQ(budget_reserved(), 180000);  // exactly full

    const engine_evt_t *t = evt_last(EVT_THROTTLE, 5);
    MT_ASSERT(t != NULL);
    MT_ASSERT_EQ(t->code, THROTTLE_CLAMPED);
    MT_ASSERT_EQ(t->arg, 80000);
    const engine_evt_t *c = evt_last(EVT_CONTRACT, 0);
    MT_ASSERT(c != NULL);
    MT_ASSERT_EQ(c->arg, 100000);
}

static void test_shed_multiple_victims(void) {
    support_reset(150000);
    seat_and_attach(5, 20000, 3000); // 60W
    seat_and_attach(4, 20000, 3000); // 60W
    MT_ASSERT_EQ(budget_reserved(), 120000); // 60+60, no idle ports

    seat_and_attach(3, 20000, 5000); // 100W: 70W short
    MT_ASSERT_EQ(port_state(3), PORT_STATE_ACTIVE);
    MT_ASSERT_EQ(budget_port_reservation(3), 100000);
    // worst priority first: port 6 to the 15W floor, port 5 covers the rest
    MT_ASSERT_EQ(port_state(5), PORT_STATE_THROTTLED);
    MT_ASSERT_EQ(budget_port_reservation(5), BUDGET_BASE_RESERVE_MW);
    MT_ASSERT_EQ(port_state(4), PORT_STATE_THROTTLED);
    MT_ASSERT_EQ(budget_port_reservation(4), 35000);
    MT_ASSERT_EQ(budget_reserved(), 150000);
}

static void test_equal_priority_never_shed(void) {
    support_reset(100000);
    g_settings.port_priority[0] = 3;
    g_settings.port_priority[1] = 3;
    seat_and_attach(1, 20000, 3000); // 60W
    seat_and_attach(0, 20000, 3000); // 60W wanted, only 40W left

    MT_ASSERT_EQ(port_state(1), PORT_STATE_ACTIVE); // peer untouched
    MT_ASSERT_EQ(budget_port_reservation(1), 60000);
    MT_ASSERT_EQ(port_state(0), PORT_STATE_THROTTLED); // newcomer clamps itself
    MT_ASSERT_EQ(budget_port_reservation(0), 40000);
    MT_ASSERT_EQ(sim_advertised_ma(0), 2000); // 40W at 20V
}

static void test_throttled_port_recovers_when_budget_frees(void) {
    support_reset(100000);
    g_settings.port_priority[1] = 0; // peer of port 1: no victim available
    seat_and_attach(1, 20000, 3000); // 60W
    seat_and_attach(0, 20000, 3000); // clamps itself to 40W
    MT_ASSERT_EQ(port_state(0), PORT_STATE_THROTTLED);

    sim_detach(1);
    tick(3);
    MT_ASSERT_EQ(port_state(0), PORT_STATE_ACTIVE);
    MT_ASSERT_EQ(budget_port_reservation(0), 60000);
    MT_ASSERT_EQ(sim_advertised_ma(0), 5000); // full advertisement restored
    MT_ASSERT_EQ(sim_contract_mw(0), 60000);  // sink renegotiated back up

    const engine_evt_t *t = evt_last(EVT_THROTTLE, 0);
    MT_ASSERT(t != NULL);
    MT_ASSERT_EQ(t->code, THROTTLE_RESTORED);
}

static void test_recovery_yields_to_higher_priority(void) {
    support_reset(150000);
    // index order would recover port 5 (ticks first) — priority must win
    g_settings.port_priority[3] = 0;
    g_settings.port_priority[4] = 9; // ticks first, worst priority
    g_settings.port_priority[5] = 2;

    seat_and_attach(5, 20000, 3000); // 60W
    seat_and_attach(4, 20000, 3000); // 60W
    seat_and_attach(3, 20000, 5000); // 100W: sheds 4 to the floor, then 5
    MT_ASSERT_EQ(budget_port_reservation(4), BUDGET_BASE_RESERVE_MW);
    MT_ASSERT_EQ(budget_port_reservation(5), 35000);

    // sink on port 4's rival renegotiates down, freeing 50W: enough for the
    // priority-2 port's recovery (25W) but also for the priority-9 port's
    // (45W). Priority, not tick order, decides who takes it.
    sim_attach(3, 20000, 2500); // 100W -> 50W
    tick(3);
    MT_ASSERT_EQ(port_state(5), PORT_STATE_ACTIVE);
    MT_ASSERT_EQ(budget_port_reservation(5), 60000);
    MT_ASSERT_EQ(port_state(4), PORT_STATE_THROTTLED); // still waiting
    MT_ASSERT_EQ(budget_port_reservation(4), BUDGET_BASE_RESERVE_MW);
}

static void test_detach_while_throttled_clears_denial(void) {
    support_reset(100000);
    g_settings.port_priority[1] = 0; // peer: the newcomer clamps itself
    seat_and_attach(1, 20000, 3000);
    seat_and_attach(0, 20000, 3000); // throttled at 40W
    MT_ASSERT_EQ(port_state(0), PORT_STATE_THROTTLED);

    sim_detach(0);
    tick(2);
    MT_ASSERT_EQ(port_state(0), PORT_STATE_IDLE);
    MT_ASSERT_EQ(budget_port_reservation(0), BUDGET_BASE_RESERVE_MW);
    MT_ASSERT_EQ(sim_advertised_ma(0), 5000);

    // freed budget must be gone from the arbiter too
    MT_ASSERT_EQ(budget_reserved(), 75000); // 60W active + 15W idle
}

void run_priority_tests(void) {
    mt_run("prio: sheds a single lower-priority victim", test_shed_single_victim);
    mt_run("prio: sheds multiple victims worst-first", test_shed_multiple_victims);
    mt_run("prio: equal priority is never shed", test_equal_priority_never_shed);
    mt_run("prio: throttled port recovers when budget frees",
           test_throttled_port_recovers_when_budget_frees);
    mt_run("prio: recovery yields to higher priority",
           test_recovery_yields_to_higher_priority);
    mt_run("prio: detach while throttled clears denial",
           test_detach_while_throttled_clears_denial);
}
