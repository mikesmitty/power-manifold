#include "backplane.h"

#include "stm32g0xx.h"

#include "blade_regs.h"
#include "board.h"
#include "hw.h"
#include "regmap.h"

// RM0444 table 173: 400 kHz from 16 MHz. A slave takes only the data setup
// and hold times from it.
#define TIMING_400K_16MHZ 0x10320309u
#define HELD_MS           50

static bool held;
static uint32_t held_since_ms;

void backplane_init(void) {
    RCC->APBENR1 |= RCC_APBENR1_I2C1EN;
    (void)RCC->APBENR1;
    I2C1->CR1 = 0;
    I2C1->TIMINGR = TIMING_400K_16MHZ;
    I2C1->OAR1 = I2C_OAR1_OA1EN | (BLADE_I2C_ADDR << 1);
    I2C1->CR1 = I2C_CR1_PE | I2C_CR1_ADDRIE | I2C_CR1_RXIE | I2C_CR1_TXIE | I2C_CR1_STOPIE |
                I2C_CR1_NACKIE | I2C_CR1_ERRIE;
    NVIC_SetPriority(I2C1_IRQn, 1);
    NVIC_EnableIRQ(I2C1_IRQn);
}

void I2C1_IRQHandler(void) {
    uint32_t isr = I2C1->ISR;

    if (isr & I2C_ISR_ADDR) {
        bool read = (isr & I2C_ISR_DIR) != 0;
        regmap_addressed(read);
        if (read) I2C1->ISR = I2C_ISR_TXE; // flush what an earlier read left in TXDR
        I2C1->ICR = I2C_ICR_ADDRCF;        // releases the clock
        return;
    }
    if (isr & I2C_ISR_RXNE) regmap_rx((uint8_t)I2C1->RXDR);
    if (isr & I2C_ISR_TXIS) I2C1->TXDR = regmap_tx();
    if (isr & I2C_ISR_NACKF) I2C1->ICR = I2C_ICR_NACKCF; // the master's end of a read
    if (isr & I2C_ISR_STOPF) {
        I2C1->ICR = I2C_ICR_STOPCF;
        regmap_stop();
    }
    if (isr & (I2C_ISR_BERR | I2C_ISR_ARLO | I2C_ISR_OVR))
        I2C1->ICR = I2C_ICR_BERRCF | I2C_ICR_ARLOCF | I2C_ICR_OVRCF;
}

// A master that went away halfway through a read (a controller restart in
// the middle of a transfer) leaves this side holding SDA low for a 0 bit,
// waiting for clock pulses that never come, and the backplane bus stuck
// whenever the mux connects this segment. SDA low with SCL high is never a
// transfer in progress: a master pauses with SCL low. Once it has lasted
// HELD_MS the peripheral is switched off and on, which lets go of the line.
// The controller clocks a held line free itself; this covers a master that
// does not. The register file needs nothing: the next address match starts
// a new transaction.
void hw_backplane_watch(uint32_t now_ms) {
    uint32_t idr = GPIOB->IDR;
    if (((idr >> PIN_SDA) & 1u) || !((idr >> PIN_SCL) & 1u)) {
        held = false;
        return;
    }
    if (!held) {
        held = true;
        held_since_ms = now_ms;
        return;
    }
    if (now_ms - held_since_ms < HELD_MS) return;
    held = false;
    NVIC_DisableIRQ(I2C1_IRQn);
    I2C1->CR1 &= ~I2C_CR1_PE;
    while (I2C1->CR1 & I2C_CR1_PE) {} // PE reads back low after the three APB cycles it needs
    I2C1->CR1 |= I2C_CR1_PE;
    NVIC_EnableIRQ(I2C1_IRQn);
}
