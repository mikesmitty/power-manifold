#include <math.h>
#include <stdlib.h>

#include "microtest.h"
#include "sense.h"

static uint16_t counts(double pin_mv, double vdda_mv) {
    return (uint16_t)lround(pin_mv / vdda_mv * 4095.0);
}

static void rail_from_vrefint(void) {
    // VREFINT 1.212 V: calibration word taken at 3.0 V, sample at 3.3 V
    uint16_t cal = counts(1212, 3000), raw = counts(1212, 3300);
    long vdda = (long)sense_vdda_mv(raw, cal);
    MT_ASSERT(labs(vdda - 3300) <= 3);
    MT_ASSERT_EQ(sense_vdda_mv(0, cal), 0);
}

static void divider_scaling(void) {
    // 20 V on the rail is 1.818 V at the pin
    long mv = (long)sense_rail_mv(counts(20000.0 / 11, 3300), 3300);
    MT_ASSERT(labs(mv - 20000) <= 10);
    MT_ASSERT_EQ(sense_rail_mv(0, 3300), 0);
    // full scale is 36.3 V: above anything the port can carry
    MT_ASSERT_EQ(sense_rail_mv(4095, 3300), 36300);
    // a low rail reads proportionally lower
    MT_ASSERT(sense_rail_mv(2000, 3200) < sense_rail_mv(2000, 3300));
}

static void port_current(void) {
    // 5 A x 7 mOhm x 42 = 1.47 V
    long ma = (long)sense_iana_ma(counts(1470, 3300), 3300);
    MT_ASSERT(labs(ma - 5000) <= 5);
    MT_ASSERT_EQ(sense_iana_ma(0, 3300), 0);
}

static double ntc_counts(double deg_c) {
    double r = 10000.0 * exp(3380.0 * (1.0 / (deg_c + 273.15) - 1.0 / 298.15));
    return 4095.0 * r / (r + 10000.0);
}

static void ntc_follows_the_curve(void) {
    MT_ASSERT_EQ(sense_ntc_dc(2048), 250);
    for (double t = -39.0; t <= 124.0; t += 0.7) {
        long got = sense_ntc_dc((uint16_t)lround(ntc_counts(t)));
        MT_ASSERT(labs(got - lround(t * 10)) <= 10); // within 1 degC, one count of rounding in it
    }
}

static void ntc_out_of_range(void) {
    MT_ASSERT_EQ(sense_ntc_dc(4095), SENSE_TEMP_OPEN_DC); // no thermistor
    MT_ASSERT_EQ(sense_ntc_dc(0), SENSE_TEMP_OPEN_DC);    // shorted
    MT_ASSERT_EQ(sense_ntc_dc((uint16_t)lround(ntc_counts(-45))), -400);
    MT_ASSERT_EQ(sense_ntc_dc((uint16_t)lround(ntc_counts(140))), 1250);
}

static void die_temperature(void) {
    uint16_t cal1 = 1037, cal2 = 1384; // 30 and 130 degC at 3.0 V
    MT_ASSERT_EQ(sense_mcu_dc(cal1, 3000, cal1, cal2), 300);
    MT_ASSERT_EQ(sense_mcu_dc(cal2, 3000, cal1, cal2), 1300);
    // the same die at 80 degC, sampled on a 3.3 V rail
    uint16_t raw = (uint16_t)lround((cal1 + (cal2 - cal1) * 0.5) * 3000.0 / 3300.0);
    long got = sense_mcu_dc(raw, 3300, cal1, cal2);
    MT_ASSERT(labs(got - 800) <= 5);
    MT_ASSERT_EQ(sense_mcu_dc(raw, 3300, 0xFFFF, 0xFFFF), SENSE_TEMP_OPEN_DC);
}

void run_sense_tests(void) {
    mt_run("sense: rail voltage from VREFINT", rail_from_vrefint);
    mt_run("sense: divider scaling", divider_scaling);
    mt_run("sense: port current from IANA", port_current);
    mt_run("sense: NTC follows the B3380 curve", ntc_follows_the_curve);
    mt_run("sense: NTC open and shorted", ntc_out_of_range);
    mt_run("sense: die temperature", die_temperature);
}
