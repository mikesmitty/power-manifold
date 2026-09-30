#pragma once

#include <stdbool.h>
#include <stdint.h>

// TI TPS55288 buck-boost converter on the private bus (SLVSF01B). Internal
// feedback at the 0.0564 ratio: 0.8 V to 21.3 V in 20 mV steps, which is the
// PPS step. Output current limit across R28 (10 mOhm): 50 mA steps to
// 6.35 A, the PPS current step.
//
// The slot's EN line drives EN/UVLO directly and the firmware cannot override
// it. EN low is shutdown: every function is off and the registers return to
// their defaults (output off, 5 V, 5 A), so the part is set up again after
// each rising edge of EN.
//
// The MODE pin resistor (R22 75k) selects VCC from the slot's 5 V rail,
// address 0x74 and PFM at light load. MODE register bit 0 hands all three to
// the register instead, where the VCC bit resets to the internal LDO, which
// would back-drive the slot rail: whenever bit 0 is written as 1 here, the
// VCC bit goes with it in the same write.
//
// In PFM the converter blocks reverse inductor current, so it cannot pull
// its own output down: a lightly loaded output only falls as fast as the
// load discharges it. In forced PWM the current reverses and the output
// follows the reference down. The register is how the firmware gets forced
// PWM for a downward voltage change on a board strapped for PFM.

#define TPS55288_REG_REF_LSB    0x00
#define TPS55288_REG_REF_MSB    0x01 // writing this one loads both into the DAC
#define TPS55288_REG_IOUT_LIMIT 0x02
#define TPS55288_REG_VOUT_SR    0x03
#define TPS55288_REG_VOUT_FS    0x04
#define TPS55288_REG_CDC        0x05
#define TPS55288_REG_MODE       0x06
#define TPS55288_REG_STATUS     0x07

#define TPS55288_MODE_OE        (1u << 7)
#define TPS55288_MODE_HICCUP    (1u << 5)
#define TPS55288_MODE_DISCHG    (1u << 4) // 100 mA sink on the output; acts with OE clear
#define TPS55288_MODE_VCC_EXT   (1u << 3)
#define TPS55288_MODE_I2CADD    (1u << 2)
#define TPS55288_MODE_FPWM      (1u << 1)
#define TPS55288_MODE_BY_REG    (1u << 0) // bits 3-1 rule instead of the MODE pin

#define TPS55288_STATUS_SCP     (1u << 7)
#define TPS55288_STATUS_OCP     (1u << 6)
#define TPS55288_STATUS_OVP     (1u << 5)

#define TPS55288_CDC_SC_MASK    (1u << 7)
#define TPS55288_CDC_OCP_MASK   (1u << 6)
#define TPS55288_CDC_OVP_MASK   (1u << 5)

#define TPS55288_VOUT_MIN_MV    800
#define TPS55288_VOUT_MAX_MV    21000 // top of the 21 V PPS range
#define TPS55288_IOUT_MAX_MA    6350

typedef enum {
    TPS55288_SR_1V25_PER_MS = 0,
    TPS55288_SR_2V5_PER_MS,  // reset default
    TPS55288_SR_5V_PER_MS,
    TPS55288_SR_10V_PER_MS,
} tps55288_slew_t;

typedef struct {
    bool output_on;
    bool discharge;   // only acts with the output off
    bool forced_pwm;  // false: light-load mode as strapped (PFM)
} tps55288_mode_t;

// Register encodings, hardware-free
uint16_t tps55288_ref_code(uint32_t mv);      // clamped to VOUT_MIN..VOUT_MAX
uint32_t tps55288_ref_mv(uint16_t code);
uint8_t  tps55288_ilim_code(uint32_t ma);     // enable bit set; rounds down, clamped
uint32_t tps55288_ilim_ma(uint8_t code);
uint8_t  tps55288_mode_byte(const tps55288_mode_t *m);

bool tps55288_probe(void);
// Internal feedback at 0.0564, slew rate, fault indication on FB/INT for
// short circuit and over-voltage. Leaves the output off at 5 V.
bool tps55288_init(tps55288_slew_t slew);
bool tps55288_set_mv(uint32_t mv);
bool tps55288_set_limit_ma(uint32_t ma);
bool tps55288_set_mode(const tps55288_mode_t *m);
bool tps55288_read_status(uint8_t *status); // reading clears SCP/OCP/OVP
