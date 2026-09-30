#include "adc.h"

#include "stm32g0xx.h"

#include "hw.h"

#define CAL_VREFINT (*(const uint16_t *)0x1FFF75AAu)
#define CAL_TS_30C  (*(const uint16_t *)0x1FFF75A8u)
#define CAL_TS_130C (*(const uint16_t *)0x1FFF75CAu)

// Every wait is bounded: a flag that never comes must not hang the main
// loop into a watchdog reset. A missed one shows as a wrong reading instead.
#define WAIT_MS 2

static void wait_cr_clear(uint32_t bit) {
    uint32_t t0 = hw_ms();
    while ((ADC1->CR & bit) && hw_ms() - t0 < WAIT_MS) {}
}

static void wait_isr_set(uint32_t bit) {
    uint32_t t0 = hw_ms();
    while (!(ADC1->ISR & bit) && hw_ms() - t0 < WAIT_MS) {}
}

void adc_init(void) {
    RCC->APBENR2 |= RCC_APBENR2_ADCEN;
    (void)RCC->APBENR2;

    ADC1->CR = ADC_CR_ADVREGEN;
    hw_delay_ms(2); // regulator start-up
    ADC1->CR |= ADC_CR_ADCAL;
    wait_cr_clear(ADC_CR_ADCAL);
    hw_delay_ms(1); // ADEN is refused for a few ADC clocks after calibration

    ADC1->ISR = ADC_ISR_ADRDY;
    ADC1->CR |= ADC_CR_ADEN;
    wait_isr_set(ADC_ISR_ADRDY);

    // 160.5 cycles at 16 MHz = 10 us: covers the internal channels' minimum
    // and the 9.1k the dividers present
    ADC1->SMPR = ADC_SMPR_SMP1;
    ADC1_COMMON->CCR |= ADC_CCR_VREFEN | ADC_CCR_TSEN;
    hw_delay_ms(1); // reference buffer and sensor start-up
}

uint16_t adc_read(uint8_t channel) {
    ADC1->ISR = ADC_ISR_CCRDY | ADC_ISR_EOC;
    ADC1->CHSELR = 1u << channel;
    wait_isr_set(ADC_ISR_CCRDY);
    ADC1->CR |= ADC_CR_ADSTART;
    wait_isr_set(ADC_ISR_EOC);
    return (uint16_t)ADC1->DR;
}

uint16_t adc_vrefint_cal(void) { return CAL_VREFINT; }
uint16_t adc_ts_cal1(void) { return CAL_TS_30C; }
uint16_t adc_ts_cal2(void) { return CAL_TS_130C; }
