#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "blade_regs.h"

// The register file behind the backplane I2C slave (blade_regs.h).
// Hardware-free: the I2C1 interrupt feeds the bus side, the main loop the
// firmware side. On the target the main loop masks that interrupt around
// each firmware-side call; nothing here blocks.

typedef struct {
    uint8_t  status;       // BLADE_ST_* but CONFIGURED and FAULT, which are kept here
    uint8_t  pdo;
    uint16_t contract_mv;
    uint16_t contract_ma;
    uint8_t  hr_sent;      // BLADE_REG_HR_SENT
    uint8_t  hr_received;  // BLADE_REG_HR_RECEIVED
    uint16_t vbus_mv;
    uint16_t iout_ma;
    uint16_t vout_mv;
    int16_t  temp_conv_dc;
    int16_t  temp_plug_dc;
    int16_t  temp_mcu_dc;
} regmap_live_t;

typedef struct {
    bool     port_en;
    uint16_t max_ma;
    uint16_t max_mv;
    uint8_t  watch_s;  // BLADE_REG_WATCH_S
} regmap_config_t;

void regmap_init(uint8_t reset_cause); // port off, limits 0, BLADE_FAULT_RESET latched
void regmap_set_boot(uint8_t flags);   // BLADE_REG_BOOT

// Bus side
void    regmap_addressed(bool read); // address match, also after a repeated start
void    regmap_rx(uint8_t byte);     // first byte of a write is the pointer
uint8_t regmap_tx(void);
void    regmap_stop(void);

// Firmware side
void     regmap_publish(const regmap_live_t *live);
void     regmap_raise(uint16_t faults);       // latch BLADE_FAULT_*
uint16_t regmap_faults(void);
bool     regmap_config(regmap_config_t *out); // true when written since the last call
uint8_t  regmap_command(void);                // next BLADE_CMD_*, 0 = none
uint32_t regmap_transactions(void);           // times the controller has addressed the blade
