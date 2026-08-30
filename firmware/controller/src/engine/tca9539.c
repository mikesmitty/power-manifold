#include "tca9539.h"

#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "pico/time.h"

#include "pins.h"

#define REG_INPUT0   0x00
#define REG_OUTPUT0  0x02
#define REG_CONFIG0  0x06

#define I2C_TIMEOUT_US (2 * 1000)

static uint16_t output_cache;

static bool write_reg16(uint8_t reg, uint16_t val) {
    uint8_t buf[3] = {reg, (uint8_t)(val & 0xFF), (uint8_t)(val >> 8)};
    return i2c_write_timeout_us(I2C_BUS, ADDR_TCA9539, buf, 3, false,
                                I2C_TIMEOUT_US) == 3;
}

static bool read_reg16(uint8_t reg, uint16_t *val) {
    uint8_t lo_hi[2];
    if (i2c_write_timeout_us(I2C_BUS, ADDR_TCA9539, &reg, 1, true,
                             I2C_TIMEOUT_US) != 1)
        return false;
    if (i2c_read_timeout_us(I2C_BUS, ADDR_TCA9539, lo_hi, 2, false,
                            I2C_TIMEOUT_US) != 2)
        return false;
    *val = (uint16_t)lo_hi[0] | ((uint16_t)lo_hi[1] << 8);
    return true;
}

bool tca9539_init(void) {
    gpio_init(PIN_EXP_RST_N);
    gpio_put(PIN_EXP_RST_N, 1);
    gpio_set_dir(PIN_EXP_RST_N, GPIO_OUT);

    gpio_put(PIN_EXP_RST_N, 0);
    sleep_us(1);
    gpio_put(PIN_EXP_RST_N, 1);
    sleep_us(1);

    // ORDER IS CRITICAL (spec §6.4): the Output Port registers power up as
    // 0xFF while every pin is an input. Writing outputs to 0x0000 BEFORE the
    // Configuration registers means the EN pins go active as driven-low, not
    // driven-high — reversing this enables every blade at once.
    output_cache = 0x0000;
    if (!write_reg16(REG_OUTPUT0, output_cache)) return false;
    uint16_t config = (uint16_t)TCA9539_CONFIG_P0 | ((uint16_t)TCA9539_CONFIG_P1 << 8);
    if (!write_reg16(REG_CONFIG0, config)) return false;
    return true;
}

static bool set_output_bit(uint8_t bit, bool on) {
    uint16_t next = on ? (output_cache | (1u << bit))
                       : (output_cache & ~(1u << bit));
    if (next == output_cache) return true;
    if (!write_reg16(REG_OUTPUT0, next)) return false;
    output_cache = next;
    return true;
}

bool tca9539_set_en(uint8_t port, bool on) {
    return set_output_bit(TCA9539_EN_BIT(port), on);
}

bool tca9539_set_fan(bool on) {
    return set_output_bit(TCA9539_FAN_BIT, on);
}

bool tca9539_all_en_off(void) {
    uint16_t next = output_cache & ~0x003Fu; // EN1..EN6 = P00..P05
    if (!write_reg16(REG_OUTPUT0, next)) return false;
    output_cache = next;
    return true;
}

bool tca9539_read_inputs(uint16_t *inputs) {
    return read_reg16(REG_INPUT0, inputs);
}
