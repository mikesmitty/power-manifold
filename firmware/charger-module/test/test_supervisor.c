#include "blade.h"
#include "board.h"
#include "fake_board.h"
#include "fake_bus.h"
#include "fake_stack.h"
#include "hw.h"
#include "microtest.h"
#include "port.h"
#include "power.h"
#include "regmap.h"
#include "sense.h"
#include "stack.h"
#include "supervisor.h"
#include "tcpp02.h"

// The supervisor on the simulated board, with the controller on the other
// side of the register file.

static void ctl_write(uint8_t reg, const uint8_t *data, unsigned n) {
    regmap_addressed(false);
    regmap_rx(reg);
    for (unsigned i = 0; i < n; i++) regmap_rx(data[i]);
    regmap_stop();
}

static uint8_t ctl_read8(uint8_t reg) {
    regmap_addressed(false);
    regmap_rx(reg);
    regmap_addressed(true);
    uint8_t v = regmap_tx();
    regmap_stop();
    return v;
}

static uint16_t ctl_read16(uint8_t reg) {
    regmap_addressed(false);
    regmap_rx(reg);
    regmap_addressed(true);
    uint16_t v = regmap_tx();
    v |= (uint16_t)(regmap_tx() << 8);
    regmap_stop();
    return v;
}

static void ctl_configure(uint16_t max_ma, uint16_t max_mv, bool port_en) {
    uint8_t limits[4] = {(uint8_t)max_ma, (uint8_t)(max_ma >> 8), (uint8_t)max_mv, (uint8_t)(max_mv >> 8)};
    ctl_write(BLADE_REG_MAX_MA, limits, sizeof limits);
    uint8_t control = port_en ? BLADE_CTL_PORT_EN : 0;
    ctl_write(BLADE_REG_CONTROL, &control, 1);
}

static void ctl_command(uint8_t cmd) { ctl_write(BLADE_REG_COMMAND, &cmd, 1); }

static void run_ms(unsigned ms) {
    while (ms--) {
        supervisor_run();
        hw_delay_ms(1);
    }
}

static void boot(void) {
    fake_board_reset();
    fake_stack_reset();
    fake_bus_set_present(ADDR_TPS55288, false); // EN is low: the converter is in shutdown
    regmap_init(BLADE_RESET_POWER);
    port_init();
    power_init();
    supervisor_init();
}

static void en_high(void) {
    fake_board_set_en(true);
    fake_bus_set_present(ADDR_TPS55288, true);
}

static void en_low(void) {
    fake_board_set_en(false);
    fake_bus_set_present(ADDR_TPS55288, false);
}

static void boots_dark_and_quiet(void) {
    boot();
    run_ms(200);
    MT_ASSERT(!stack_port_armed());
    MT_ASSERT(!fake_board_alert()); // the restart is latched, and pulls nothing
    MT_ASSERT_EQ(ctl_read16(BLADE_REG_FAULT), BLADE_FAULT_RESET);
    MT_ASSERT_EQ(fake_bus_peek(ADDR_TCPP02, TCPP02_REG_ACK), 0x08); // hibernate
    MT_ASSERT(hw_tcpp_en_sense());
    MT_ASSERT(fake_board_watchdog_feeds() > 100);
}

static void en_alone_does_not_arm(void) {
    boot();
    en_high();
    run_ms(200);
    MT_ASSERT(power_converter_ready());
    MT_ASSERT(!stack_port_armed()); // the controller has not spoken
    MT_ASSERT(ctl_read8(BLADE_REG_STATUS) & BLADE_ST_EN);
}

static void configuration_alone_does_not_arm(void) {
    boot();
    ctl_configure(3000, BLADE_MAX_MV_ALL, true);
    run_ms(200);
    MT_ASSERT(!stack_port_armed()); // EN is low
    MT_ASSERT_EQ(port_table()->count, 7); // but the table is ready for it
}

