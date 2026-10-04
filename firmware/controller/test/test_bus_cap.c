#include "budget.h"
#include "bus_cap.h"
#include "manifold.h"
#include "microtest.h"
#include "port_fsm.h"
#include "settings.h"
#include "test_support.h"

// The bus-sag current cap: its own on/off rule from the bus reading, then the
// chassis ceiling it drives through the port state machine against the
// simulated blades.

// ---- bus_cap on its own -----------------------------------------------------

static uint32_t t_ms;

// Feed one reading per 100 ms for `ms`, as the monitor samples; returns how
// many times the cap changed.
static unsigned feed(uint32_t mv, uint32_t ms) {
    unsigned changes = 0;
    for (uint32_t t = 0; t < ms; t += 100) {
        t_ms += 100;
        if (bus_cap_poll(t_ms, mv)) changes++;
    }
    return changes;
}

static void start(void) {
    t_ms = 1000;
    bus_cap_init();
}

static void test_cap_off_on_a_healthy_bus(void) {
    start();
    MT_ASSERT_EQ(feed(24000, 60000), 0);
    MT_ASSERT(!bus_cap_on());
    MT_ASSERT_EQ(bus_cap_ma(), 0);
}

static void test_cap_goes_on_under_20v_and_off_after_30s_above_20v5(void) {
    start();
    feed(24000, 1000);
    MT_ASSERT_EQ(feed(20000, 1000), 0); // at the threshold: not under it
    MT_ASSERT_EQ(feed(19990, 100), 1);  // the first reading under 20 V
    MT_ASSERT(bus_cap_on());
    MT_ASSERT_EQ(bus_cap_ma(), BUS_CAP_MA);

    MT_ASSERT_EQ(feed(20400, 60000), 0); // recovered, but not to 20.5 V: stays on
    MT_ASSERT_EQ(feed(20500, 29900), 0); // 20.5 V, not yet for 30 s
    MT_ASSERT_EQ(feed(20500, 200), 1);   // 30 s up
    MT_ASSERT(!bus_cap_on());
}

static void test_dip_during_the_hold_restarts_it(void) {
    start();
    feed(19000, 100);
    MT_ASSERT(bus_cap_on());
    feed(21000, 20000);                  // 20 s into the hold
    feed(20400, 100);                    // one reading under 20.5 V
    MT_ASSERT_EQ(feed(21000, 20000), 0); // 20 s again: not enough
    MT_ASSERT_EQ(feed(21000, 10100), 1); // 30 s since the dip
    MT_ASSERT(!bus_cap_on());
}

static void test_first_reading_is_judged_against_the_release_level(void) {
    start();
    MT_ASSERT_EQ(feed(20300, 100), 1); // over 20 V, under 20.5 V: capped at power-up
    MT_ASSERT(bus_cap_on());
    MT_ASSERT_EQ(feed(20300, 60000), 0); // and stays so until 20.5 V holds
    MT_ASSERT_EQ(feed(20600, 30100), 1);
    MT_ASSERT(!bus_cap_on());
    MT_ASSERT_EQ(feed(20300, 60000), 0); // the same voltage while running: no cap

    start();
    MT_ASSERT_EQ(feed(20500, 100), 0); // 20.5 V at power-up: not capped
    MT_ASSERT(!bus_cap_on());
}

static void test_no_reading_changes_nothing(void) {
    start();
    MT_ASSERT_EQ(feed(0, 60000), 0); // a board without the divider
    MT_ASSERT(!bus_cap_on());
    MT_ASSERT_EQ(feed(24000, 100), 0); // the first real reading is still the first
    feed(19000, 100);
    MT_ASSERT(bus_cap_on());
    MT_ASSERT_EQ(feed(0, 60000), 0); // readings stopping leaves the cap where it is
    MT_ASSERT(bus_cap_on());
}

// ---- the ceiling through the port FSM -------------------------------------

static void seat_and_attach(uint8_t slot, uint16_t mv, uint32_t ma) {
    sim_set_present(slot, true);
    tick(2);
    sim_attach(slot, mv, ma);
    tick(2);
}

static void test_ceiling_caps_an_attached_port(void) {
    support_reset(600000);
    seat_and_attach(0, 20000, 5000); // 100 W
    MT_ASSERT_EQ(sim_advertised_ma(0), 5000);
    MT_ASSERT_EQ(sim_contract_mw(0), 100000);

    port_fsm_set_ceiling(BUS_CAP_MA);
    tick(2);
    MT_ASSERT_EQ(sim_advertised_ma(0), 3000);
    MT_ASSERT_EQ(sim_contract_mw(0), 60000); // the sink renegotiated within the cap
    MT_ASSERT_EQ(budget_port_reservation(0), 60000);
    MT_ASSERT_EQ(port_state(0), PORT_STATE_ACTIVE); // capped, not throttled: the budget had room

    port_fsm_set_ceiling(0);
    tick(2);
    MT_ASSERT_EQ(sim_advertised_ma(0), 5000);
    MT_ASSERT_EQ(sim_contract_mw(0), 100000);
}

