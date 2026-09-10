#include "budget.h"
#include "manifold.h"
#include "microtest.h"
#include "port_fsm.h"
#include "settings.h"
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
    MT_ASSERT_EQ(sim_ina_alert_ma(0), 6250); // emergency trip: 125% of the blade's 5 A ceiling
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

static void test_limit_applies_live(void) {
    support_reset(360000);
    sim_set_present(0, true);
    tick(2);
    sim_attach(0, 20000, 3000); // 60W laptop
    tick(2);
    MT_ASSERT_EQ(sim_contract_mw(0), 60000);
    uint32_t caps = sim_src_cap_count(0);
    g_settings.port_limit_ma[0] = 1000; // core 0 stores it, then commands the engine
    engine_cmd_t c = {.op = CMD_PORT_LIMIT, .port = 0, .arg = 1000};
    port_fsm_cmd(0, &c);
    tick(2);
    MT_ASSERT_EQ(sim_advertised_ma(0), 1000);
    MT_ASSERT_EQ(sim_src_cap_count(0), caps + 1); // renegotiated at once
    MT_ASSERT_EQ(sim_contract_mw(0), 20000);       // 20 V x 1 A
    MT_ASSERT_EQ(budget_port_reservation(0), 20000);
    MT_ASSERT_EQ(port_state(0), PORT_STATE_ACTIVE);
    MT_ASSERT_EQ(sim_ina_alert_ma(0), 6250); // the emergency trip does not follow the cap
    // the ceiling sticks across a detach / re-attach
    sim_detach(0);
    tick(2);
    sim_attach(0, 20000, 3000);
    tick(2);
    MT_ASSERT_EQ(sim_contract_mw(0), 20000);
}

static void test_limit_set_while_idle(void) {
    support_reset(360000);
    sim_set_present(0, true);
    tick(2);
    uint32_t caps = sim_src_cap_count(0);
    g_settings.port_limit_ma[0] = 1500;
    engine_cmd_t c = {.op = CMD_PORT_LIMIT, .port = 0, .arg = 1500};
    port_fsm_cmd(0, &c);
    MT_ASSERT_EQ(sim_advertised_ma(0), 1500);
    MT_ASSERT_EQ(sim_src_cap_count(0), caps); // nobody to renegotiate with
    sim_attach(0, 20000, 3000);
    tick(2);
    MT_ASSERT_EQ(sim_contract_mw(0), 30000); // 20 V x 1.5 A
}

static void test_volt_cap_applies_live(void) {
    support_reset(360000);
    sim_set_present(0, true);
    tick(2);
    sim_attach(0, 20000, 3000); // 60 W laptop
    tick(2);
    MT_ASSERT_EQ(tele.port[0].bus_mv, 20000);
    MT_ASSERT_EQ(budget_port_reservation(0), 60000);

    g_settings.port_max_mv[0] = 9000; // core 0 stores it, then commands the engine
    engine_cmd_t c = {.op = CMD_PORT_VOLT, .port = 0, .arg = 9000};
    port_fsm_cmd(0, &c);
    tick(2);
    MT_ASSERT_EQ(sim_advertised_mv(0), 9000);
    MT_ASSERT_EQ(tele.port[0].bus_mv, 9000); // the sink renegotiated down to the cap
    MT_ASSERT_EQ(tele.port[0].selected_pdo, 2);
    MT_ASSERT_EQ(budget_port_reservation(0), 27000); // 9 V x 3 A
    MT_ASSERT_EQ(port_state(0), PORT_STATE_ACTIVE);

    g_settings.port_max_mv[0] = PORT_VOLT_MAX_MV;
    c.arg = PORT_VOLT_MAX_MV;
    port_fsm_cmd(0, &c);
    tick(2);
    MT_ASSERT_EQ(tele.port[0].bus_mv, 20000); // and back up
    MT_ASSERT_EQ(budget_port_reservation(0), 60000);
}

