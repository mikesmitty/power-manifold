// UPS serial link on the RP2350: UART0 on the controller card's UPS header,
// 9600 8N1 (the LAD's fixed setting). Receive runs off the UART interrupt
// into a small ring so a reply is never lost to a slow main-loop turn (a
// settings save or an OTA chunk can hold the loop longer than the UART's
// 32-byte FIFO covers). Transmit is a request of at most 13 bytes, which
// the FIFO absorbs without blocking.

#include "lad_hw.h"

#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/uart.h"
#include "pico/stdlib.h"

#include "pins.h"

#define RING_SIZE 128 // power of two

static volatile uint8_t ring[RING_SIZE];
static volatile uint8_t ring_wr, ring_rd;

static void uart_irq(void) {
    while (uart_is_readable(UPS_UART)) {
        uint8_t b = (uint8_t)uart_getc(UPS_UART);
        uint8_t next = (uint8_t)((ring_wr + 1) & (RING_SIZE - 1));
        if (next == ring_rd) continue; // full: drop, the poller will time out and retry
        ring[ring_wr] = b;
        ring_wr = next;
    }
}

void lad_hw_init(void) {
    uart_init(UPS_UART, UPS_BAUD);
    uart_set_format(UPS_UART, 8, 1, UART_PARITY_NONE);
    uart_set_hw_flow(UPS_UART, false, false);
    uart_set_fifo_enabled(UPS_UART, true);
    gpio_set_function(PIN_UPS_TX, GPIO_FUNC_UART);
    gpio_set_function(PIN_UPS_RX, GPIO_FUNC_UART);
    // idle-high on RX while nothing is plugged in, so the line does not sit
    // at the pad's default pull-down and read as a permanent break
    gpio_set_pulls(PIN_UPS_RX, true, false);

    irq_set_exclusive_handler(UPS_UART_IRQ, uart_irq);
    irq_set_enabled(UPS_UART_IRQ, true);
    uart_set_irq_enables(UPS_UART, true, false); // RX + RX timeout
}

void lad_hw_write(const uint8_t *src, size_t n) {
    uart_write_blocking(UPS_UART, src, n);
}

size_t lad_hw_read(uint8_t *dst, size_t cap) {
    size_t n = 0;
    while (n < cap && ring_rd != ring_wr) {
        dst[n++] = ring[ring_rd];
        ring_rd = (uint8_t)((ring_rd + 1) & (RING_SIZE - 1));
    }
    return n;
}
