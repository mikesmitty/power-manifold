#include "blade_regs.h"
#include "microtest.h"
#include "pdo.h"

static uint32_t fixed_rdo(unsigned pos, unsigned op_ma) {
    return ((uint32_t)pos << 28) | ((op_ma / 10) << 10) | (op_ma / 10);
}

static uint32_t pps_rdo(unsigned pos, unsigned mv, unsigned op_ma) {
    return ((uint32_t)pos << 28) | ((mv / 20) << 9) | (op_ma / 50);
}

static void full_table(void) {
    pdo_limits_t lim = {.max_ma = 5000, .max_mv = BLADE_MAX_MV_ALL, .cable_ma = 5000};
    pdo_table_t t;
    pdo_build(&lim, &t);
    MT_ASSERT_EQ(t.count, 7);
    static const unsigned mv[5] = {5000, 9000, 12000, 15000, 20000};
    for (int i = 0; i < 5; i++) {
        MT_ASSERT(!pdo_is_pps(t.obj[i]));
        MT_ASSERT_EQ(pdo_fixed_mv(t.obj[i]), mv[i]);
        MT_ASSERT_EQ(pdo_fixed_ma(t.obj[i]), 5000);
    }
    MT_ASSERT_EQ(t.obj[0], 0x080191F4); // 5 V 5 A, unconstrained power
    MT_ASSERT_EQ(t.obj[4], 0x000641F4); // 20 V 5 A: no flags past the first object
    MT_ASSERT(pdo_is_pps(t.obj[5]));
    MT_ASSERT_EQ(pdo_pps_min_mv(t.obj[5]), 3300);
    MT_ASSERT_EQ(pdo_pps_max_mv(t.obj[5]), 11000);
    MT_ASSERT_EQ(pdo_pps_ma(t.obj[5]), 5000);
    MT_ASSERT_EQ(pdo_pps_max_mv(t.obj[6]), 21000);
}

static void no_object_passes_100w(void) {
    pdo_limits_t lim = {.max_ma = 5000, .max_mv = BLADE_MAX_MV_ALL, .cable_ma = 5000};
    pdo_table_t t;
    pdo_build(&lim, &t);
    MT_ASSERT_EQ(pdo_pps_ma(t.obj[6]), 4750); // 21 V: 99.75 W
    for (int i = 0; i < t.count; i++) {
        uint32_t top = pdo_is_pps(t.obj[i]) ? pdo_pps_max_mv(t.obj[i]) : pdo_fixed_mv(t.obj[i]);
        uint32_t ma = pdo_is_pps(t.obj[i]) ? pdo_pps_ma(t.obj[i]) : pdo_fixed_ma(t.obj[i]);
        MT_ASSERT(top * ma <= 100000u * 1000u);
    }
}

static void plain_cable_stops_at_3a(void) {
    pdo_limits_t lim = {.max_ma = 5000, .max_mv = BLADE_MAX_MV_ALL, .cable_ma = 3000};
    pdo_table_t t;
    pdo_build(&lim, &t);
    MT_ASSERT_EQ(t.count, 7);
    for (int i = 0; i < 5; i++) MT_ASSERT_EQ(pdo_fixed_ma(t.obj[i]), 3000);
    MT_ASSERT_EQ(pdo_pps_ma(t.obj[5]), 3000);
    MT_ASSERT_EQ(pdo_pps_ma(t.obj[6]), 3000);
}

static void current_ceiling(void) {
    pdo_limits_t lim = {.max_ma = 1525, .max_mv = BLADE_MAX_MV_ALL, .cable_ma = 5000};
    pdo_table_t t;
    pdo_build(&lim, &t);
    MT_ASSERT_EQ(pdo_fixed_ma(t.obj[0]), 1520); // 10 mA steps, rounded down
    MT_ASSERT_EQ(pdo_pps_ma(t.obj[5]), 1500);   // 50 mA steps
    lim.max_ma = 9000;                          // past the hardware
    pdo_build(&lim, &t);
    MT_ASSERT_EQ(pdo_fixed_ma(t.obj[0]), BLADE_MAX_MA_LIMIT);
    lim.max_ma = 0;                             // unconfigured: nothing to offer
    pdo_build(&lim, &t);
    MT_ASSERT_EQ(t.count, 0);
}

