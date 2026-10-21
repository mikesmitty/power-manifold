#include "tca9548a.h"

#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "pico/time.h"

#include "i2c_txn.h"
#include "pins.h"
#include "rst_line.h"

#define I2C_TIMEOUT_US (2 * 1000)

#define CLEAR_HALF_US  5 // 100 kHz pulses

static int8_t current = -1; // cached selection; -1 = unknown/none
static int freed = -1;      // tca9548a_take_freed

// The I2C block back to idle. A held line appearing when the mux connects
// its segment looks to the block like another master's START (SDA falling
// while SCL is high), and the block then waits for that master's STOP
// before it sends anything: with the line freed, or the segment cut off by
// a mux reset, that STOP never comes, and every transfer timed out on the
// bench with a command left sitting in its FIFO.
static void reset_controller(void) {
    i2c_init(I2C_BUS, I2C_BAUD);
}

// A device whose master went away halfway through a transfer (a controller
// restart in the middle of one) holds SDA low and waits for clock pulses
// that never come; the bus is stuck whenever its segment is connected. SDA
// low with SCL high is never a transfer in progress: this core is the only
// master, and it is not in one here. SCL is pulsed, nine times at most, the
// I2C specification's bus clear (UM10204 3.1.16), and each pulse ends with
// a STOP: SDA taken low while SCL is low and let go while SCL is high. A
// device sending a byte lets go of SDA for its next 1 bit, or for the
// acknowledge, and that STOP then ends its transfer; stopping at the first
// high SDA alone left one halfway through its byte. SCL held low is a
// device stretching the clock, and is left alone.
static void free_data_line(int channel) {
    if (gpio_get(PIN_I2C_SDA) || !gpio_get(PIN_I2C_SCL)) return;
    // released = input, the bus pull-ups hold it high; low = output at 0
    gpio_put(PIN_I2C_SCL, 0);
    gpio_put(PIN_I2C_SDA, 0);
    gpio_set_dir(PIN_I2C_SCL, GPIO_IN);
    gpio_set_dir(PIN_I2C_SDA, GPIO_IN);
    gpio_set_function(PIN_I2C_SCL, GPIO_FUNC_SIO);
    gpio_set_function(PIN_I2C_SDA, GPIO_FUNC_SIO);
    bool let_go = false; // a line shorted low never is, and is not reported
    for (int k = 0; k < 9 && !let_go; k++) {
        gpio_set_dir(PIN_I2C_SCL, GPIO_OUT);
        sleep_us(CLEAR_HALF_US);
        gpio_set_dir(PIN_I2C_SDA, GPIO_OUT);
        sleep_us(CLEAR_HALF_US);
        gpio_set_dir(PIN_I2C_SCL, GPIO_IN);
        sleep_us(CLEAR_HALF_US);
        gpio_set_dir(PIN_I2C_SDA, GPIO_IN);
        sleep_us(CLEAR_HALF_US);
        let_go = gpio_get(PIN_I2C_SDA);
    }
    gpio_set_function(PIN_I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(PIN_I2C_SCL, GPIO_FUNC_I2C);
    reset_controller();
    if (let_go) freed = channel;
}

int tca9548a_take_freed(void) {
    int c = freed;
    freed = -1;
    return c;
}

void tca9548a_init(void) {
    rst_line_setup(PIN_MUX_RST_N); // released (rst_line.h)
    // A reset only clears the channel selection, so it is harmless on a warm
    // start too; the engine reselects before every transfer.
    tca9548a_hw_reset();
}

static bool write_ctrl(uint8_t mask) {
    bool t = i2c_txn_begin();
    bool ok = i2c_write_timeout_us(I2C_BUS, ADDR_TCA9548A, &mask, 1, false,
                                   I2C_TIMEOUT_US) == 1;
    i2c_txn_end(t);
    return ok;
}

bool tca9548a_select(uint8_t channel) {
    if (current != (int8_t)channel) {
        free_data_line(current >= 0 ? current : TCA9548A_UPSTREAM); // or the write cannot get through
        if (!write_ctrl(1u << channel)) {
            current = -1;
            return false;
        }
        current = (int8_t)channel;
    }
    free_data_line(channel);
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
    rst_line_assert(PIN_MUX_RST_N);
    sleep_us(1); // t_WL min 6ns
    rst_line_release(PIN_MUX_RST_N);
    sleep_us(10); // the line rises through the backplane's 10k
    current = -1;
    reset_controller();
}
