#include "blade_regs.h"
#include "board.h"
#include "dac.h"
#include "fake_board.h"
#include "fake_bus.h"
#include "hw.h"
#include "microtest.h"
#include "ovp.h"
#include "power.h"
#include "tps55288.h"

// The power path on the simulated board: what it does, and the order it
// does it in.

static void bring_up(void) {
    fake_board_reset();
    power_init();
    (void)power_port_up();
    (void)power_converter_up();
    (void)power_cc_mode(TCPP02_NORMAL);
}

static uint8_t tcpp_ctrl(void) { return fake_bus_peek(ADDR_TCPP02, TCPP02_REG_ACK); }
static uint8_t conv_mode(void) { return fake_bus_peek(ADDR_TPS55288, TPS55288_REG_MODE); }

static void port_comes_up_in_hibernate(void) {
    fake_board_reset();
    power_init();
    MT_ASSERT(!fake_bus_peek(ADDR_TCPP02, TCPP02_REG_ACK));
    MT_ASSERT_EQ(power_port_up(), POWER_OK);
    MT_ASSERT_EQ(tcpp_ctrl(), 0x08); // CC switches open, VBUS switch open
    MT_ASSERT_EQ(fake_board_trip_mv() / 100, 59); // threshold for 5 V set before TCPP_EN rose
    MT_ASSERT(!power_vbus_is_on());
}

static void port_up_with_the_line_held(void) {
    fake_board_reset();
    power_init();
    fake_board_set_vout_mv(9000); // above the 5 V threshold: the comparator holds TCPP_EN
    MT_ASSERT_EQ(power_port_up(), POWER_TRIPPED);
    MT_ASSERT(power_poll_faults() & BLADE_FAULT_OVP);
}

static void port_up_without_a_tcpp02(void) {
    fake_board_reset();
    power_init();
    fake_board_fit_tcpp02(false);
    MT_ASSERT_EQ(power_port_up(), POWER_BUS);
}

static void nothing_without_the_converter(void) {
    fake_board_reset();
    power_init();
    MT_ASSERT_EQ(power_port_up(), POWER_OK);
    MT_ASSERT_EQ(power_cc_mode(TCPP02_NORMAL), POWER_OK);
    MT_ASSERT_EQ(power_vbus_on(), POWER_NOT_READY); // EN low: no converter
    MT_ASSERT(!(tcpp_ctrl() & TCPP02_CTRL_GDP));
    fake_bus_set_present(ADDR_TPS55288, false);
    MT_ASSERT_EQ(power_converter_up(), POWER_BUS);
    MT_ASSERT(!power_converter_ready());
}

static void vbus_on_ramps_behind_the_switch(void) {
    bring_up();
    MT_ASSERT_EQ(power_vbus_on(), POWER_OK);
    MT_ASSERT(power_vbus_is_on());
    MT_ASSERT(tcpp_ctrl() & TCPP02_CTRL_GDP);
    MT_ASSERT(conv_mode() & TPS55288_MODE_OE);
    MT_ASSERT_EQ(conv_mode() & 0x0F, 0); // as strapped
    MT_ASSERT(!fake_board_output_on_with_switch_open());
    MT_ASSERT(fake_board_vbus_mv() >= 4750 && fake_board_vbus_mv() <= 5250);
    MT_ASSERT_EQ(power_setpoint_mv(), 5000);
    MT_ASSERT(!fake_board_comparator_tripped());
}

static void vbus_on_gives_up(void) {
    bring_up();
    fake_board_stick_vbus(true);
    MT_ASSERT_EQ(power_vbus_on(), POWER_TIMEOUT);
    MT_ASSERT(!power_vbus_is_on());
    MT_ASSERT(!(conv_mode() & TPS55288_MODE_OE));
    MT_ASSERT(!(tcpp_ctrl() & TCPP02_CTRL_GDP));
}

static void step_up(void) {
    bring_up();
    (void)power_vbus_on();
    MT_ASSERT_EQ(power_set(20000, 3000, false), POWER_OK);
    MT_ASSERT(fake_board_vbus_mv() >= 19000 && fake_board_vbus_mv() <= 21000);
    MT_ASSERT(!fake_board_comparator_tripped()); // the threshold went up first
    MT_ASSERT_EQ(fake_board_trip_mv() / 100, OVP_CEILING_MV / 100 - 1);
    // fixed contract: 110 % of 3 A
    MT_ASSERT_EQ(tps55288_ilim_ma(fake_bus_peek(ADDR_TPS55288, TPS55288_REG_IOUT_LIMIT)), 3300);
}

