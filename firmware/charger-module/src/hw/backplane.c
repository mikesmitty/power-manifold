#include "backplane.h"

#include "stm32g0xx.h"

#include "blade_regs.h"
#include "regmap.h"

// RM0444 table 173: 400 kHz from 16 MHz. A slave takes only the data setup
// and hold times from it.
#define TIMING_400K_16MHZ 0x10320309u

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
