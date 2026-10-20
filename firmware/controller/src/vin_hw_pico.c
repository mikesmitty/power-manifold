#include "vin_hw.h"

#include "pins.h"

#ifdef PWRMAN_CONTROLLER_CARD

#include "hardware/adc.h"
#include "pico/time.h"

bool vin_hw_init(void) {
    adc_init();
    adc_gpio_init(PIN_VIN_SENSE); // digital function and pulls off the pad
    // Until now the pad's default pull-down loaded the divider, so C18 sits
    // below the true reading; it recovers through the divider with a time
    // constant of about 1 ms. Read straight away, the first sample came out
    // 23 % low and set the bus-sag cap at every power-up.
    sleep_ms(10);
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
