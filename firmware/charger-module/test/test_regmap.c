#include "microtest.h"
#include "regmap.h"

// The register file driven the way the I2C1 interrupt drives it.

static void bus_write(uint8_t reg, const uint8_t *data, unsigned n) {
    regmap_addressed(false);
    regmap_rx(reg);
    for (unsigned i = 0; i < n; i++) regmap_rx(data[i]);
    regmap_stop();
}

static void bus_read(uint8_t reg, uint8_t *data, unsigned n) {
    regmap_addressed(false);
    regmap_rx(reg);
    regmap_addressed(true); // repeated start
    for (unsigned i = 0; i < n; i++) data[i] = regmap_tx();
    regmap_stop();
}

static uint8_t read8(uint8_t reg) {
    uint8_t v;
    bus_read(reg, &v, 1);
    return v;
}

static uint16_t read16(uint8_t reg) {
    uint8_t v[2];
    bus_read(reg, v, 2);
    return (uint16_t)(v[0] | (v[1] << 8));
}

static void write8(uint8_t reg, uint8_t v) { bus_write(reg, &v, 1); }

static void write16(uint8_t reg, uint16_t v) {
    uint8_t b[2] = {(uint8_t)(v & 0xFF), (uint8_t)(v >> 8)};
    bus_write(reg, b, 2);
}

static void identity(void) {
    regmap_init(BLADE_RESET_POWER);
    uint8_t id[8];
    bus_read(BLADE_REG_WHO_AM_I, id, sizeof id);
    MT_ASSERT_EQ(id[0], BLADE_WHO_AM_I);
    MT_ASSERT_EQ(id[1], BLADE_PROTO_VERSION);
    MT_ASSERT_EQ(id[2], FW_MAJOR);
    MT_ASSERT_EQ(id[3], FW_MINOR);
    MT_ASSERT_EQ(id[4], FW_PATCH);
    MT_ASSERT_EQ(id[5], BLADE_RESET_POWER);
    MT_ASSERT_EQ(id[6], BLADE_CAP_PPS);
    MT_ASSERT_EQ(id[7], 0); // factory option bytes until told otherwise
    regmap_set_boot(BLADE_BOOT_VIA_LOADER);
    MT_ASSERT_EQ(read8(BLADE_REG_BOOT), BLADE_BOOT_VIA_LOADER);
    write8(BLADE_REG_BOOT, 0); // read-only
    MT_ASSERT_EQ(read8(BLADE_REG_BOOT), BLADE_BOOT_VIA_LOADER);
}

// The watch register rides with the limits, and every time the controller
// addresses the blade counts as a sign of life.
static void watch_register_and_transactions(void) {
    regmap_init(0);
    regmap_config_t c;
    MT_ASSERT_EQ(regmap_transactions(), 0);
    uint8_t limits[5] = {0xB8, 0x0B, 0x20, 0x4E, 30}; // 3000 mA, 20000 mV, watch 30 s
    bus_write(BLADE_REG_MAX_MA, limits, sizeof limits);
    MT_ASSERT(regmap_config(&c));
    MT_ASSERT_EQ(c.max_ma, 3000);
    MT_ASSERT_EQ(c.max_mv, 20000);
    MT_ASSERT_EQ(c.watch_s, 30);
    MT_ASSERT_EQ(read8(BLADE_REG_WATCH_S), 30);
    MT_ASSERT_EQ(regmap_transactions(), 3); // the write, and the read's two address phases
    write8(BLADE_REG_WATCH_S, 0);
    MT_ASSERT(regmap_config(&c));
    MT_ASSERT_EQ(c.watch_s, 0);
    MT_ASSERT_EQ(c.max_ma, 3000); // the limits stay
}

static void starts_dark_and_flagged(void) {
    regmap_init(BLADE_RESET_WATCHDOG);
    regmap_config_t c;
    MT_ASSERT(!regmap_config(&c));
    MT_ASSERT(!c.port_en);
    MT_ASSERT_EQ(c.max_ma, 0);
    MT_ASSERT_EQ(regmap_faults(), BLADE_FAULT_RESET); // a restart is never silent
    MT_ASSERT_EQ(read8(BLADE_REG_STATUS), BLADE_ST_FAULT);
    MT_ASSERT_EQ(read8(BLADE_REG_RESET_CAUSE), BLADE_RESET_WATCHDOG);
}

