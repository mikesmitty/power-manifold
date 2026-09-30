#include "private_bus.h"

#include "stm32g0xx.h"

#include "bus.h"
#include "hw.h"

// RM0444 table 173: 100 kHz from a 16 MHz kernel clock
#define TIMING_100K_16MHZ 0x30420F13u
#define TIMEOUT_MS        5

void private_bus_init(void) {
    RCC->APBENR1 |= RCC_APBENR1_I2C2EN;
    (void)RCC->APBENR1;
    I2C2->CR1 = 0;
    I2C2->TIMINGR = TIMING_100K_16MHZ;
    I2C2->CR1 = I2C_CR1_PE;
}

// A transfer that did not finish leaves the peripheral mid-state: PE off and
// on again returns it to idle and releases the lines.
static void recover(void) {
    I2C2->CR1 &= ~I2C_CR1_PE;
    while (I2C2->CR1 & I2C_CR1_PE) {}
    I2C2->CR1 |= I2C_CR1_PE;
}

static bool wait_for(uint32_t flag) {
    uint32_t t0 = hw_ms();
    for (;;) {
        uint32_t isr = I2C2->ISR;
        if (isr & (I2C_ISR_NACKF | I2C_ISR_BERR | I2C_ISR_ARLO)) return false;
        if (isr & flag) return true;
        if (hw_ms() - t0 > TIMEOUT_MS) return false;
    }
}

static bool finish(bool ok) {
    // with AUTOEND the STOP follows the last byte, and a NACK, by itself
    if (ok) ok = wait_for(I2C_ISR_STOPF);
    if (!ok) {
        recover();
        return false;
    }
    I2C2->ICR = I2C_ICR_STOPCF;
    return true;
}

static void start(uint8_t addr, size_t len, uint32_t flags) {
    I2C2->ICR = I2C_ICR_STOPCF | I2C_ICR_NACKCF | I2C_ICR_BERRCF | I2C_ICR_ARLOCF;
    I2C2->CR2 = ((uint32_t)addr << 1) | ((uint32_t)len << I2C_CR2_NBYTES_Pos) | flags |
                I2C_CR2_START;
}

bool bus_probe(uint8_t addr) {
    start(addr, 0, I2C_CR2_AUTOEND);
    return finish(true);
}

bool bus_write(uint8_t addr, const uint8_t *buf, size_t len) {
    if (!len || len > 255) return false;
    start(addr, len, I2C_CR2_AUTOEND);
    bool ok = true;
    for (size_t i = 0; ok && i < len; i++) {
        ok = wait_for(I2C_ISR_TXIS);
        if (ok) I2C2->TXDR = buf[i];
    }
    return finish(ok);
}

bool bus_read_reg(uint8_t addr, uint8_t reg, uint8_t *buf, size_t len) {
    if (!len || len > 255) return false;
    start(addr, 1, 0); // no STOP: the read follows a repeated start
    bool ok = wait_for(I2C_ISR_TXIS);
    if (ok) {
        I2C2->TXDR = reg;
        ok = wait_for(I2C_ISR_TC);
    }
    if (!ok) return finish(false);

    start(addr, len, I2C_CR2_RD_WRN | I2C_CR2_AUTOEND);
    for (size_t i = 0; ok && i < len; i++) {
        ok = wait_for(I2C_ISR_RXNE);
        if (ok) buf[i] = (uint8_t)I2C2->RXDR;
    }
    return finish(ok);
}
