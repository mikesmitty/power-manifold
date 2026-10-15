#include "blade_regs.h"
#include "microtest.h"
#include "pdo.h"

static uint32_t fixed_rdo(unsigned pos, unsigned op_ma) {
    return ((uint32_t)pos << 28) | ((op_ma / 10) << 10) | (op_ma / 10);
}

static uint32_t pps_rdo(unsigned pos, unsigned mv, unsigned op_ma) {
    return ((uint32_t)pos << 28) | ((mv / 20) << 9) | (op_ma / 50);
}

static uint32_t avs_rdo(unsigned pos, unsigned mv, unsigned op_ma) {
    return ((uint32_t)pos << 28) | ((mv / 25) << 9) | (op_ma / 50);
}

static pdo_table_t build(uint8_t max_w, uint16_t max_mv, uint16_t cable_ma) {
    pdo_limits_t lim = {.max_w = max_w, .max_mv = max_mv, .cable_ma = cable_ma};
    pdo_table_t t;
    pdo_build(&lim, &t);
    return t;
}

static void full_table(void) {
    pdo_table_t t = build(100, BLADE_MAX_MV_ALL, 5000);
    MT_ASSERT_EQ(t.count, 7);
    static const unsigned mv[5] = {5000, 9000, 12000, 15000, 20000};
    for (int i = 0; i < 5; i++) {
        MT_ASSERT(!pdo_is_apdo(t.obj[i]));
        MT_ASSERT_EQ(pdo_fixed_mv(t.obj[i]), mv[i]);
        MT_ASSERT_EQ(pdo_fixed_ma(t.obj[i]), 5000);
    }
    MT_ASSERT_EQ(t.obj[0], 0x080191F4); // 5 V 5 A, unconstrained power
    MT_ASSERT_EQ(t.obj[4], 0x000641F4); // 20 V 5 A: no flags past the first object

    MT_ASSERT(pdo_is_avs(t.obj[5])); // SPR AVS comes before PPS
    MT_ASSERT(pdo_is_apdo(t.obj[5]));
    MT_ASSERT_EQ(pdo_avs_max_mv(t.obj[5]), 20000);
    MT_ASSERT_EQ(pdo_avs_ma_15v(t.obj[5]), 5000);
    MT_ASSERT_EQ(pdo_avs_ma_20v(t.obj[5]), 5000);
    MT_ASSERT_EQ(t.obj[5], 0xE007D1F4);

    MT_ASSERT(pdo_is_pps(t.obj[6]));
    MT_ASSERT_EQ(pdo_pps_min_mv(t.obj[6]), 5000);
    MT_ASSERT_EQ(pdo_pps_max_mv(t.obj[6]), 21000);
    MT_ASSERT_EQ(pdo_pps_ma(t.obj[6]), 5000); // 100 W at 20 V, PD 3.2 Table 3.4
    MT_ASSERT(pdo_pps_power_limited(t.obj[6])); // 21 V at 5 A would be 105 W
    MT_ASSERT_EQ(t.power_mw, 100000);
}

// Each fixed object gets the power limit at its own voltage, up to 5 A
static void the_power_limit_sets_each_current(void) {
    pdo_table_t t = build(40, BLADE_MAX_MV_ALL, 5000);
    MT_ASSERT_EQ(t.count, 7);
    MT_ASSERT_EQ(pdo_fixed_ma(t.obj[0]), 5000); // 25 W: 5 A is the most there is
    MT_ASSERT_EQ(pdo_fixed_ma(t.obj[1]), 4440); // 40 W at 9 V, rounded down to 10 mA
    MT_ASSERT_EQ(pdo_fixed_ma(t.obj[2]), 3330);
    MT_ASSERT_EQ(pdo_fixed_ma(t.obj[3]), 2660);
    MT_ASSERT_EQ(pdo_fixed_ma(t.obj[4]), 2000);
    MT_ASSERT_EQ(pdo_avs_ma_15v(t.obj[5]), 2660); // the 15 V and 20 V objects' currents
    MT_ASSERT_EQ(pdo_avs_ma_20v(t.obj[5]), 2000);
    MT_ASSERT_EQ(pdo_pps_ma(t.obj[6]), 2000);
    MT_ASSERT(pdo_pps_power_limited(t.obj[6]));
    MT_ASSERT_EQ(t.power_mw, 40000);
}