static void arms_when_everything_is_there(void) {
    boot();
    ctl_configure(3000, BLADE_MAX_MV_ALL, true);
    en_high();
    run_ms(20);
    MT_ASSERT(!stack_port_armed()); // the converter gets its time to wake
    run_ms(100);
    MT_ASSERT(stack_port_armed());
    MT_ASSERT_EQ(fake_bus_peek(ADDR_TCPP02, TCPP02_REG_ACK), 0x28); // waiting for a sink
    MT_ASSERT(!fake_board_alert());
}

static void a_ceiling_of_zero_is_off(void) {
    boot();
    en_high();
    ctl_configure(0, BLADE_MAX_MV_ALL, true);
    run_ms(200);
    MT_ASSERT(!stack_port_armed());
    ctl_configure(3000, BLADE_MAX_MV_ALL, false);
    run_ms(10);
    MT_ASSERT(!stack_port_armed());
    ctl_configure(3000, BLADE_MAX_MV_ALL, true);
    run_ms(10);
    MT_ASSERT(stack_port_armed());
}

static void port_disable_and_en_low_disarm(void) {
    boot();
    en_high();
    ctl_configure(3000, BLADE_MAX_MV_ALL, true);
    run_ms(200);
    MT_ASSERT(stack_port_armed());

    ctl_configure(3000, BLADE_MAX_MV_ALL, false);
    run_ms(5);
    MT_ASSERT(!stack_port_armed());
    MT_ASSERT_EQ(fake_bus_peek(ADDR_TCPP02, TCPP02_REG_ACK), 0x48); // hibernate, VBUS discharge

    ctl_configure(3000, BLADE_MAX_MV_ALL, true);
    run_ms(5);
    MT_ASSERT(stack_port_armed());

    en_low();
    run_ms(5);
    MT_ASSERT(!stack_port_armed());
    MT_ASSERT(!power_converter_ready());

    en_high(); // and back: the converter is set up afresh
    run_ms(200);
    MT_ASSERT(stack_port_armed());
    MT_ASSERT(!fake_board_alert());
}

static void new_limits_go_to_the_sink(void) {
    boot();
    en_high();
    ctl_configure(3000, BLADE_MAX_MV_ALL, true);
    run_ms(200);
    unsigned sent = fake_stack_capabilities_sent();

    ctl_configure(3000, BLADE_MAX_MV_ALL, true); // the same again
    run_ms(5);
    MT_ASSERT_EQ(fake_stack_capabilities_sent(), sent);

    ctl_configure(1500, BLADE_MAX_MV_ALL, true); // the budget arbiter throttles the port
    run_ms(5);
    MT_ASSERT(fake_stack_capabilities_sent() > sent);
    MT_ASSERT_EQ(pdo_fixed_ma(port_table()->obj[0]), 1500);
    MT_ASSERT(stack_port_armed());
}

static void commands_reach_the_stack(void) {
    boot();
    en_high();
    ctl_configure(3000, BLADE_MAX_MV_ALL, true);
    run_ms(200);
    unsigned sent = fake_stack_capabilities_sent();
    ctl_command(BLADE_CMD_SRC_CAP);
    ctl_command(BLADE_CMD_HARD_RESET);
    run_ms(5);
    MT_ASSERT_EQ(fake_stack_capabilities_sent(), sent + 1);
    MT_ASSERT_EQ(fake_stack_hard_resets(), 1);
}

static void over_voltage_trip_takes_the_port_down(void) {
    boot();
    en_high();
    ctl_configure(3000, BLADE_MAX_MV_ALL, true);
    run_ms(200);

    fake_board_set_vout_mv(9000); // above the threshold, however briefly
    fake_board_set_vout_mv(0);
    MT_ASSERT(hw_tcpp_en_sense()); // the comparator has let go again
    run_ms(2);
    MT_ASSERT(!stack_port_armed());
    MT_ASSERT(!hw_tcpp_en_sense()); // TCPP_EN taken low: the TCPP02 is off
    MT_ASSERT(fake_board_alert());
    MT_ASSERT(ctl_read16(BLADE_REG_FAULT) & BLADE_FAULT_OVP);
    MT_ASSERT(ctl_read8(BLADE_REG_STATUS) & BLADE_ST_FAULT);

    run_ms(500);
    MT_ASSERT(!stack_port_armed()); // it stays down by itself

    ctl_command(BLADE_CMD_CLEAR_FAULTS);
    run_ms(5);
    MT_ASSERT(hw_tcpp_en_sense());
    MT_ASSERT(stack_port_armed());
    MT_ASSERT(!fake_board_alert());
    MT_ASSERT_EQ(ctl_read16(BLADE_REG_FAULT), 0);
}