static void test_ceiling_applies_to_a_blade_that_comes_up_under_it(void) {
    support_reset(600000);
    port_fsm_set_ceiling(BUS_CAP_MA); // as at a power-up on a sagging bus
    seat_and_attach(2, 20000, 5000);
    MT_ASSERT_EQ(sim_advertised_ma(2), 3000);
    MT_ASSERT_EQ(sim_contract_mw(2), 60000);
    sim_set_present(3, true); // an idle port is programmed the same
    tick(2);
    MT_ASSERT_EQ(sim_advertised_ma(3), 3000);

    port_fsm_set_ceiling(0);
    tick(2);
    MT_ASSERT_EQ(sim_advertised_ma(2), 5000);
    MT_ASSERT_EQ(sim_advertised_ma(3), 5000);
}

static void test_ceiling_stays_under_a_port_limit_setting(void) {
    support_reset(600000);
    g_settings.port_limit_ma[1] = 2000; // a port set lower than the cap keeps its own limit
    seat_and_attach(1, 20000, 5000);
    MT_ASSERT_EQ(sim_advertised_ma(1), 2000);
    port_fsm_set_ceiling(BUS_CAP_MA);
    tick(2);
    MT_ASSERT_EQ(sim_advertised_ma(1), 2000);

    // raising the setting while capped lands on the cap, not the setting
    g_settings.port_limit_ma[1] = 5000;
    engine_cmd_t c = {.op = CMD_PORT_LIMIT, .port = 1, .arg = 5000};
    port_fsm_cmd(1, &c);
    tick(2);
    MT_ASSERT_EQ(sim_advertised_ma(1), 3000);
    port_fsm_set_ceiling(0);
    tick(2);
    MT_ASSERT_EQ(sim_advertised_ma(1), 5000); // the setting applies once the cap lifts
}

static void test_ceiling_and_a_budget_clamp(void) {
    support_reset(140000);
    seat_and_attach(5, 20000, 5000); // 100 W, lowest priority
    seat_and_attach(0, 20000, 5000); // 100 W wanted: port 6 is clamped to 40 W (2 A)
    MT_ASSERT_EQ(port_state(5), PORT_STATE_THROTTLED);
    MT_ASSERT_EQ(sim_advertised_ma(5), 2000);

    port_fsm_set_ceiling(BUS_CAP_MA);
    tick(2);
    MT_ASSERT_EQ(sim_advertised_ma(0), 3000); // the active port follows the cap;
    tick(200);                                // the clamped one takes the freed 40 W, up to the cap
    MT_ASSERT_EQ(sim_advertised_ma(5), 3000);
    MT_ASSERT_EQ(sim_contract_mw(5), 60000);
    MT_ASSERT_EQ(port_state(5), PORT_STATE_THROTTLED); // it still wants its 100 W
    MT_ASSERT_EQ(budget_reserved(), 120000);
    // ...but is not made to renegotiate the same 3 A every second for it
    uint32_t caps = sim_src_cap_count(5);
    tick(500);
    MT_ASSERT_EQ(sim_src_cap_count(5), caps);
    MT_ASSERT_EQ(sim_advertised_ma(5), 3000);

    port_fsm_set_ceiling(0);
    tick(200);
    MT_ASSERT_EQ(sim_advertised_ma(0), 5000); // back to its own setting, and its 100 W...
    MT_ASSERT_EQ(sim_contract_mw(0), 100000);
    MT_ASSERT_EQ(sim_advertised_ma(5), 2000); // ...which clamps the low-priority port again
    MT_ASSERT_EQ(port_state(5), PORT_STATE_THROTTLED);
    sim_detach(0);
    tick(3);
    MT_ASSERT_EQ(port_state(5), PORT_STATE_ACTIVE); // room at last
    MT_ASSERT_EQ(sim_advertised_ma(5), 5000);
    MT_ASSERT_EQ(sim_contract_mw(5), 100000);
}

void run_bus_cap_tests(void) {
    mt_run("bus cap: off on a healthy bus", test_cap_off_on_a_healthy_bus);
    mt_run("bus cap: on under 20 V, off after 30 s at 20.5 V", test_cap_goes_on_under_20v_and_off_after_30s_above_20v5);
    mt_run("bus cap: a dip during the hold restarts it", test_dip_during_the_hold_restarts_it);
    mt_run("bus cap: the first reading is judged at 20.5 V", test_first_reading_is_judged_against_the_release_level);
    mt_run("bus cap: no reading changes nothing", test_no_reading_changes_nothing);
    mt_run("ceiling: caps an attached port and lets it back", test_ceiling_caps_an_attached_port);
    mt_run("ceiling: a blade that comes up under it", test_ceiling_applies_to_a_blade_that_comes_up_under_it);
    mt_run("ceiling: stays under a port's own limit", test_ceiling_stays_under_a_port_limit_setting);
    mt_run("ceiling: with a budget clamp", test_ceiling_and_a_budget_clamp);
}