static void no_object_passes_the_power_limit(void) {
    static const uint8_t limits_w[] = {5, 15, 27, 40, 45, 60, 65, 100};
    for (unsigned n = 0; n < sizeof limits_w; n++) {
        uint32_t limit_uw = limits_w[n] * 1000000u;
        pdo_table_t t = build(limits_w[n], BLADE_MAX_MV_ALL, 5000);
        for (int i = 0; i < t.count; i++) {
            uint32_t pdo = t.obj[i];
            if (pdo_is_avs(pdo)) {
                MT_ASSERT(15000u * pdo_avs_ma_15v(pdo) <= limit_uw);
                MT_ASSERT(20000u * pdo_avs_ma_20v(pdo) <= limit_uw);
            } else if (pdo_is_pps(pdo)) {
                // past the power at the top of its range only with the flag set
                if (pdo_pps_max_mv(pdo) * pdo_pps_ma(pdo) > limit_uw) MT_ASSERT(pdo_pps_power_limited(pdo));
            } else {
                MT_ASSERT(pdo_fixed_mv(pdo) * pdo_fixed_ma(pdo) <= limit_uw);
            }
        }
    }
}

static void a_cable_without_an_e_marker_stops_at_3a(void) {
    pdo_table_t t = build(100, BLADE_MAX_MV_ALL, 3000);
    MT_ASSERT_EQ(t.count, 7);
    for (int i = 0; i < 5; i++) MT_ASSERT_EQ(pdo_fixed_ma(t.obj[i]), 3000);
    MT_ASSERT_EQ(pdo_avs_ma_15v(t.obj[5]), 3000);
    MT_ASSERT_EQ(pdo_avs_ma_20v(t.obj[5]), 3000);
    MT_ASSERT_EQ(pdo_pps_ma(t.obj[6]), 3000);
    MT_ASSERT(!pdo_pps_power_limited(t.obj[6])); // 63 W at 21 V is within 100 W
}

static void power_limit_bounds(void) {
    pdo_table_t t = build(0, BLADE_MAX_MV_ALL, 5000); // unconfigured: nothing to offer
    MT_ASSERT_EQ(t.count, 0);
    t = build(250, BLADE_MAX_MV_ALL, 5000); // past 100 W is 100 W
    MT_ASSERT_EQ(t.power_mw, 100000);
    MT_ASSERT_EQ(pdo_fixed_ma(t.obj[4]), 5000);
    t = build(5, BLADE_MAX_MV_ALL, 3000);
    MT_ASSERT_EQ(pdo_fixed_ma(t.obj[0]), 1000);
    MT_ASSERT_EQ(pdo_fixed_ma(t.obj[4]), 250);
    MT_ASSERT_EQ(pdo_pps_ma(t.obj[6]), 250);
}

static void voltage_cap(void) {
    pdo_table_t t = build(100, 15000, 5000); // 20 V goes; AVS stops at 15 V; PPS is 5 - 11 V
    MT_ASSERT_EQ(t.count, 6);
    MT_ASSERT_EQ(pdo_fixed_mv(t.obj[3]), 15000);
    MT_ASSERT(pdo_is_avs(t.obj[4]));
    MT_ASSERT_EQ(pdo_avs_max_mv(t.obj[4]), 15000);
    MT_ASSERT_EQ(pdo_avs_ma_20v(t.obj[4]), 0);
    MT_ASSERT(pdo_is_pps(t.obj[5]));
    MT_ASSERT_EQ(pdo_pps_min_mv(t.obj[5]), 5000);
    MT_ASSERT_EQ(pdo_pps_max_mv(t.obj[5]), 11000);
    MT_ASSERT_EQ(pdo_pps_ma(t.obj[5]), 5000);
    MT_ASSERT(!pdo_pps_power_limited(t.obj[5])); // 55 W at 11 V

    t = build(60, 12000, 3000); // no AVS without 15 V
    MT_ASSERT_EQ(t.count, 4);
    MT_ASSERT_EQ(pdo_fixed_mv(t.obj[2]), 12000);
    MT_ASSERT(pdo_is_pps(t.obj[3]));
    MT_ASSERT_EQ(pdo_pps_max_mv(t.obj[3]), 11000);

    t = build(60, 9000, 3000); // the 11 V range reaches above it
    MT_ASSERT_EQ(t.count, 2);

    t = build(60, 0, 3000); // 5 V always stays
    MT_ASSERT_EQ(t.count, 1);
    MT_ASSERT_EQ(pdo_fixed_mv(t.obj[0]), 5000);

    t = build(100, BLADE_MAX_MV_ALL, 5000); // a 20 V cap is no cap: the 21 V range too
    MT_ASSERT_EQ(pdo_pps_max_mv(t.obj[6]), 21000);
}

