#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "blade_regs.h"

// Gen-3 charger blade (STM32G071 + TPS55288 + TCPP02-M18): the register file
// in blade_regs.h at BLADE_I2C_ADDR, one per blade behind the mux (select the
// channel first). The blade's MCU runs from the slot's 5 V, so it answers
// with EN low; the port itself stays dark until the blade has been configured
// and EN is high. Values are little-endian; a read is a snapshot of the
// moment the blade was addressed, a configuration write takes effect when
// the transfer ends.

typedef struct {
    uint8_t  status;      // BLADE_ST_*
    uint8_t  pdo;         // object position of the contract, 0 = none
    uint16_t faults;      // BLADE_FAULT_*, latched on the blade
    uint16_t contract_mv;
    uint16_t contract_ma; // the operating current the sink asked for
    uint16_t vbus_mv;
    uint16_t iout_ma;
    uint16_t vout_mv;     // converter output ahead of the VBUS switch
    int16_t  temp_conv_dc; // 0.1 degC; INT16_MIN = no reading (NTC open or shorted)
    int16_t  temp_plug_dc;
    int16_t  temp_mcu_dc;
} blade3_status_t;

typedef struct {
    bool     port_en;
    uint16_t max_ma;
    uint16_t max_mv;
} blade3_config_t;

bool blade3_probe(void); // WHO_AM_I
bool blade3_read_status(blade3_status_t *s); // BLADE_REG_STATUS .. TEMP_MCU in one read
bool blade3_read_config(blade3_config_t *c);
// Limits first, then CONTROL, so the port never arms on limits it has not
// been given yet.
bool blade3_write_config(const blade3_config_t *c);
bool blade3_command(uint8_t cmd); // BLADE_CMD_*
