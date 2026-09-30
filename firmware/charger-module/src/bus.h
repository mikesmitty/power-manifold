#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// The blade's private I2C bus (I2C2): TPS55288 and TCPP02. The part drivers
// reach it only through these, so the host tests stand in for the parts.

bool bus_probe(uint8_t addr); // address ACKed
bool bus_write(uint8_t addr, const uint8_t *buf, size_t len);
bool bus_read_reg(uint8_t addr, uint8_t reg, uint8_t *buf, size_t len);
