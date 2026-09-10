#include "vin_hw.h"

#include "pins.h"

#ifdef PWRMAN_CONTROLLER_CARD

#include "hardware/adc.h"

bool vin_hw_init(void) {
    adc_init();
    adc_gpio_init(PIN_VIN_SENSE); // digital function and pulls off the pad
    return true;
}

uint16_t vin_hw_read(void) {
    adc_select_input(VIN_ADC_INPUT); // nothing else uses the ADC, but it is one register
    return adc_read();
}

#else

// The pcie-breakout brings no VIN to an ADC pin.
bool vin_hw_init(void) { return false; }
uint16_t vin_hw_read(void) { return 0; }

#endif
