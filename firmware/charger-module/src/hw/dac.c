#include "dac.h"

#include "stm32g0xx.h"

void dac_init(void) {
    RCC->APBENR1 |= RCC_APBENR1_DAC1EN;
    (void)RCC->APBENR1;
    DAC1->DHR12R1 = 0;
    DAC1->CR |= DAC_CR_EN1; // reset mode: buffered, on the pin
}

void dac_set(uint16_t code) {
    DAC1->DHR12R1 = code & 0xFFFu;
}