static void voltage_cap(void) {
    pdo_limits_t lim = {.max_ma = 3000, .max_mv = 12000, .cable_ma = 3000};
    pdo_table_t t;
    pdo_build(&lim, &t);
    MT_ASSERT_EQ(t.count, 4); // 5, 9, 12 and the 11 V PPS range
    MT_ASSERT_EQ(pdo_fixed_mv(t.obj[2]), 12000);
    MT_ASSERT_EQ(pdo_pps_max_mv(t.obj[3]), 11000);

    lim.max_mv = 9000; // the 11 V range reaches above it
    pdo_build(&lim, &t);
    MT_ASSERT_EQ(t.count, 2);

    lim.max_mv = 0; // 5 V always stays
    pdo_build(&lim, &t);
    MT_ASSERT_EQ(t.count, 1);
    MT_ASSERT_EQ(pdo_fixed_mv(t.obj[0]), 5000);

    lim.max_mv = 15000; // 20 V and the 21 V range go
    pdo_build(&lim, &t);
    MT_ASSERT_EQ(t.count, 5);
}

static pdo_table_t table(void) {
    pdo_limits_t lim = {.max_ma = 5000, .max_mv = BLADE_MAX_MV_ALL, .cable_ma = 3000};
    pdo_table_t t;
    pdo_build(&lim, &t);
    return t;
}

static void fixed_request(void) {
    pdo_table_t t = table();
    pdo_request_t r;
    MT_ASSERT(pdo_check_request(&t, fixed_rdo(5, 2250), &r));
    MT_ASSERT_EQ(r.position, 5);
    MT_ASSERT(!r.pps);
    MT_ASSERT_EQ(r.mv, 20000);
    MT_ASSERT_EQ(r.op_ma, 2250);
    MT_ASSERT_EQ(r.limit_ma, 3000); // the object's, not the sink's figure
    MT_ASSERT(!r.mismatch);

    MT_ASSERT(pdo_check_request(&t, fixed_rdo(1, 3000) | (1u << 26), &r));
    MT_ASSERT(r.mismatch);
}

static void fixed_request_rejected(void) {
    pdo_table_t t = table();
    pdo_request_t r;
    MT_ASSERT(!pdo_check_request(&t, fixed_rdo(0, 1000), &r)); // position 0 is reserved
    MT_ASSERT(!pdo_check_request(&t, fixed_rdo(8, 1000), &r)); // past the table
    MT_ASSERT(!pdo_check_request(&t, fixed_rdo(2, 3010), &r)); // more than offered
}

static void pps_request(void) {
    pdo_table_t t = table();
    pdo_request_t r;
    MT_ASSERT(pdo_check_request(&t, pps_rdo(6, 8400, 2500), &r));
    MT_ASSERT(r.pps);
    MT_ASSERT_EQ(r.mv, 8400);
    MT_ASSERT_EQ(r.op_ma, 2500);
    MT_ASSERT_EQ(r.limit_ma, 2500); // a PPS current is the limit to regulate to
    MT_ASSERT(pdo_check_request(&t, pps_rdo(7, 21000, 3000), &r));
    MT_ASSERT(pdo_check_request(&t, pps_rdo(6, 3300, 1000), &r));
}

static void pps_request_rejected(void) {
    pdo_table_t t = table();
    pdo_request_t r;
    MT_ASSERT(!pdo_check_request(&t, pps_rdo(6, 11020, 1000), &r)); // above the range
    MT_ASSERT(!pdo_check_request(&t, pps_rdo(6, 3280, 1000), &r));  // below it
    MT_ASSERT(!pdo_check_request(&t, pps_rdo(7, 20000, 3050), &r)); // more than offered
    MT_ASSERT(!pdo_check_request(&t, pps_rdo(7, 40960 + 9000, 1000), &r)); // voltage bit 11 set
}

static void positions_follow_the_table_sent(void) {
    pdo_limits_t lim = {.max_ma = 3000, .max_mv = 9000, .cable_ma = 3000};
    pdo_table_t t;
    pdo_build(&lim, &t);
    pdo_request_t r;
    MT_ASSERT(pdo_check_request(&t, fixed_rdo(2, 2000), &r));
    MT_ASSERT_EQ(r.mv, 9000);
    MT_ASSERT(!pdo_check_request(&t, fixed_rdo(3, 2000), &r)); // 12 V was never offered
}

void run_pdo_tests(void) {
    mt_run("pdo: the full table", full_table);
    mt_run("pdo: no object passes 100 W", no_object_passes_100w);
    mt_run("pdo: a cable without an e-marker stops at 3 A", plain_cable_stops_at_3a);
    mt_run("pdo: current ceiling", current_ceiling);
    mt_run("pdo: voltage cap", voltage_cap);
    mt_run("pdo: fixed request", fixed_request);
    mt_run("pdo: fixed request rejected", fixed_request_rejected);
    mt_run("pdo: PPS request", pps_request);
    mt_run("pdo: PPS request rejected", pps_request_rejected);
    mt_run("pdo: positions follow the table sent", positions_follow_the_table_sent);
}
