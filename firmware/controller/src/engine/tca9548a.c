#include "tca9548a.h"

#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "pico/time.h"

#include "pins.h"

#define I2C_TIMEOUT_US (2 * 1000)

static int8_t current = -1; // cached selection; -1 = unknown/none

void tca9548a_init(void) {
    gpio_init(PIN_MUX_RST_N);
    gpio_put(PIN_MUX_RST_N, 1);
    gpio_set_dir(PIN_MUX_RST_N, GPIO_OUT);
    tca9548a_hw_reset();
}

static bool write_ctrl(uint8_t mask) {
    return i2c_write_timeout_us(I2C_BUS, ADDR_TCA9548A, &mask, 1, false,
                                I2C_TIMEOUT_US) == 1;
}

bool tca9548a_select(uint8_t channel) {
    if (current == (int8_t)channel) return true;
    if (!write_ctrl(1u << channel)) {
        current = -1;
        return false;
    }
    current = (int8_t)channel;
    return true;
}

bool tca9548a_deselect_all(void) {
    if (current == -2) return true;
    if (!write_ctrl(0)) {
        current = -1;
        return false;
    }
    current = -2; // -2 = known deselected
    return true;
}

void tca9548a_hw_reset(void) {
    gpio_put(PIN_MUX_RST_N, 0);
    sleep_us(1); // t_WL min 6ns
    gpio_put(PIN_MUX_RST_N, 1);
    sleep_us(1);
    current = -1;
}