static void a_fault_that_persists_comes_back(void) {
    boot();
    en_high();
    ctl_configure(3000, BLADE_MAX_MV_ALL, true);
    run_ms(200);
    fake_board_set_temps_dc(250, TEMP_LIMIT_PLUG_DC + 1);
    run_ms(60);
    MT_ASSERT(!stack_port_armed());
    MT_ASSERT_EQ(ctl_read16(BLADE_REG_FAULT) & BLADE_FAULT_ALERTING, BLADE_FAULT_OT_PLUG);

    ctl_command(BLADE_CMD_CLEAR_FAULTS); // still hot
    run_ms(20);
    MT_ASSERT(!stack_port_armed());
    MT_ASSERT(ctl_read16(BLADE_REG_FAULT) & BLADE_FAULT_OT_PLUG);

    fake_board_set_temps_dc(250, 400);
    ctl_command(BLADE_CMD_CLEAR_FAULTS);
    run_ms(20);
    MT_ASSERT(stack_port_armed());
}

static void a_hot_reading_has_to_hold(void) {
    boot();
    en_high();
    ctl_configure(3000, BLADE_MAX_MV_ALL, true);
    run_ms(200);

    // Three samples past the limit, then back: a glitch, not a fault
    fake_board_set_temps_dc(TEMP_LIMIT_CONV_DC + 1, 250);
    run_ms(30);
    fake_board_set_temps_dc(250, 250);
    run_ms(100);
    MT_ASSERT(stack_port_armed());
    MT_ASSERT_EQ(ctl_read16(BLADE_REG_FAULT) & BLADE_FAULT_ALERTING, 0);

    // A break in the run starts the count over
    fake_board_set_temps_dc(TEMP_LIMIT_CONV_DC + 1, 250);
    run_ms(30);
    fake_board_set_temps_dc(250, 250);
    run_ms(10);
    fake_board_set_temps_dc(TEMP_LIMIT_CONV_DC + 1, 250);
    run_ms(30);
    fake_board_set_temps_dc(250, 250);
    run_ms(100);
    MT_ASSERT(stack_port_armed());
    MT_ASSERT_EQ(ctl_read16(BLADE_REG_FAULT) & BLADE_FAULT_ALERTING, 0);

    // Held past the limit, it trips
    fake_board_set_temps_dc(TEMP_LIMIT_CONV_DC + 1, 250);
    run_ms(60);
    MT_ASSERT(!stack_port_armed());
    MT_ASSERT_EQ(ctl_read16(BLADE_REG_FAULT) & BLADE_FAULT_ALERTING, BLADE_FAULT_OT_CONV);
}

static void a_blind_sensor_is_a_fault(void) {
    boot();
    fake_board_set_temps_dc(SENSE_TEMP_OPEN_DC, 250);
    en_high();
    ctl_configure(3000, BLADE_MAX_MV_ALL, true);
    run_ms(200);
    MT_ASSERT(!stack_port_armed());
    MT_ASSERT(ctl_read16(BLADE_REG_FAULT) & BLADE_FAULT_OT_CONV);
    MT_ASSERT(fake_board_alert());
}

