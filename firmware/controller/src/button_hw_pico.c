#include "button_hw.h"

#include "pins.h"

#ifdef PIN_BUTTON

#include "hardware/gpio.h"

bool button_hw_init(void) {
    gpio_init(PIN_BUTTON);
    gpio_set_dir(PIN_BUTTON, GPIO_IN);
    gpio_pull_up(PIN_BUTTON); // the switch pulls the pin to ground; nothing else does
    return true;
}

bool button_hw_pressed(void) {
    return !gpio_get(PIN_BUTTON);
}

#else

bool button_hw_init(void) { return false; }
bool button_hw_pressed(void) { return false; }

#endif
