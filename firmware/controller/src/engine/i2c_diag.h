#pragma once
// Bench diagnostic: raw access to the backplane I2C bus from the console,
// executed on the engine core (the bus owner) between ticks. Real builds
// only. arg packing: [31:28] op, [27:24] mux channel (0-5, 0xF = none / port
// for EN), [23:16] address, [15:8] register, [7:0] byte count / value / on.
#include <stdbool.h>
#include <stdint.h>

enum { I2C_DIAG_SCAN = 1, I2C_DIAG_READ = 2, I2C_DIAG_WRITE = 3, I2C_DIAG_EN = 4 };

typedef struct {
    uint8_t  op, ok, selected, count;
    uint8_t  data[16];
    uint32_t found[4]; // scan: bit per 7-bit address
} i2c_diag_result_t;

extern i2c_diag_result_t g_i2c_diag;
extern volatile uint32_t g_i2c_diag_seq; // bumped by the engine when a result lands

void i2c_diag_run(uint32_t arg); // engine core
