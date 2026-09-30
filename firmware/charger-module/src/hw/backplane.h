#pragma once

// I2C1 slave on PB6/PB7 at BLADE_I2C_ADDR, interrupt driven: the bus side of
// the register file (regmap.h).

void backplane_init(void);
