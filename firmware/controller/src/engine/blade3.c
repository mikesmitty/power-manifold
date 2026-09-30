#include "blade3.h"

#include "hardware/i2c.h"

#include "pins.h"

#define I2C_TIMEOUT_US (2 * 1000)

static bool read_regs(uint8_t reg, uint8_t *buf, size_t n) {
    if (i2c_write_timeout_us(I2C_BUS, BLADE_I2C_ADDR, &reg, 1, true, I2C_TIMEOUT_US) != 1)
        return false;
    return i2c_read_timeout_us(I2C_BUS, BLADE_I2C_ADDR, buf, n, false, I2C_TIMEOUT_US) == (int)n;
}

static bool write_regs(const uint8_t *buf, size_t n) { // buf[0] is the register
    return i2c_write_timeout_us(I2C_BUS, BLADE_I2C_ADDR, buf, n, false, I2C_TIMEOUT_US) == (int)n;
}

static uint16_t u16(const uint8_t *p) {
    return (uint16_t)(p[0] | (p[1] << 8));
}

bool blade3_probe(void) {
    uint8_t id;
    return read_regs(BLADE_REG_WHO_AM_I, &id, 1) && id == BLADE_WHO_AM_I;
}

bool blade3_read_identity(blade3_identity_t *id) {
    uint8_t b[BLADE_REG_BOOT + 1 - BLADE_REG_PROTO];
    if (!read_regs(BLADE_REG_PROTO, b, sizeof b)) return false;
    id->proto       = b[BLADE_REG_PROTO - BLADE_REG_PROTO];
    id->major       = b[BLADE_REG_FW_MAJOR - BLADE_REG_PROTO];
    id->minor       = b[BLADE_REG_FW_MINOR - BLADE_REG_PROTO];
    id->patch       = b[BLADE_REG_FW_PATCH - BLADE_REG_PROTO];
    id->reset_cause = b[BLADE_REG_RESET_CAUSE - BLADE_REG_PROTO];
    id->caps        = b[BLADE_REG_CAPS - BLADE_REG_PROTO];
    id->boot        = b[BLADE_REG_BOOT - BLADE_REG_PROTO];
    return true;
}

bool blade3_read_status(blade3_status_t *s) {
    uint8_t b[BLADE_REG_TEMP_MCU + 2 - BLADE_REG_STATUS];
    if (!read_regs(BLADE_REG_STATUS, b, sizeof b)) return false;
    s->status       = b[BLADE_REG_STATUS - BLADE_REG_STATUS];
    s->pdo          = b[BLADE_REG_PDO - BLADE_REG_STATUS];
    s->faults       = u16(b + BLADE_REG_FAULT - BLADE_REG_STATUS);
    s->contract_mv  = u16(b + BLADE_REG_CONTRACT_MV - BLADE_REG_STATUS);
    s->contract_ma  = u16(b + BLADE_REG_CONTRACT_MA - BLADE_REG_STATUS);
    s->vbus_mv      = u16(b + BLADE_REG_VBUS_MV - BLADE_REG_STATUS);
    s->iout_ma      = u16(b + BLADE_REG_IOUT_MA - BLADE_REG_STATUS);
    s->vout_mv      = u16(b + BLADE_REG_VOUT_MV - BLADE_REG_STATUS);
    s->temp_conv_dc = (int16_t)u16(b + BLADE_REG_TEMP_CONV - BLADE_REG_STATUS);
    s->temp_plug_dc = (int16_t)u16(b + BLADE_REG_TEMP_PLUG - BLADE_REG_STATUS);
    s->temp_mcu_dc  = (int16_t)u16(b + BLADE_REG_TEMP_MCU - BLADE_REG_STATUS);
    return true;
}

bool blade3_read_config(blade3_config_t *c) {
    uint8_t ctl, lim[5];
    if (!read_regs(BLADE_REG_CONTROL, &ctl, 1)) return false;
    if (!read_regs(BLADE_REG_MAX_MA, lim, sizeof lim)) return false;
    c->port_en = (ctl & BLADE_CTL_PORT_EN) != 0;
    c->max_ma = u16(lim);
    c->max_mv = u16(lim + 2);
    c->watch_s = lim[4];
    return true;
}

bool blade3_write_config(const blade3_config_t *c) {
    uint8_t lim[6] = {BLADE_REG_MAX_MA, (uint8_t)c->max_ma, (uint8_t)(c->max_ma >> 8),
                      (uint8_t)c->max_mv, (uint8_t)(c->max_mv >> 8), c->watch_s};
    if (!write_regs(lim, sizeof lim)) return false;
    uint8_t ctl[2] = {BLADE_REG_CONTROL, c->port_en ? BLADE_CTL_PORT_EN : 0};
    return write_regs(ctl, sizeof ctl);
}

bool blade3_command(uint8_t cmd) {
    uint8_t b[2] = {BLADE_REG_COMMAND, cmd};
    return write_regs(b, sizeof b);
}
