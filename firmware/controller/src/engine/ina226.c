#include "ina226.h"

#include "hardware/i2c.h"

#include "pins.h"

#define REG_CONFIG      0x00
#define REG_SHUNT_V     0x01
#define REG_BUS_V       0x02
#define REG_POWER       0x03
#define REG_CURRENT     0x04
#define REG_CALIBRATION 0x05
#define REG_MASK_ENABLE 0x06
#define REG_ALERT_LIMIT 0x07
#define REG_MFG_ID      0xFE

#define MFG_ID_TI       0x5449

// CONFIG: AVG=4 (0b010), VBUSCT=588us (0b011), VSHCT=588us (0b011),
// MODE=shunt+bus continuous (0b111) -> ~4.7ms per averaged result set
#define CONFIG_VALUE ((1u << 14) | (0b010u << 9) | (0b011u << 6) | (0b011u << 3) | 0b111u)

#define CAL_VALUE       2048    // 0.00512 / (0.25mA * 0.010R)
#define CURRENT_LSB_UA  250
#define POWER_LSB_UW    6250
#define BUS_LSB_UV      1250

// Mask/Enable bits
#define ME_SOL          (1u << 15) // shunt over-voltage alert
#define ME_AFF          (1u << 4)  // alert function flag
#define ME_LEN          (1u << 0)  // latch enable

#define I2C_TIMEOUT_US  (2 * 1000)

static bool write_reg(uint8_t reg, uint16_t val) {
    uint8_t buf[3] = {reg, (uint8_t)(val >> 8), (uint8_t)(val & 0xFF)};
    return i2c_write_timeout_us(I2C_BUS, ADDR_INA226, buf, 3, false,
                                I2C_TIMEOUT_US) == 3;
}

static bool read_reg(uint8_t reg, uint16_t *val) {
    uint8_t buf[2];
    if (i2c_write_timeout_us(I2C_BUS, ADDR_INA226, &reg, 1, true,
                             I2C_TIMEOUT_US) != 1)
        return false;
    if (i2c_read_timeout_us(I2C_BUS, ADDR_INA226, buf, 2, false,
                            I2C_TIMEOUT_US) != 2)
        return false;
    *val = ((uint16_t)buf[0] << 8) | buf[1];
    return true;
}

bool ina226_probe(void) {
    uint16_t id;
    return read_reg(REG_MFG_ID, &id) && id == MFG_ID_TI;
}

bool ina226_configure(void) {
    if (!write_reg(REG_CONFIG, CONFIG_VALUE)) return false;
    return write_reg(REG_CALIBRATION, CAL_VALUE);
}

bool ina226_read(ina226_reading_t *r) {
    uint16_t bus, cur, pwr;
    if (!read_reg(REG_BUS_V, &bus)) return false;
    if (!read_reg(REG_CURRENT, &cur)) return false;
    if (!read_reg(REG_POWER, &pwr)) return false;
    r->bus_mv = (uint16_t)(((uint32_t)bus * BUS_LSB_UV) / 1000);
    r->current_ma = ((int32_t)(int16_t)cur * CURRENT_LSB_UA) / 1000;
    r->power_mw = ((uint32_t)pwr * POWER_LSB_UW) / 1000;
    return true;
}

bool ina226_set_alert_ma(uint32_t ma) {
    // SOL compares the shunt-voltage register: 10mOhm * I, LSB 2.5uV
    // -> limit_raw = I_ma * 4 (5000mA -> 50mV -> 20000)
    uint32_t raw = ma * 4;
    if (raw > 0x7FFF) raw = 0x7FFF;
    if (!write_reg(REG_ALERT_LIMIT, (uint16_t)raw)) return false;
    return write_reg(REG_MASK_ENABLE, ME_SOL | ME_LEN);
}

bool ina226_alert_tripped(bool *tripped) {
    uint16_t me;
    if (!read_reg(REG_MASK_ENABLE, &me)) return false; // read clears the latch
    *tripped = (me & ME_AFF) != 0;
    return true;
}
