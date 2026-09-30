#include "board.h"
#include "microtest.h"
#include "ovp.h"

static void threshold_tracks_the_contract(void) {
    MT_ASSERT_EQ(ovp_threshold_mv(5000), 6000);
    MT_ASSERT_EQ(ovp_threshold_mv(9000), 10800);
    MT_ASSERT_EQ(ovp_threshold_mv(15000), 18000);
    MT_ASSERT_EQ(ovp_threshold_mv(3300), 3960);
}

static void threshold_stops_at_the_ceiling(void) {
    MT_ASSERT_EQ(ovp_threshold_mv(20000), OVP_CEILING_MV);
    MT_ASSERT_EQ(ovp_threshold_mv(21000), OVP_CEILING_MV);
}

static void transition_covers_the_higher_voltage(void) {
    MT_ASSERT_EQ(ovp_transition_mv(5000, 15000), 18000);
    MT_ASSERT_EQ(ovp_transition_mv(15000, 5000), 18000);
    MT_ASSERT_EQ(ovp_transition_mv(9000, 9000), 10800);
}

static void dac_code_rounds_down(void) {
    // 6.0 V / 11 = 545.45 mV of 3300 mV = 676.86 counts
    MT_ASSERT_EQ(ovp_dac_code(6000, 3300), 676);
    // the pin voltage the code produces never exceeds the one asked for
    for (uint32_t mv = 3960; mv <= OVP_CEILING_MV; mv += 40) {
        uint32_t code = ovp_dac_code(mv, 3300);
        MT_ASSERT(code * 3300u * SENSE_DIV_NUM <= mv * 4095u);
    }
}

static void dac_code_follows_the_rail(void) {
    MT_ASSERT(ovp_dac_code(18000, 3400) < ovp_dac_code(18000, 3300));
    MT_ASSERT(ovp_dac_code(18000, 3200) > ovp_dac_code(18000, 3300));
    MT_ASSERT_EQ(ovp_dac_code(18000, 0), 0);      // no rail reading: threshold at 0 V
    MT_ASSERT_EQ(ovp_dac_code(60000, 3300), 4095);
}

void run_ovp_tests(void) {
    mt_run("ovp: threshold is 1.2 x the contract", threshold_tracks_the_contract);
    mt_run("ovp: threshold stops at the 23 V ceiling", threshold_stops_at_the_ceiling);
    mt_run("ovp: a transition is covered by the higher voltage", transition_covers_the_higher_voltage);
    mt_run("ovp: DAC code rounds down", dac_code_rounds_down);
    mt_run("ovp: DAC code follows the measured rail", dac_code_follows_the_rail);
}