static void test_volt_cap_from_probe(void) {
    support_reset(360000);
    g_settings.port_max_mv[0] = 5000; // 5 V only
    sim_set_present(0, true);
    tick(2);
    MT_ASSERT_EQ(sim_advertised_mv(0), 5000);
    sim_attach(0, 20000, 3000); // asks for 20 V, is offered 5 V
    tick(2);
    MT_ASSERT_EQ(port_state(0), PORT_STATE_ACTIVE);
    MT_ASSERT_EQ(tele.port[0].bus_mv, 5000);
    MT_ASSERT_EQ(tele.port[0].selected_pdo, 1);
    MT_ASSERT_EQ(budget_port_reservation(0), BUDGET_BASE_RESERVE_MW); // 15 W fits the base reserve

    sim_detach(0);
    tick(2);
    g_settings.port_max_mv[0] = 12000; // an idle port re-advertises at once too
    engine_cmd_t c = {.op = CMD_PORT_VOLT, .port = 0, .arg = 12000};
    port_fsm_cmd(0, &c);
    sim_attach(0, 15000, 3000); // 15 V ask lands on the 12 V PDO
    tick(2);
    MT_ASSERT_EQ(tele.port[0].bus_mv, 12000);
    MT_ASSERT_EQ(tele.port[0].selected_pdo, 3);
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

static void test_boot_policy(void) {
    support_reset(360000);
    g_settings.port_boot[0] = PORT_BOOT_OFF;
    g_settings.port_boot[1] = PORT_BOOT_LAST;
    g_settings.port_boot[2] = PORT_BOOT_LAST;
    g_settings.port_off_mask = 1u << 1; // port 2 was switched off, port 3 was not
    port_fsm_init();                   // power-up with these settings
    for (uint8_t i = 0; i < 4; i++) sim_set_present(i, true);
    tick(2);
    MT_ASSERT_EQ(port_state(0), PORT_STATE_DISABLED); // policy off
    MT_ASSERT(!sim_en(0));
    MT_ASSERT_EQ(port_state(1), PORT_STATE_DISABLED); // last: was off
    MT_ASSERT_EQ(port_state(2), PORT_STATE_IDLE);     // last: was on
    MT_ASSERT_EQ(port_state(3), PORT_STATE_IDLE);     // default: on
    // switching a boot-disabled port on brings it up at once
    engine_cmd_t on = {.op = CMD_PORT_ENABLE, .port = 0};
    port_fsm_cmd(0, &on);
    tick(3);
    MT_ASSERT_EQ(port_state(0), PORT_STATE_IDLE);
}

static void test_boot_stagger(void) {
    support_reset(360000);
    g_settings.port_priority[0] = 9; // port 1 demoted to the back of the queue
    g_settings.port_boot[3] = PORT_BOOT_OFF;
    port_fsm_init();
    bool present[NUM_PORTS] = {true, true, false, true, true, true}; // slot 3 empty
    for (uint8_t i = 0; i < NUM_PORTS; i++) sim_set_present(i, present[i]);
    port_fsm_boot_inventory(present, 0, now_ms); // cold: nothing powered
    // queue by priority among the seated, enabled blades: 2, 5, 6, then 1;
    // the boot-disabled port 4 holds no slot
    tick(2);
    MT_ASSERT_EQ(port_state(1), PORT_STATE_IDLE);
    MT_ASSERT_EQ(port_state(3), PORT_STATE_DISABLED);
    MT_ASSERT_EQ(port_state(4), PORT_STATE_ABSENT); // waiting
    MT_ASSERT_EQ(port_state(5), PORT_STATE_ABSENT);
    MT_ASSERT_EQ(port_state(0), PORT_STATE_ABSENT);
    tick_ms(250); // t = 270: slot 1 opened at 250
    MT_ASSERT_EQ(port_state(4), PORT_STATE_IDLE);
    MT_ASSERT_EQ(port_state(5), PORT_STATE_ABSENT);
    tick_ms(250); // t = 520
    MT_ASSERT_EQ(port_state(5), PORT_STATE_IDLE);
    MT_ASSERT_EQ(port_state(0), PORT_STATE_ABSENT);
    tick_ms(250); // t = 770
    MT_ASSERT_EQ(port_state(0), PORT_STATE_IDLE);
    // a blade seated after boot is not paced
    sim_set_present(2, true);
    tick(2);
    MT_ASSERT_EQ(port_state(2), PORT_STATE_IDLE);
    // nor is a boot-disabled port when it is switched on
    engine_cmd_t on = {.op = CMD_PORT_ENABLE, .port = 3};
    port_fsm_cmd(3, &on);
    tick(3);
    MT_ASSERT_EQ(port_state(3), PORT_STATE_IDLE);
}

static void test_charge_complete(void) {
    support_reset(360000);
    g_settings.charged_mw = 1000; // 1 W floor, 1 minute hold
    g_settings.charged_min = 1;
    sim_set_present(0, true);
    tick(2);
    sim_attach(0, 20000, 3000); // 60 W laptop, drawing 80 %: 48 W
    tick(2);
    MT_ASSERT(!tele.port[0].charged);
    sim_set_load_pct(0, 1); // 0.6 W: under the floor
    tick_ms(59000);
    MT_ASSERT(!tele.port[0].charged); // not for a minute yet
    tick_ms(2000);
    MT_ASSERT(tele.port[0].charged);
    MT_ASSERT_EQ(port_state(0), PORT_STATE_ACTIVE); // no auto-off policy: stays powered
    const engine_evt_t *e = evt_last(EVT_CHARGE, 0);
    MT_ASSERT(e != NULL);
    MT_ASSERT_EQ(e->code, CHARGE_DONE);
    // a brief wake-up does not clear it; a sustained draw does
    sim_set_load_pct(0, 80);
    tick_ms(30000);
    MT_ASSERT(tele.port[0].charged);
    tick_ms(31000);
    MT_ASSERT(!tele.port[0].charged);
    e = evt_last(EVT_CHARGE, 0);
    MT_ASSERT_EQ(e->code, CHARGE_RESUMED);
    // detach and re-attach start over
    sim_set_load_pct(0, 1);
    tick_ms(61000);
    MT_ASSERT(tele.port[0].charged);
    sim_detach(0);
    tick(2);
    MT_ASSERT(!tele.port[0].charged);
    sim_attach(0, 20000, 3000);
    tick_ms(30000);
    MT_ASSERT(!tele.port[0].charged); // the hold restarted at attach
}

static void test_charged_auto_off(void) {
    support_reset(360000);
    g_settings.charged_mw = 1000;
    g_settings.charged_min = 1;
    g_settings.port_auto_off = 1u << 0;
    sim_set_present(0, true);
    tick(2);
    sim_attach(0, 20000, 3000);
    sim_set_load_pct(0, 1);
    tick(2);
    tick_ms(61000);
    MT_ASSERT_EQ(port_state(0), PORT_STATE_DISABLED);
    MT_ASSERT(!sim_en(0));
    MT_ASSERT_EQ(budget_port_reservation(0), 0);
    const engine_evt_t *e = evt_last(EVT_CHARGE, 0);
    MT_ASSERT(e != NULL);
    MT_ASSERT_EQ(e->code, CHARGE_AUTO_OFF);
    MT_ASSERT_EQ(e->arg, AUTO_OFF_CHARGED);
    // switched on again, it powers up like any disabled port (the sink is
    // still plugged in, so it lands active and the clock starts over)
    engine_cmd_t on = {.op = CMD_PORT_ENABLE, .port = 0};
    port_fsm_cmd(0, &on);
    tick(3);
    MT_ASSERT_EQ(port_state(0), PORT_STATE_ACTIVE);
    MT_ASSERT(!tele.port[0].charged);
}

static void test_sleep_timer(void) {
    support_reset(360000);
    g_settings.charged_mw = 0; // detection off: the timer works on its own
    g_settings.port_sleep_min[0] = 2;
    sim_set_present(0, true);
    tick(2);
    sim_attach(0, 20000, 3000); // drawing 48 W throughout
    tick(2);
    tick_ms(119000);
    MT_ASSERT_EQ(port_state(0), PORT_STATE_ACTIVE);
    tick_ms(2000);
    MT_ASSERT_EQ(port_state(0), PORT_STATE_DISABLED);
    const engine_evt_t *e = evt_last(EVT_CHARGE, 0);
    MT_ASSERT(e != NULL);
    MT_ASSERT_EQ(e->code, CHARGE_AUTO_OFF);
    MT_ASSERT_EQ(e->arg, AUTO_OFF_SLEEP);
    // an idle port has no sink to time
    g_settings.port_sleep_min[1] = 1;
    sim_set_present(1, true);
    tick(2);
    tick_ms(120000);
    MT_ASSERT_EQ(port_state(1), PORT_STATE_IDLE);
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
    mt_run("fsm: current limit applies live", test_limit_applies_live);
    mt_run("fsm: current limit set while idle", test_limit_set_while_idle);
    mt_run("fsm: voltage cap applies live", test_volt_cap_applies_live);
    mt_run("fsm: voltage cap from probe and while idle", test_volt_cap_from_probe);
    mt_run("fsm: detach returns to idle", test_detach_returns_to_idle);
    mt_run("fsm: OCP faults then recovers", test_ocp_faults_then_recovers);
    mt_run("fsm: MPQ fault via poll", test_mpq_fault_via_poll);
    mt_run("fsm: admin disable/enable", test_admin_disable_enable);
    mt_run("fsm: boot policy on/off/last", test_boot_policy);
    mt_run("fsm: blades seated at boot come up staggered by priority", test_boot_stagger);
    mt_run("fsm: charge-complete from the draw, symmetric, per attach", test_charge_complete);
    mt_run("fsm: off when charged", test_charged_auto_off);
    mt_run("fsm: sleep timer after attach", test_sleep_timer);
    mt_run("fsm: unseat powers down", test_unseat_powers_down);
}
