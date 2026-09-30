#include "board.h"
#include "fake_bus.h"
#include "microtest.h"
#include "tps55288.h"

// Register encodings against the figures printed in SLVSF01B, and the
// transfers the driver puts on the private bus.

static void ref_matches_datasheet(void) {
    MT_ASSERT_EQ(tps55288_ref_code(5000), 0x0D2);  // the reset value: 282 mV, 5 V
    MT_ASSERT_EQ(tps55288_ref_code(800), 0x000);   // 45 mV
    MT_ASSERT_EQ(tps55288_ref_mv(0x3C0), 20015);   // 1129 mV, "20 V"
    MT_ASSERT_EQ(tps55288_ref_mv(0x0D2), 5002);
}

static void ref_steps_are_pps_steps(void) {
    // every 20 mV PPS step lands within half a converter step (20.02 mV) of itself
    for (uint32_t mv = 3300; mv <= 21000; mv += 20) {
        long got = (long)tps55288_ref_mv(tps55288_ref_code(mv));
        long err = got - (long)mv;
        if (err < 0) err = -err;
        MT_ASSERT(err <= 11);
    }
}

static void ref_is_clamped(void) {
    MT_ASSERT_EQ(tps55288_ref_code(0), tps55288_ref_code(TPS55288_VOUT_MIN_MV));
    MT_ASSERT_EQ(tps55288_ref_code(30000), tps55288_ref_code(TPS55288_VOUT_MAX_MV));
    MT_ASSERT(tps55288_ref_mv(tps55288_ref_code(30000)) <= 21010);
}

static void limit_matches_datasheet(void) {
    MT_ASSERT_EQ(tps55288_ilim_code(5000), 0xE4);  // the reset value: 50 mV, enabled
    MT_ASSERT_EQ(tps55288_ilim_ma(0xE4), 5000);
    MT_ASSERT_EQ(tps55288_ilim_code(3049), 0x80 | 60); // rounds down
    MT_ASSERT_EQ(tps55288_ilim_code(9000), 0xFF);      // 6.35 A is the top
    MT_ASSERT_EQ(tps55288_ilim_ma(0xFF), TPS55288_IOUT_MAX_MA);
}

static void mode_as_strapped_leaves_the_pin_in_charge(void) {
    for (int on = 0; on < 2; on++) {
        for (int dis = 0; dis < 2; dis++) {
            tps55288_mode_t m = {.output_on = on, .discharge = dis};
            uint8_t b = tps55288_mode_byte(&m);
            MT_ASSERT_EQ(b & 0x0F, 0); // MODE, PFM, I2CADD, VCC: the MODE pin's
            MT_ASSERT_EQ(!!(b & TPS55288_MODE_OE), on);
            MT_ASSERT_EQ(!!(b & TPS55288_MODE_DISCHG), dis);
            MT_ASSERT(b & TPS55288_MODE_HICCUP);
        }
    }
}

static void forced_pwm_keeps_vcc_external(void) {
    tps55288_mode_t m = {.output_on = true, .forced_pwm = true};
    MT_ASSERT_EQ(tps55288_mode_byte(&m), 0xAB);
    // every byte that takes the settings from the pin's hands keeps VCC on
    // the slot rail and the address where the driver talks to it
    for (int i = 0; i < 8; i++) {
        tps55288_mode_t x = {.output_on = i & 1, .discharge = (i >> 1) & 1, .forced_pwm = (i >> 2) & 1};
        uint8_t b = tps55288_mode_byte(&x);
        if (!(b & TPS55288_MODE_BY_REG)) continue;
        MT_ASSERT(b & TPS55288_MODE_VCC_EXT);
        MT_ASSERT(!(b & TPS55288_MODE_I2CADD));
    }
}

static void init_leaves_the_output_off(void) {
    fake_bus_reset();
    fake_bus_poke(ADDR_TPS55288, TPS55288_REG_MODE, 0xA0); // as if left running
    MT_ASSERT(tps55288_probe());
    MT_ASSERT(tps55288_init(TPS55288_SR_2V5_PER_MS));
    MT_ASSERT_EQ(fake_bus_peek(ADDR_TPS55288, TPS55288_REG_MODE), 0x20);
    MT_ASSERT_EQ(fake_bus_peek(ADDR_TPS55288, TPS55288_REG_VOUT_FS), 0x03);
    MT_ASSERT_EQ(fake_bus_peek(ADDR_TPS55288, TPS55288_REG_CDC), 0xA0); // OCP indication masked
    MT_ASSERT_EQ(fake_bus_peek(ADDR_TPS55288, TPS55288_REG_REF_LSB), 0xD2);
    // the output goes off before anything else is touched
    MT_ASSERT_EQ(fake_bus_write_at(0)->data[0], TPS55288_REG_MODE);
}

static void voltage_is_one_transfer(void) {
    fake_bus_reset();
    MT_ASSERT(tps55288_set_mv(20000));
    MT_ASSERT_EQ(fake_bus_write_count(), 1);
    const fake_bus_write_t *w = fake_bus_write_at(0);
    MT_ASSERT_EQ(w->len, 3); // pointer, LSB, then the MSB that loads the DAC
    MT_ASSERT_EQ(w->data[0], TPS55288_REG_REF_LSB);
    MT_ASSERT_EQ(w->data[1] | (w->data[2] << 8), tps55288_ref_code(20000));
}

static void absent_part_fails(void) {
    fake_bus_reset();
    fake_bus_set_present(ADDR_TPS55288, false);
    uint8_t st;
    MT_ASSERT(!tps55288_probe());
    MT_ASSERT(!tps55288_init(TPS55288_SR_2V5_PER_MS));
    MT_ASSERT(!tps55288_set_limit_ma(3000));
    MT_ASSERT(!tps55288_read_status(&st));
}

void run_tps55288_tests(void) {
    mt_run("tps55288: reference codes match the datasheet", ref_matches_datasheet);
    mt_run("tps55288: every PPS step is reachable", ref_steps_are_pps_steps);
    mt_run("tps55288: reference is clamped to the output range", ref_is_clamped);
    mt_run("tps55288: current limit codes match the datasheet", limit_matches_datasheet);
    mt_run("tps55288: as strapped, MODE leaves the pin in charge", mode_as_strapped_leaves_the_pin_in_charge);
    mt_run("tps55288: forced PWM keeps VCC external", forced_pwm_keeps_vcc_external);
    mt_run("tps55288: init leaves the output off", init_leaves_the_output_off);
    mt_run("tps55288: a voltage change is one transfer", voltage_is_one_transfer);
    mt_run("tps55288: an absent part fails every call", absent_part_fails);
}
