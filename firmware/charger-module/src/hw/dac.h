#pragma once

#include <stdint.h>

// DAC channel 1 on PA4: the tracking OVP threshold (ovp.h)

void dac_init(void);            // output at 0 V
void dac_set(uint16_t code);    // 12 bit