static void a_missing_part_is_a_fault(void) {
    fake_board_reset();
    fake_stack_reset();
    fake_board_fit_tcpp02(false);
    regmap_init(BLADE_RESET_POWER);
    port_init();
    power_init();
    supervisor_init();
    run_ms(5);
    MT_ASSERT(ctl_read16(BLADE_REG_FAULT) & BLADE_FAULT_BUS);
    MT_ASSERT(!hw_tcpp_en_sense()); // the line is not left driven

    boot();
    fake_board_set_en(true); // EN high, and no converter answers
    ctl_configure(3000, BLADE_MAX_MV_ALL, true);
    run_ms(200);
    MT_ASSERT(ctl_read16(BLADE_REG_FAULT) & BLADE_FAULT_BUS);
    MT_ASSERT(!stack_port_armed());
}

static void stack_failures_are_faults(void) {
    boot();
    en_high();
    ctl_configure(3000, BLADE_MAX_MV_ALL, true);
    run_ms(200);
    supervisor_power_failed();
    run_ms(2);
    MT_ASSERT(ctl_read16(BLADE_REG_FAULT) & BLADE_FAULT_VBUS);
    MT_ASSERT(!stack_port_armed());

    boot();
    supervisor_stack_failed();
    en_high();
    ctl_configure(3000, BLADE_MAX_MV_ALL, true);
    run_ms(200);
    MT_ASSERT(ctl_read16(BLADE_REG_FAULT) & BLADE_FAULT_PD);
    MT_ASSERT(!stack_port_armed());
}

static void telemetry_reaches_the_controller(void) {
    boot();
    en_high();
    ctl_configure(5000, BLADE_MAX_MV_ALL, true);
    run_ms(200);
    fake_board_set_temps_dc(612, 388);
    port_attach();
    MT_ASSERT(port_request(((uint32_t)2 << 28) | (200u << 10) | 200u)); // 9 V, 2 A
    port_delivered();
    run_ms(20);
    MT_ASSERT_EQ(ctl_read8(BLADE_REG_STATUS),
                 BLADE_ST_EN | BLADE_ST_CONFIGURED | BLADE_ST_FAULT | BLADE_ST_ATTACHED | BLADE_ST_CONTRACT);
    MT_ASSERT_EQ(ctl_read8(BLADE_REG_PDO), 2);
    MT_ASSERT_EQ(ctl_read16(BLADE_REG_CONTRACT_MV), 9000);
    MT_ASSERT_EQ(ctl_read16(BLADE_REG_CONTRACT_MA), 2000);
    MT_ASSERT_EQ((int16_t)ctl_read16(BLADE_REG_TEMP_CONV), 612);
    MT_ASSERT_EQ((int16_t)ctl_read16(BLADE_REG_TEMP_PLUG), 388);
}

void run_supervisor_tests(void) {
    mt_run("supervisor: boots dark and quiet", boots_dark_and_quiet);
    mt_run("supervisor: EN alone does not arm the port", en_alone_does_not_arm);
    mt_run("supervisor: configuration alone does not arm the port", configuration_alone_does_not_arm);
    mt_run("supervisor: arms when everything is there", arms_when_everything_is_there);
    mt_run("supervisor: a ceiling of zero is off", a_ceiling_of_zero_is_off);
    mt_run("supervisor: port disable and EN low disarm", port_disable_and_en_low_disarm);
    mt_run("supervisor: new limits go to the sink", new_limits_go_to_the_sink);
    mt_run("supervisor: commands reach the stack", commands_reach_the_stack);
    mt_run("supervisor: an over-voltage trip takes the port down", over_voltage_trip_takes_the_port_down);
    mt_run("supervisor: a fault that persists comes back", a_fault_that_persists_comes_back);
    mt_run("supervisor: a hot reading has to hold", a_hot_reading_has_to_hold);
    mt_run("supervisor: a blind sensor is a fault", a_blind_sensor_is_a_fault);
    mt_run("supervisor: a missing part is a fault", a_missing_part_is_a_fault);
    mt_run("supervisor: stack failures are faults", stack_failures_are_faults);
    mt_run("supervisor: telemetry reaches the controller", telemetry_reaches_the_controller);
}
