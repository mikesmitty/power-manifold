#include "meter.h"

#include "adc.h"
#include "board.h"
#include "sense.h"

uint32_t meter_vdda_mv(void) {
    return sense_vdda_mv(adc_read(ADC_CH_VREFINT), adc_vrefint_cal());
}

uint32_t meter_vbus_mv(void) {
    return sense_rail_mv(adc_read(ADC_CH_VBUS), meter_vdda_mv());
}

uint32_t meter_vout_mv(void) {
    return sense_rail_mv(adc_read(ADC_CH_VOUT), meter_vdda_mv());
}

uint32_t meter_iout_ma(void) {
    return sense_iana_ma(adc_read(ADC_CH_IANA), meter_vdda_mv());
}

int16_t meter_temp_conv_dc(void) {
    return sense_ntc_dc(adc_read(ADC_CH_NTC_CONV));
}

int16_t meter_temp_plug_dc(void) {
    return sense_ntc_dc(adc_read(ADC_CH_NTC_PLUG));
}

int16_t meter_temp_mcu_dc(void) {
    return sense_mcu_dc(adc_read(ADC_CH_TEMP), meter_vdda_mv(), adc_ts_cal1(), adc_ts_cal2());
}
