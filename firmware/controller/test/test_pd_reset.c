#include "budget.h"
#include "manifold.h"
#include "microtest.h"
#include "port_fsm.h"
#include "settings.h"
#include "test_support.h"

// PD hard-reset reports from gen-3 blades.

static void seat3(uint8_t slot) {
    sim_set_gen(slot, 3);
    sim_set_present(slot, true);
    tick(2);
}

static void plug(uint8_t slot, uint16_t mv, uint32_t ma) {
    sim_attach(slot, mv, ma);
    tick(2);
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

void run_pd_reset_tests(void) {
    mt_run("pd reset: hard resets are reported", test_hard_resets_are_reported);
    mt_run("pd reset: counts before the probe are a baseline",
           test_counts_seen_before_the_probe_are_not_reported);
}
