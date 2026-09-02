// W6100 bus on the RP2350: SPI0 on the EVB-Pico2 pins (see pins.h), chip
// select and reset as plain GPIOs. INTn is wired up by eth.c.

#include "w6100_hw.h"

#include "hardware/gpio.h"
#include "hardware/spi.h"
#include "pico/stdlib.h"

#include "pins.h"

void w6100_hw_init(void) {
    spi_init(ETH_SPI, ETH_SPI_HZ);
    spi_set_format(ETH_SPI, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    gpio_set_function(PIN_ETH_MISO, GPIO_FUNC_SPI);
    gpio_set_function(PIN_ETH_SCK, GPIO_FUNC_SPI);
    gpio_set_function(PIN_ETH_MOSI, GPIO_FUNC_SPI);
    gpio_init(PIN_ETH_CS_N);
    gpio_put(PIN_ETH_CS_N, 1);
    gpio_set_dir(PIN_ETH_CS_N, GPIO_OUT);
    gpio_init(PIN_ETH_RST_N);
    gpio_put(PIN_ETH_RST_N, 1);
    gpio_set_dir(PIN_ETH_RST_N, GPIO_OUT);
}

void w6100_hw_reset(bool asserted) {
    gpio_put(PIN_ETH_RST_N, !asserted);
}

void w6100_hw_select(bool selected) {
    gpio_put(PIN_ETH_CS_N, !selected);
}

void w6100_hw_write(const uint8_t *src, size_t n) {
    spi_write_blocking(ETH_SPI, src, n);
}

void w6100_hw_read(uint8_t *dst, size_t n) {
    spi_read_blocking(ETH_SPI, 0x00, dst, n);
}

void w6100_hw_delay_ms(uint32_t ms) {
    sleep_ms(ms);
}

uint32_t w6100_hw_ms(void) {
    return to_ms_since_boot(get_absolute_time());
}
