#include <string.h>

#include "microtest.h"
#include "settings.h"
#include "vin.h"
#include "vin_hw.h"

// The bus-voltage monitor against an emulated ADC: scaling, calibration,
// averaging, the low/high flags with their hysteresis, and a board without
// the divider.

static bool emu_fitted;
static uint16_t emu_raw, emu_raw_alt; // alternate between the two per conversion when alt != 0
static unsigned emu_reads;

bool vin_hw_init(void) { return emu_fitted; }
uint16_t vin_hw_read(void) {
    emu_reads++;
    return (emu_raw_alt && (emu_reads & 1)) ? emu_raw_alt : emu_raw;
}

static uint32_t now;

static void start(bool fitted, uint16_t raw) {
    emu_fitted = fitted;
    emu_raw = raw;
    emu_raw_alt = 0;
    emu_reads = 0;
    g_settings.vin_cal = VIN_CAL_DEFAULT;
    now = 0;
    vin_init();
}

static void run_ms(uint32_t ms) { // the main loop's cadence
    for (uint32_t t = 0; t < ms; t += 10) {
        now += 10;
        vin_poll(now);
    }
}

// counts for a bus voltage at the nominal scale (42.9 V full scale)
static uint16_t counts(uint32_t mv) { return (uint16_t)((mv * 4096u + 21450u) / 42900u); }

static void test_not_fitted(void) {
    start(false, 2048);
    run_ms(2000);
    MT_ASSERT(!vin_fitted());
    MT_ASSERT_EQ(vin_mv(), 0);
    MT_ASSERT_EQ(emu_reads, 0);
    MT_ASSERT(!vin_low() && !vin_high());
    MT_ASSERT(!strcmp(vin_status_str(), "not fitted"));
    MT_ASSERT_EQ(vin_cal_for(24000), 0);
}

static void test_scale(void) {
    start(true, 2048); // half scale: 21.45 V
    MT_ASSERT(vin_fitted());
    MT_ASSERT(!strcmp(vin_status_str(), "no sample yet"));
    run_ms(10);
    MT_ASSERT(vin_mv() >= 21440 && vin_mv() <= 21460); // one sample is enough for a reading
    MT_ASSERT_EQ(emu_reads, 8);
    run_ms(1000);
    MT_ASSERT_EQ(emu_reads, 88); // a sample every 100 ms, eight conversions each
    MT_ASSERT(vin_mv() >= 21440 && vin_mv() <= 21460);
    MT_ASSERT_EQ(vin_raw(), 2048);
    MT_ASSERT(!strcmp(vin_status_str(), "21.45 V"));
    MT_ASSERT(!vin_low() && !vin_high());
}

static void test_calibration(void) {
    start(true, counts(24000));
    run_ms(1100);
    uint32_t nominal = vin_mv();
    MT_ASSERT(nominal >= 23985 && nominal <= 24015);
    // the meter says 24.50 V: the cal that gets there, and its effect
    uint16_t c = vin_cal_for(24500);
    MT_ASSERT(c >= 1019 && c <= 1023);
    g_settings.vin_cal = c;
    MT_ASSERT(vin_mv() >= 24480 && vin_mv() <= 24520);
    // clamped both ways, and an out-of-range stored value falls back to unity
    MT_ASSERT_EQ(vin_cal_for(60000), VIN_CAL_MAX);
    MT_ASSERT_EQ(vin_cal_for(1000), VIN_CAL_MIN);
    g_settings.vin_cal = 5;
    MT_ASSERT(vin_mv() >= 23985 && vin_mv() <= 24015);
}

static void test_averaging(void) {
    start(true, 2000);
    emu_raw_alt = 2100; // conversions alternate: the sample is their mean
    run_ms(1100);
    uint32_t mv = vin_mv();
    MT_ASSERT(mv >= 21460 && mv <= 21480); // 2050 counts = 21.47 V
    // a step settles over the one-second window, not at once
    emu_raw_alt = 0;
    emu_raw = counts(28000);
    run_ms(500);
    MT_ASSERT(vin_mv() > 23000 && vin_mv() < 26000);
    run_ms(600);
    MT_ASSERT(vin_mv() >= 27985 && vin_mv() <= 28015);
}

static void test_low_with_hysteresis(void) {
    start(true, counts(24000));
    run_ms(1100);
    MT_ASSERT(!vin_low());
    emu_raw = counts(18000);
    run_ms(1100);
    MT_ASSERT(vin_low());
    MT_ASSERT(!strcmp(vin_status_str(), "18.00 V LOW"));
    emu_raw = counts(19300); // above the threshold, inside the hysteresis band
    run_ms(1100);
    MT_ASSERT(vin_low());
    emu_raw = counts(19800);
    run_ms(1100);
    MT_ASSERT(!vin_low());
}

static void test_high_with_hysteresis(void) {
    start(true, counts(24000));
    run_ms(1100);
    emu_raw = counts(29500);
    run_ms(1100);
    MT_ASSERT(vin_high());
    MT_ASSERT(!vin_low());
    emu_raw = counts(28800);
    run_ms(1100);
    MT_ASSERT(vin_high());
    emu_raw = counts(28300);
    run_ms(1100);
    MT_ASSERT(!vin_high());
    MT_ASSERT(vin_mv() >= 28285 && vin_mv() <= 28315);
    MT_ASSERT(strstr(vin_status_str(), "HIGH") == NULL);
}

void run_vin_tests(void) {
    mt_run("vin: not fitted", test_not_fitted);
    mt_run("vin: scale and cadence", test_scale);
    mt_run("vin: calibration from a meter reading", test_calibration);
    mt_run("vin: averaging and settling", test_averaging);
    mt_run("vin: low flag with hysteresis", test_low_with_hysteresis);
    mt_run("vin: high flag with hysteresis", test_high_with_hysteresis);
}