static void fixed_request(void) {
    pdo_table_t t = build(100, BLADE_MAX_MV_ALL, 3000);
    pdo_request_t r;
    MT_ASSERT(pdo_check_request(&t, fixed_rdo(5, 2250), &r));
    MT_ASSERT_EQ(r.position, 5);
    MT_ASSERT_EQ(r.kind, PDO_FIXED);
    MT_ASSERT_EQ(r.mv, 20000);
    MT_ASSERT_EQ(r.op_ma, 2250);
    MT_ASSERT_EQ(r.limit_ma, 3000); // the object's, not the sink's figure
    MT_ASSERT(!r.mismatch);

    MT_ASSERT(pdo_check_request(&t, fixed_rdo(1, 3000) | (1u << 26), &r));
    MT_ASSERT(r.mismatch);
}

static void fixed_request_rejected(void) {
    pdo_table_t t = build(100, BLADE_MAX_MV_ALL, 3000);
    pdo_request_t r;
    MT_ASSERT(!pdo_check_request(&t, fixed_rdo(0, 1000), &r)); // position 0 is reserved
    MT_ASSERT(!pdo_check_request(&t, fixed_rdo(8, 1000), &r)); // past the table
    MT_ASSERT(!pdo_check_request(&t, fixed_rdo(2, 3010), &r)); // more than offered
}

static void pps_request(void) {
    pdo_table_t t = build(100, BLADE_MAX_MV_ALL, 3000);
    pdo_request_t r;
    MT_ASSERT(pdo_check_request(&t, pps_rdo(7, 8400, 2500), &r));
    MT_ASSERT_EQ(r.kind, PDO_PPS);
    MT_ASSERT_EQ(r.mv, 8400);
    MT_ASSERT_EQ(r.op_ma, 2500);
    MT_ASSERT_EQ(r.limit_ma, 2500); // a PPS current is the limit to regulate to
    MT_ASSERT(pdo_check_request(&t, pps_rdo(7, 21000, 3000), &r));
    MT_ASSERT(pdo_check_request(&t, pps_rdo(7, 5000, 1000), &r));
}

static void pps_request_rejected(void) {
    pdo_table_t t = build(100, BLADE_MAX_MV_ALL, 3000);
    pdo_request_t r;
    MT_ASSERT(!pdo_check_request(&t, pps_rdo(7, 21020, 1000), &r)); // above the range
    MT_ASSERT(!pdo_check_request(&t, pps_rdo(7, 4980, 1000), &r));  // below it
    MT_ASSERT(!pdo_check_request(&t, pps_rdo(7, 3300, 1000), &r));  // PD 3.2 ranges start at 5 V
    MT_ASSERT(!pdo_check_request(&t, pps_rdo(7, 20000, 3050), &r)); // more than offered
    MT_ASSERT(!pdo_check_request(&t, pps_rdo(7, 40960 + 9000, 1000), &r)); // voltage bit 11 set
}

// With the PPS Power Limited flag set, the converter holds the power limit
// at whatever voltage the sink picks
static void pps_power_limit_holds_the_current(void) {
    pdo_table_t t = build(100, BLADE_MAX_MV_ALL, 5000);
    pdo_request_t r;
    MT_ASSERT(pdo_check_request(&t, pps_rdo(7, 21000, 5000), &r));
    MT_ASSERT_EQ(r.op_ma, 5000);
    MT_ASSERT_EQ(r.limit_ma, 4761); // 100 W at 21 V
    MT_ASSERT(pdo_check_request(&t, pps_rdo(7, 20000, 5000), &r));
    MT_ASSERT_EQ(r.limit_ma, 5000);
    MT_ASSERT(pdo_check_request(&t, pps_rdo(7, 10000, 5000), &r));
    MT_ASSERT_EQ(r.limit_ma, 5000);
}

