#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// The private bus with a TPS55288 and a TCPP02 on it, as far as the drivers
// can tell: register storage, the TCPP02's acknowledge register following
// its control register, and a log of the write transfers.

#define FAKE_BUS_LOG 32

typedef struct {
    uint8_t addr;
    uint8_t len;
    uint8_t data[4];
} fake_bus_write_t;

void fake_bus_reset(void);
void fake_bus_set_present(uint8_t addr, bool present);
void fake_bus_poke(uint8_t addr, uint8_t reg, uint8_t val);
uint8_t fake_bus_peek(uint8_t addr, uint8_t reg);
void fake_bus_stick_tcpp02_ack(bool stuck); // the acknowledge register stops following

void fake_bus_on_write(void (*observer)(void)); // called after every write transfer

unsigned fake_bus_write_count(void);
const fake_bus_write_t *fake_bus_write_at(unsigned i);