static void telemetry(void) {
    regmap_init(0);
    regmap_live_t live = {
        .status = BLADE_ST_ATTACHED | BLADE_ST_CONTRACT | BLADE_ST_VBUS_ON | BLADE_ST_EN,
        .pdo = 5, .contract_mv = 20000, .contract_ma = 3250,
        .hr_sent = 2, .hr_received = 7,
        .vbus_mv = 19876, .iout_ma = 3011, .vout_mv = 19950,
        .temp_conv_dc = 612, .temp_plug_dc = -35, .temp_mcu_dc = 480,
    };
    regmap_publish(&live);
    MT_ASSERT_EQ(read8(BLADE_REG_PDO), 5);
    MT_ASSERT_EQ(read16(BLADE_REG_CONTRACT_MV), 20000);
    MT_ASSERT_EQ(read16(BLADE_REG_CONTRACT_MA), 3250);
    MT_ASSERT_EQ(read8(BLADE_REG_HR_SENT), 2);
    MT_ASSERT_EQ(read8(BLADE_REG_HR_RECEIVED), 7);
    MT_ASSERT_EQ(read16(BLADE_REG_VBUS_MV), 19876);
    MT_ASSERT_EQ(read16(BLADE_REG_IOUT_MA), 3011);
    MT_ASSERT_EQ(read16(BLADE_REG_VOUT_MV), 19950);
    MT_ASSERT_EQ((int16_t)read16(BLADE_REG_TEMP_CONV), 612);
    MT_ASSERT_EQ((int16_t)read16(BLADE_REG_TEMP_PLUG), -35);
    // the reset fault is still latched: the firmware's bits plus FAULT
    MT_ASSERT_EQ(read8(BLADE_REG_STATUS), live.status | BLADE_ST_FAULT);
}

static void a_read_sees_one_snapshot(void) {
    regmap_init(0);
    regmap_live_t live = {.vbus_mv = 0x12FF};
    regmap_publish(&live);

    regmap_addressed(false);
    regmap_rx(BLADE_REG_VBUS_MV);
    regmap_addressed(true);
    uint8_t lo = regmap_tx();
    live.vbus_mv = 0x1300; // the main loop publishes between the two bytes
    regmap_publish(&live);
    uint8_t hi = regmap_tx();
    regmap_stop();

    MT_ASSERT_EQ(lo | (hi << 8), 0x12FF);
    MT_ASSERT_EQ(read16(BLADE_REG_VBUS_MV), 0x1300);
}

static void configuration_lands_at_the_end(void) {
    regmap_init(0);
    regmap_config_t c;

    regmap_addressed(false);
    regmap_rx(BLADE_REG_MAX_MA);
    regmap_rx(0xB8); // 3000 = 0x0BB8
    MT_ASSERT(!regmap_config(&c));
    MT_ASSERT_EQ(c.max_ma, 0); // half a value is no value
    regmap_rx(0x0B);
    regmap_rx(0x20); // 20000 = 0x4E20, the pointer ran on into MAX_MV
    regmap_rx(0x4E);
    MT_ASSERT(!regmap_config(&c));
    regmap_stop();

    MT_ASSERT(regmap_config(&c));
    MT_ASSERT_EQ(c.max_ma, 3000);
    MT_ASSERT_EQ(c.max_mv, 20000);
    MT_ASSERT(!c.port_en);
    MT_ASSERT(!regmap_config(&c)); // reported once
    MT_ASSERT_EQ(c.max_ma, 3000);  // ...but always readable

    MT_ASSERT_EQ(read16(BLADE_REG_MAX_MA), 3000);
    MT_ASSERT(read8(BLADE_REG_STATUS) & BLADE_ST_CONFIGURED);

    write8(BLADE_REG_CONTROL, BLADE_CTL_PORT_EN);
    MT_ASSERT(regmap_config(&c));
    MT_ASSERT(c.port_en);
    MT_ASSERT_EQ(c.max_ma, 3000);
}