static void step_down_unloaded(void) {
    bring_up();
    (void)power_vbus_on();
    (void)power_set(20000, 3000, false);
    MT_ASSERT_EQ(power_set(9000, 2000, false), POWER_OK); // nothing but the converter to pull it down
    MT_ASSERT(fake_board_vbus_mv() >= 8550 && fake_board_vbus_mv() <= 9450);
    MT_ASSERT(!fake_board_comparator_tripped()); // the threshold came down last
    MT_ASSERT_EQ(fake_board_trip_mv() / 100, 107);
    MT_ASSERT_EQ(conv_mode() & 0x0F, 0); // back as strapped
    MT_ASSERT(conv_mode() & TPS55288_MODE_OE);
}

static void step_down_that_stalls(void) {
    bring_up();
    (void)power_vbus_on();
    (void)power_set(20000, 3000, false);
    fake_board_stick_vbus(true);
    MT_ASSERT_EQ(power_set(5000, 3000, false), POWER_TIMEOUT);
    // VBUS is still at 20 V: the threshold still covers it
    MT_ASSERT(fake_board_trip_mv() > 20000);
    MT_ASSERT(!fake_board_comparator_tripped());
}

static void pps_contract(void) {
    bring_up();
    (void)power_vbus_on();
    MT_ASSERT_EQ(power_set(8400, 2250, true), POWER_OK);
    MT_ASSERT(fake_board_vbus_mv() >= 8300 && fake_board_vbus_mv() <= 8500);
    // the requested current is the limit
    MT_ASSERT_EQ(tps55288_ilim_ma(fake_bus_peek(ADDR_TPS55288, TPS55288_REG_IOUT_LIMIT)), 2250);
    MT_ASSERT_EQ(power_set(8420, 2250, true), POWER_OK); // one step
    MT_ASSERT_EQ(power_set(3300, 1000, true), POWER_OK);
    MT_ASSERT(!fake_board_comparator_tripped());
}

static void small_fixed_current_gets_the_floor(void) {
    bring_up();
    (void)power_vbus_on();
    MT_ASSERT_EQ(power_set(5000, 100, false), POWER_OK);
    MT_ASSERT_EQ(tps55288_ilim_ma(fake_bus_peek(ADDR_TPS55288, TPS55288_REG_IOUT_LIMIT)),
                 POWER_LIMIT_FLOOR_MA);
}

static void set_needs_vbus(void) {
    bring_up();
    MT_ASSERT_EQ(power_set(9000, 3000, false), POWER_NOT_READY);
    MT_ASSERT(!(conv_mode() & TPS55288_MODE_OE));
}

static void vbus_off_discharges(void) {
    bring_up();
    (void)power_vbus_on();
    (void)power_set(20000, 3000, false);
    MT_ASSERT_EQ(power_vbus_off(), POWER_OK);
    MT_ASSERT(fake_board_vbus_mv() < POWER_VSAFE0V_MV);
    MT_ASSERT(!power_vbus_is_on());
    MT_ASSERT(!(tcpp_ctrl() & TCPP02_CTRL_GDP));
    MT_ASSERT(!(tcpp_ctrl() & TCPP02_CTRL_VBUSD)); // discharge released again
    MT_ASSERT(!(conv_mode() & TPS55288_MODE_OE));
    MT_ASSERT(conv_mode() & TPS55288_MODE_DISCHG);
    MT_ASSERT_EQ(fake_board_trip_mv() / 100, 59); // back to the 5 V threshold
    MT_ASSERT(!fake_board_comparator_tripped());
    // and on again
    MT_ASSERT_EQ(power_vbus_on(), POWER_OK);
    MT_ASSERT(!fake_board_output_on_with_switch_open());
}

static void vbus_off_without_the_converter(void) {
    bring_up();
    (void)power_vbus_on();
    fake_board_set_load(true);
    power_converter_lost(); // EN fell: the part is in shutdown
    fake_bus_set_present(ADDR_TPS55288, false);
    fake_bus_poke(ADDR_TPS55288, TPS55288_REG_MODE, 0x20);
    MT_ASSERT_EQ(power_vbus_off(), POWER_OK);
    MT_ASSERT(!(tcpp_ctrl() & TCPP02_CTRL_GDP));
}

