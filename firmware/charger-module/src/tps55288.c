#include "tps55288.h"

#include "board.h"
#include "bus.h"

// REF: 45 mV at code 0, 1.129 mV per step; VOUT = VREF / 0.0564
#define REF_ZERO_UV     45000
#define REF_STEP_UV     1129
#define REF_CODE_MAX    0x3FF
#define FB_RATIO_X1E4   564

#define ILIM_EN         0x80
#define ILIM_STEP_UV    500 // across the shunt

#define VOUT_FS_INTFB_0_0564 0x03 // FB bit clear: internal feedback

static uint32_t ilim_step_ma(void) {
    return (ILIM_STEP_UV * 1000u) / CONV_SHUNT_UOHM;
}

uint16_t tps55288_ref_code(uint32_t mv) {
    if (mv < TPS55288_VOUT_MIN_MV) mv = TPS55288_VOUT_MIN_MV;
    if (mv > TPS55288_VOUT_MAX_MV) mv = TPS55288_VOUT_MAX_MV;
    uint32_t ref_uv = (mv * FB_RATIO_X1E4) / 10;
    uint32_t code = (ref_uv - REF_ZERO_UV + REF_STEP_UV / 2) / REF_STEP_UV;
    return (uint16_t)(code > REF_CODE_MAX ? REF_CODE_MAX : code);
}

uint32_t tps55288_ref_mv(uint16_t code) {
    uint32_t ref_uv = REF_ZERO_UV + (uint32_t)(code & REF_CODE_MAX) * REF_STEP_UV;
    return (ref_uv * 10 + FB_RATIO_X1E4 / 2) / FB_RATIO_X1E4;
}

uint8_t tps55288_ilim_code(uint32_t ma) {
    uint32_t code = ma / ilim_step_ma();
    if (code > 0x7F) code = 0x7F;
    return (uint8_t)(ILIM_EN | code);
}

uint32_t tps55288_ilim_ma(uint8_t code) {
    return (uint32_t)(code & 0x7F) * ilim_step_ma();
}

uint8_t tps55288_mode_byte(const tps55288_mode_t *m) {
    uint8_t b = TPS55288_MODE_HICCUP;
    if (m->output_on) b |= TPS55288_MODE_OE;
    if (m->discharge) b |= TPS55288_MODE_DISCHG;
    // As strapped, bits 3-0 stay 0 and the MODE pin rules. Forced PWM takes
    // the register's word for all three settings, so it repeats the pin's
    // other two: external VCC, address 0x74.
    if (m->forced_pwm) b |= TPS55288_MODE_BY_REG | TPS55288_MODE_FPWM | TPS55288_MODE_VCC_EXT;
    return b;
}

static bool reg_write(uint8_t reg, uint8_t val) {
    uint8_t buf[2] = {reg, val};
    return bus_write(ADDR_TPS55288, buf, sizeof buf);
}

bool tps55288_probe(void) {
    return bus_probe(ADDR_TPS55288);
}

bool tps55288_init(tps55288_slew_t slew) {
    static const tps55288_mode_t off = {0};
    if (!tps55288_set_mode(&off)) return false;
    if (!reg_write(TPS55288_REG_VOUT_FS, VOUT_FS_INTFB_0_0564)) return false;
    if (!reg_write(TPS55288_REG_VOUT_SR, (uint8_t)slew)) return false; // OCP_DELAY 0: limit at once
    // OCP indication stays masked: the mask bit must be 0 whenever OE or the
    // limit enable go 0 -> 1, and in a PPS contract the limit is a normal
    // operating point
    if (!reg_write(TPS55288_REG_CDC, TPS55288_CDC_SC_MASK | TPS55288_CDC_OVP_MASK)) return false;
    return tps55288_set_mv(5000);
}

bool tps55288_set_mv(uint32_t mv) {
    uint16_t code = tps55288_ref_code(mv);
    uint8_t buf[3] = {TPS55288_REG_REF_LSB, (uint8_t)(code & 0xFF), (uint8_t)(code >> 8)};
    return bus_write(ADDR_TPS55288, buf, sizeof buf);
}

bool tps55288_set_limit_ma(uint32_t ma) {
    return reg_write(TPS55288_REG_IOUT_LIMIT, tps55288_ilim_code(ma));
}

bool tps55288_set_mode(const tps55288_mode_t *m) {
    return reg_write(TPS55288_REG_MODE, tps55288_mode_byte(m));
}

bool tps55288_read_status(uint8_t *status) {
    return bus_read_reg(ADDR_TPS55288, TPS55288_REG_STATUS, status, 1);
}