static void configuration_is_clamped(void) {
    regmap_init(0);
    regmap_config_t c;
    write16(BLADE_REG_MAX_MA, 6000);
    write8(BLADE_REG_CONTROL, 0xFF);
    MT_ASSERT(regmap_config(&c));
    MT_ASSERT_EQ(c.max_ma, BLADE_MAX_MA_LIMIT);
    MT_ASSERT_EQ(read16(BLADE_REG_MAX_MA), BLADE_MAX_MA_LIMIT); // what the controller reads back
    MT_ASSERT_EQ(read8(BLADE_REG_CONTROL), BLADE_CTL_PORT_EN);
}

static void read_only_registers_stay(void) {
    regmap_init(0);
    regmap_config_t c;
    write8(BLADE_REG_WHO_AM_I, 0x00);
    write16(BLADE_REG_FAULT, 0x0000);
    write16(BLADE_REG_VBUS_MV, 1234);
    MT_ASSERT_EQ(read8(BLADE_REG_WHO_AM_I), BLADE_WHO_AM_I);
    MT_ASSERT_EQ(read16(BLADE_REG_FAULT), BLADE_FAULT_RESET);
    MT_ASSERT_EQ(read16(BLADE_REG_VBUS_MV), 0);
    MT_ASSERT(!regmap_config(&c));
    MT_ASSERT(!(read8(BLADE_REG_STATUS) & BLADE_ST_CONFIGURED));
}

static void past_the_end(void) {
    regmap_init(0);
    uint8_t v[4];
    bus_read(BLADE_REG_END - 1, v, sizeof v);
    MT_ASSERT_EQ(v[1], 0);
    MT_ASSERT_EQ(v[3], 0);
    bus_read(0xFE, v, sizeof v); // the pointer wraps through 0xFF into the identity block
    MT_ASSERT_EQ(v[0], 0);
    MT_ASSERT_EQ(v[2], BLADE_WHO_AM_I);
    write8(0xF0, 0x55);
    MT_ASSERT_EQ(read8(0xF0), 0);
}

static void faults_latch_until_cleared(void) {
    regmap_init(0);
    write8(BLADE_REG_COMMAND, BLADE_CMD_CLEAR_FAULTS);
    MT_ASSERT_EQ(regmap_faults(), 0);
    MT_ASSERT_EQ(regmap_command(), BLADE_CMD_CLEAR_FAULTS); // the firmware hears of it too

    regmap_raise(BLADE_FAULT_OVP);
    regmap_raise(BLADE_FAULT_OT_PLUG);
    MT_ASSERT_EQ(read16(BLADE_REG_FAULT), BLADE_FAULT_OVP | BLADE_FAULT_OT_PLUG);
    MT_ASSERT(read8(BLADE_REG_STATUS) & BLADE_ST_FAULT);

    regmap_live_t live = {0};
    regmap_publish(&live); // telemetry does not touch the latch
    MT_ASSERT_EQ(regmap_faults(), BLADE_FAULT_OVP | BLADE_FAULT_OT_PLUG);

    write8(BLADE_REG_COMMAND, BLADE_CMD_CLEAR_FAULTS);
    MT_ASSERT_EQ(regmap_faults(), 0);
    MT_ASSERT(!(read8(BLADE_REG_STATUS) & BLADE_ST_FAULT));
}

static void commands_queue_in_order(void) {
    regmap_init(0);
    MT_ASSERT_EQ(regmap_command(), 0);
    write8(BLADE_REG_COMMAND, BLADE_CMD_SRC_CAP);
    write8(BLADE_REG_COMMAND, BLADE_CMD_HARD_RESET);
    MT_ASSERT_EQ(read8(BLADE_REG_COMMAND), 0); // write-only
    MT_ASSERT_EQ(regmap_command(), BLADE_CMD_SRC_CAP);
    MT_ASSERT_EQ(regmap_command(), BLADE_CMD_HARD_RESET);
    MT_ASSERT_EQ(regmap_command(), 0);

    for (int i = 0; i < 10; i++) write8(BLADE_REG_COMMAND, BLADE_CMD_SRC_CAP);
    int n = 0;
    while (regmap_command()) n++;
    MT_ASSERT_EQ(n, 4); // the queue's depth; the rest were dropped

    regmap_config_t c;
    MT_ASSERT(!regmap_config(&c)); // a command is not configuration
}