static void over_voltage_trip(void) {
    bring_up();
    (void)power_vbus_on();
    (void)power_set(9000, 3000, false);
    MT_ASSERT_EQ(power_poll_faults(), 0);

    // the converter runs away: its reference jumps to 20 V behind the firmware's back
    uint16_t code = tps55288_ref_code(20000);
    fake_bus_poke(ADDR_TPS55288, TPS55288_REG_REF_LSB, (uint8_t)code);
    fake_bus_poke(ADDR_TPS55288, TPS55288_REG_REF_MSB, (uint8_t)(code >> 8));
    hw_delay_ms(10);

    MT_ASSERT(fake_board_comparator_tripped());
    MT_ASSERT(fake_board_peak_vbus_mv() < 13400); // the switch opened one slew step past 10.8 V
    MT_ASSERT(power_poll_faults() & BLADE_FAULT_OVP);
    MT_ASSERT_EQ(power_set(9000, 3000, false), POWER_TRIPPED);

    power_shutdown();
    MT_ASSERT(!(conv_mode() & TPS55288_MODE_OE));
    MT_ASSERT(!power_vbus_is_on());
}

static void trip_is_remembered(void) {
    bring_up();
    (void)power_vbus_on();
    hw_delay_ms(5);
    // a spike: above the threshold and gone again before anyone looked
    dac_set(0);
    dac_set(ovp_dac_code(ovp_threshold_mv(5000), 3300));
    MT_ASSERT(hw_tcpp_en_sense());
    MT_ASSERT(power_poll_faults() & BLADE_FAULT_OVP);
    // a fresh start clears it
    power_port_down();
    MT_ASSERT_EQ(power_port_up(), POWER_OK);
    MT_ASSERT_EQ(power_poll_faults(), 0);
}

static void part_faults(void) {
    bring_up();
    (void)power_vbus_on();
    fake_board_set_port_fault(true);
    fake_bus_poke(ADDR_TCPP02, TCPP02_REG_FLAGS,
                  TCPP02_FLAG_IS_TCPP02 | TCPP02_FLAG_OCP_VBUS | TCPP02_FLAG_OTP);
    MT_ASSERT_EQ(power_poll_faults(), BLADE_FAULT_OCP_VBUS | BLADE_FAULT_OTP_PORT);
    // outside normal mode the pin says something else
    (void)power_vbus_off();
    (void)power_cc_mode(TCPP02_LOW_POWER);
    MT_ASSERT_EQ(power_poll_faults(), 0);
    (void)power_cc_mode(TCPP02_NORMAL);
    fake_board_set_port_fault(false);

    fake_board_set_converter_fault(true);
    fake_bus_poke(ADDR_TPS55288, TPS55288_REG_STATUS, TPS55288_STATUS_SCP | TPS55288_STATUS_OCP);
    // the current limit alone is no fault: it is where a PPS contract lives
    MT_ASSERT_EQ(power_poll_faults(), BLADE_FAULT_CONV_SCP);
    fake_bus_set_present(ADDR_TPS55288, false);
    MT_ASSERT_EQ(power_poll_faults(), BLADE_FAULT_BUS);
}

static void vconn_follows_the_cable(void) {
    bring_up();
    MT_ASSERT_EQ(power_vconn(TCPP02_VCONN_CC2), POWER_OK);
    MT_ASSERT(tcpp_ctrl() & TCPP02_CTRL_V2);
    MT_ASSERT(!(tcpp_ctrl() & TCPP02_CTRL_V1));
    MT_ASSERT_EQ(power_cc_mode(TCPP02_LOW_POWER), POWER_OK); // detach
    MT_ASSERT_EQ(tcpp_ctrl(), 0x28); // VCONN went with it
}

void run_power_tests(void) {
    mt_run("power: the port comes up in hibernate", port_comes_up_in_hibernate);
    mt_run("power: port up with TCPP_EN held", port_up_with_the_line_held);
    mt_run("power: port up without a TCPP02", port_up_without_a_tcpp02);
    mt_run("power: nothing without the converter", nothing_without_the_converter);
    mt_run("power: VBUS ramps up behind the closed switch", vbus_on_ramps_behind_the_switch);
    mt_run("power: VBUS on gives up and shuts down", vbus_on_gives_up);
    mt_run("power: step up raises the threshold first", step_up);
    mt_run("power: step down, unloaded, lowers the threshold last", step_down_unloaded);
    mt_run("power: a step down that stalls keeps its threshold", step_down_that_stalls);
    mt_run("power: PPS contract", pps_contract);
    mt_run("power: a small fixed current gets the limit floor", small_fixed_current_gets_the_floor);
    mt_run("power: no contract without VBUS", set_needs_vbus);
    mt_run("power: VBUS off discharges to vSafe0V", vbus_off_discharges);
    mt_run("power: VBUS off with the converter in shutdown", vbus_off_without_the_converter);
    mt_run("power: over-voltage trip", over_voltage_trip);
    mt_run("power: a trip is remembered", trip_is_remembered);
    mt_run("power: TCPP02 and converter faults", part_faults);
    mt_run("power: VCONN", vconn_follows_the_cable);
}