static void avs_request(void) {
    pdo_table_t t = build(60, BLADE_MAX_MV_ALL, 5000); // 4 A up to 15 V, 3 A above
    MT_ASSERT_EQ(pdo_avs_ma_15v(t.obj[5]), 4000);
    MT_ASSERT_EQ(pdo_avs_ma_20v(t.obj[5]), 3000);
    pdo_request_t r;
    MT_ASSERT(pdo_check_request(&t, avs_rdo(6, 12300, 4000), &r));
    MT_ASSERT_EQ(r.kind, PDO_AVS);
    MT_ASSERT_EQ(r.position, 6);
    MT_ASSERT_EQ(r.mv, 12300);
    MT_ASSERT_EQ(r.op_ma, 4000);
    MT_ASSERT_EQ(r.limit_ma, 4000); // like a fixed object: what it advertises at this voltage
    MT_ASSERT(pdo_check_request(&t, avs_rdo(6, 9000, 1000), &r));
    MT_ASSERT(pdo_check_request(&t, avs_rdo(6, 15000, 4000), &r)); // 15 V is in both halves
    MT_ASSERT_EQ(r.limit_ma, 4000);
    MT_ASSERT(pdo_check_request(&t, avs_rdo(6, 18700, 3000), &r));
    MT_ASSERT_EQ(r.limit_ma, 3000);
    MT_ASSERT(pdo_check_request(&t, avs_rdo(6, 20000, 3000), &r));
}

static void avs_request_rejected(void) {
    pdo_table_t t = build(60, BLADE_MAX_MV_ALL, 5000);
    pdo_request_t r;
    MT_ASSERT(!pdo_check_request(&t, avs_rdo(6, 12350, 1000), &r)); // not a 100 mV step
    MT_ASSERT(!pdo_check_request(&t, avs_rdo(6, 8900, 1000), &r));  // below 9 V
    MT_ASSERT(!pdo_check_request(&t, avs_rdo(6, 20100, 1000), &r)); // above 20 V
    MT_ASSERT(!pdo_check_request(&t, avs_rdo(6, 12300, 4050), &r)); // more than up to 15 V
    MT_ASSERT(!pdo_check_request(&t, avs_rdo(6, 15100, 3050), &r)); // more than above 15 V

    t = build(60, 15000, 5000); // the range stops at 15 V
    MT_ASSERT(pdo_is_avs(t.obj[4]));
    MT_ASSERT(pdo_check_request(&t, avs_rdo(5, 15000, 1000), &r));
    MT_ASSERT(!pdo_check_request(&t, avs_rdo(5, 15100, 1000), &r));
}

static void positions_follow_the_table_sent(void) {
    pdo_table_t t = build(60, 9000, 3000);
    pdo_request_t r;
    MT_ASSERT(pdo_check_request(&t, fixed_rdo(2, 2000), &r));
    MT_ASSERT_EQ(r.mv, 9000);
    MT_ASSERT(!pdo_check_request(&t, fixed_rdo(3, 2000), &r)); // 12 V was never offered
}

static void reported_power(void) {
    pdo_table_t t = build(100, BLADE_MAX_MV_ALL, 5000);
    MT_ASSERT_EQ(pdo_reported_w(&t), 100);
    t = build(100, BLADE_MAX_MV_ALL, 3000);
    MT_ASSERT_EQ(pdo_reported_w(&t), 60);
    t = build(40, BLADE_MAX_MV_ALL, 5000);
    MT_ASSERT_EQ(pdo_reported_w(&t), 40);
    t = build(100, 9000, 3000);
    MT_ASSERT_EQ(pdo_reported_w(&t), 27);
    t = build(0, BLADE_MAX_MV_ALL, 3000);
    MT_ASSERT_EQ(pdo_reported_w(&t), 0);
}

void run_pdo_tests(void) {
    mt_run("pdo: the full table", full_table);
    mt_run("pdo: the power limit sets each current", the_power_limit_sets_each_current);
    mt_run("pdo: no object passes the power limit", no_object_passes_the_power_limit);
    mt_run("pdo: a cable without an e-marker stops at 3 A", a_cable_without_an_e_marker_stops_at_3a);
    mt_run("pdo: power limit bounds", power_limit_bounds);
    mt_run("pdo: voltage cap", voltage_cap);
    mt_run("pdo: fixed request", fixed_request);
    mt_run("pdo: fixed request rejected", fixed_request_rejected);
    mt_run("pdo: PPS request", pps_request);
    mt_run("pdo: PPS request rejected", pps_request_rejected);
    mt_run("pdo: the PPS power limit holds the current", pps_power_limit_holds_the_current);
    mt_run("pdo: AVS request", avs_request);
    mt_run("pdo: AVS request rejected", avs_request_rejected);
    mt_run("pdo: positions follow the table sent", positions_follow_the_table_sent);
    mt_run("pdo: reported power", reported_power);
}
