#pragma once

#include <stdint.h>

// ADC1, one polled 12-bit conversion at a time (ADC_CH_* in board.h)

void     adc_init(void);         // regulator, self-calibration, internal channels on
uint16_t adc_read(uint8_t channel);

// Factory calibration words from the engineering bytes (DS12232 tables 5, 6)
uint16_t adc_vrefint_cal(void);
uint16_t adc_ts_cal1(void);
uint16_t adc_ts_cal2(void);
