#pragma once

#include <stdint.h>

// ADC counts (12 bit, right aligned) to engineering units. Hardware-free:
// the factory calibration words and the raw samples come from the caller.

#define SENSE_ADC_MAX       4095
#define SENSE_CAL_VDDA_MV   3000 // the rail the factory calibration words were taken at
#define SENSE_TEMP_OPEN_DC  INT16_MIN // NTC missing or shorted

// The 3.3 V rail from a VREFINT sample and the part's VREFINT_CAL word. The
// rail is the ADC and DAC reference, so every absolute figure hangs on it.
uint32_t sense_vdda_mv(uint16_t vrefint_raw, uint16_t vrefint_cal);

uint32_t sense_rail_mv(uint16_t raw, uint32_t vdda_mv);  // VBUS_SENSE / VOUT_SENSE, 1/11
uint32_t sense_iana_ma(uint16_t raw, uint32_t vdda_mv);  // TCPP02 IANA

// 10k B3380 NTC to ground under a 10k pull-up: ratiometric, no rail term.
// Tenths of a degree, -40.0 to 125.0; SENSE_TEMP_OPEN_DC past either rail.
int16_t sense_ntc_dc(uint16_t raw);

// Die temperature from the TS_CAL1 (30 degC) / TS_CAL2 (130 degC) words
int16_t sense_mcu_dc(uint16_t raw, uint32_t vdda_mv, uint16_t ts_cal1, uint16_t ts_cal2);