// The two commands that reset the MCU count only with their complement
// behind them in the same transfer.
static void reset_commands_need_their_complement(void) {
    regmap_init(0);
    write8(BLADE_REG_COMMAND, BLADE_CMD_RESET); // alone
    write8(BLADE_REG_COMMAND, BLADE_CMD_BOOT_OPT);
    MT_ASSERT_EQ(regmap_command(), 0);

    uint8_t wrong[2] = {BLADE_CMD_RESET, BLADE_CMD_RESET};
    bus_write(BLADE_REG_COMMAND, wrong, 2);
    MT_ASSERT_EQ(regmap_command(), 0);

    write8(BLADE_REG_COMMAND, BLADE_CMD_RESET); // the complement in the next transfer is not behind it
    write8(BLADE_REG_COMMAND, (uint8_t)~BLADE_CMD_RESET);
    MT_ASSERT_EQ(regmap_command(), (uint8_t)~BLADE_CMD_RESET); // a command nobody knows, and no reset
    MT_ASSERT_EQ(regmap_command(), 0);

    // a limits write whose pointer lost a bit: 1285 mA's high byte lands on
    // the command register as the boot-option command
    uint8_t limits[5] = {0x05, 0x05, 0x20, 0x4E, 120};
    bus_write(BLADE_REG_CONTROL, limits, sizeof limits);
    MT_ASSERT_EQ(regmap_command(), 0);

    uint8_t reset[2] = {BLADE_CMD_RESET, (uint8_t)~BLADE_CMD_RESET};
    bus_write(BLADE_REG_COMMAND, reset, 2);
    MT_ASSERT_EQ(regmap_command(), BLADE_CMD_RESET);
    uint8_t opt[2] = {BLADE_CMD_BOOT_OPT, (uint8_t)~BLADE_CMD_BOOT_OPT};
    bus_write(BLADE_REG_COMMAND, opt, 2);
    MT_ASSERT_EQ(regmap_command(), BLADE_CMD_BOOT_OPT);
    MT_ASSERT_EQ(regmap_command(), 0);

    // the others are as they were, and the pointer moves on past a guarded pair
    write16(BLADE_REG_MAX_MA, 3000);
    uint8_t then[3] = {BLADE_CMD_RESET, (uint8_t)~BLADE_CMD_RESET, 0xB9};
    bus_write(BLADE_REG_COMMAND, then, 3);
    MT_ASSERT_EQ(regmap_command(), BLADE_CMD_RESET);
    MT_ASSERT_EQ(read16(BLADE_REG_MAX_MA), 3001); // 0x0BB8 with its low byte rewritten
    write8(BLADE_REG_COMMAND, BLADE_CMD_SRC_CAP);
    MT_ASSERT_EQ(regmap_command(), BLADE_CMD_SRC_CAP);
}

void run_regmap_tests(void) {
    mt_run("regmap: identity block", identity);
    mt_run("regmap: watch register and transaction count", watch_register_and_transactions);
    mt_run("regmap: starts dark with the reset fault latched", starts_dark_and_flagged);
    mt_run("regmap: telemetry", telemetry);
    mt_run("regmap: a read sees one snapshot", a_read_sees_one_snapshot);
    mt_run("regmap: configuration lands at the end of the transfer", configuration_lands_at_the_end);
    mt_run("regmap: configuration is clamped", configuration_is_clamped);
    mt_run("regmap: read-only registers ignore writes", read_only_registers_stay);
    mt_run("regmap: reads and writes past the end", past_the_end);
    mt_run("regmap: faults latch until cleared", faults_latch_until_cleared);
    mt_run("regmap: commands queue in order", commands_queue_in_order);
    mt_run("regmap: reset commands need their complement", reset_commands_need_their_complement);
}
