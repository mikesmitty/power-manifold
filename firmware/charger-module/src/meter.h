#pragma once

#include <stdint.h>

// Measurements, one fresh conversion per call, in engineering units. The
// target's are in hw/meter.c; the host tests supply their own.

uint32_t meter_vdda_mv(void);   // the 3.3 V rail, the ADC and DAC reference
uint32_t meter_vbus_mv(void);   // at the receptacle
uint32_t meter_vout_mv(void);   // converter output, ahead of the VBUS switch
uint32_t meter_iout_ma(void);   // port current
int16_t  meter_temp_conv_dc(void);
int16_t  meter_temp_plug_dc(void);
int16_t  meter_temp_mcu_dc(void);
