#include "mpq4242.h"

#include "hardware/i2c.h"

#include "manifold.h"
#include "pins.h"

#define REG_PDO_SET1    0x00
#define REG_PDO_SET2    0x01
#define REG_PDO_I1      0x03
#define REG_PDO_V2_L    0x04
#define REG_PD_CTL2     0x17
#define REG_PWR_CTL1    0x18
#define REG_CTL_SYS1    0x1E
#define REG_CTL_SYS2    0x1F
#define REG_CTL_SYS17   0x2E
#define REG_STATUS1     0x30
#define REG_STATUS2     0x31
#define REG_STATUS3     0x32
#define REG_DEV_ID      0x38
#define REG_CLK_ON      0x39

#define I2C_TIMEOUT_US  (2 * 1000)

static bool reg_write(uint8_t reg, uint8_t val) {
    uint8_t buf[2] = {reg, val};
    return i2c_write_timeout_us(I2C_BUS, ADDR_MPQ4242, buf, 2, false,
                                I2C_TIMEOUT_US) == 2;
}

static bool reg_read(uint8_t reg, uint8_t *val) {
    if (i2c_write_timeout_us(I2C_BUS, ADDR_MPQ4242, &reg, 1, true,
                             I2C_TIMEOUT_US) != 1)
        return false;
    return i2c_read_timeout_us(I2C_BUS, ADDR_MPQ4242, val, 1, false,
                               I2C_TIMEOUT_US) == 1;
}

static bool reg_set_bit(uint8_t reg, uint8_t bit, bool on) {
    uint8_t v;
    if (!reg_read(reg, &v)) return false;
    v = on ? (v | (1u << bit)) : (v & ~(1u << bit));
    return reg_write(reg, v);
}

bool mpq4242_probe(void) {
    uint8_t id;
    return reg_read(REG_DEV_ID, &id) && id == MPQ4242_CHIP_ID;
}

bool mpq4242_unlock(void) {
    return reg_write(REG_CLK_ON, 1);
}

static bool pdo_is_pps(uint8_t pdo) {
    if (pdo < 2) return false;
    uint8_t types;
    if (!reg_read(REG_PDO_SET2, &types)) return false;
    return (types >> (pdo - 2)) & 1u;
}

static bool set_pdo_current(uint8_t pdo, uint32_t ma, bool pps) {
    uint32_t step = pps ? 50 : 20; // PPS 50mA steps, fixed 20mA steps
    uint32_t raw = (ma + step / 2) / step;
    if (raw > 0xFF) raw = 0xFF;
    return reg_write(REG_PDO_I1 + 3 * (pdo - 1), (uint8_t)raw);
}

bool mpq4242_set_max_current_ma(uint32_t ma) {
    for (uint8_t pdo = 1; pdo <= 7; pdo++) {
        if (!set_pdo_current(pdo, ma, pdo >= 2 && pdo_is_pps(pdo))) return false;
    }
    return true;
}

bool mpq4242_set_pdo_enabled(uint8_t pdo, bool enabled) {
    if (pdo < 2 || pdo > 7) return false; // PDO1 (5V) is always advertised
    return reg_set_bit(REG_PDO_SET1, pdo - 2, enabled);
}

bool mpq4242_set_pdo_fixed(uint8_t pdo, uint16_t mv, uint32_t ma, bool enabled) {
    if (pdo < 2 || pdo > 7) return false;
    if (!reg_set_bit(REG_PDO_SET2, pdo - 2, false)) return false; // fixed type
    uint8_t base = REG_PDO_V2_L + 3 * (pdo - 2);
    if (!reg_write(base, (uint8_t)(mv / 100))) return false; // 0.1V LSB
    if (!reg_write(base + 1, 0)) return false;
    if (!set_pdo_current(pdo, ma, false)) return false;
    return mpq4242_set_pdo_enabled(pdo, enabled);
}

bool mpq4242_send_src_cap(void) {
    return reg_set_bit(REG_CTL_SYS1, 7, true);
}

bool mpq4242_send_hard_reset(void) {
    return reg_set_bit(REG_PD_CTL2, 7, true);
}

bool mpq4242_configure(uint32_t max_ma) {
    if (!mpq4242_unlock()) return false;

    // CTL_SYS2: GPIO1 fn bits[7:5], GPIO2 fn bits[4:2]
    uint8_t ctl_sys2;
    if (!reg_read(REG_CTL_SYS2, &ctl_sys2)) return false;
    ctl_sys2 = (uint8_t)((MPQ4242_GPIO1_FN_FAULT << 5) |
                         (MPQ4242_GPIO2_FN_DISABLED << 2) | (ctl_sys2 & 0x03));
    if (!reg_write(REG_CTL_SYS2, ctl_sys2)) return false;

    // CTL_SYS17: peak input current limit, bits[7:6]
    uint8_t ctl_sys17;
    if (!reg_read(REG_CTL_SYS17, &ctl_sys17)) return false;
    ctl_sys17 = (uint8_t)((ctl_sys17 & 0x3F) | (MPQ4242_PEAK_CL_8A << 6));
    if (!reg_write(REG_CTL_SYS17, ctl_sys17)) return false;

    return mpq4242_set_max_current_ma(max_ma);
}

bool mpq4242_read_status(mpq4242_status_t *s) {
    uint8_t s1, s2, s3;
    if (!reg_read(REG_STATUS1, &s1)) return false;
    if (!reg_read(REG_STATUS2, &s2)) return false;
    if (!reg_read(REG_STATUS3, &s3)) return false;

    s->attached = (s1 >> 7) & 1u;
    s->selected_pdo = (s2 >> 1) & 0x07u;
    s->contract_mw = (uint32_t)s3 * 500;

    uint8_t f = 0;
    if ((s1 >> 6) & 1u) f |= MPQ_FAULT_NTC2;
    if ((s1 >> 4) & 1u) f |= MPQ_FAULT_SHORT_VBATT;
    if ((s1 >> 3) & 1u) f |= MPQ_FAULT_GENERAL;
    if ((s1 >> 1) & 1u) f |= MPQ_FAULT_CC;
    if (s1 & 1u)        f |= MPQ_FAULT_NTC1;
    if ((s2 >> 7) & 1u) f |= MPQ_FAULT_VBATT_LOW;
    if ((s2 >> 6) & 1u) f |= MPQ_FAULT_VBATT_LOW;
    if ((s2 >> 5) & 1u) f |= MPQ_FAULT_OTW1;
    if ((s2 >> 4) & 1u) f |= MPQ_FAULT_OTW2;
    s->fault_bits = f;
    return true;
}
