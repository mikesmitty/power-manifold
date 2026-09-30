#include "console.h"

#include "stm32g0xx.h"

#define PCLK_HZ 16000000u
#define BAUD    115200u

// Interrupt driven, so a report never holds up the PD stack's loop. What
// does not fit the buffer is dropped.
#define RING 512u // power of two
static volatile uint8_t ring[RING];
static volatile uint16_t head, tail; // head: next to write, tail: next to send

void console_init(void) {
    RCC->APBENR1 |= RCC_APBENR1_USART2EN;
    (void)RCC->APBENR1;
    USART2->BRR = (PCLK_HZ + BAUD / 2) / BAUD;
    USART2->CR1 = USART_CR1_TE | USART_CR1_UE;
    NVIC_SetPriority(USART2_IRQn, 3);
    NVIC_EnableIRQ(USART2_IRQn);
}

void USART2_IRQHandler(void) {
    if (!(USART2->ISR & USART_ISR_TXE_TXFNF)) return;
    if (tail == head) {
        USART2->CR1 &= ~USART_CR1_TXEIE_TXFNFIE;
        return;
    }
    USART2->TDR = ring[tail];
    tail = (uint16_t)((tail + 1u) % RING);
}

static void put(char c) {
    uint16_t next = (uint16_t)((head + 1u) % RING);
    if (next == tail) return; // full
    ring[head] = (uint8_t)c;
    head = next;
    // read-modify-write of CR1 against the handler clearing the same bit
    __disable_irq();
    USART2->CR1 |= USART_CR1_TXEIE_TXFNFIE;
    __enable_irq();
}

void console_str(const char *s) {
    for (; *s; s++) {
        if (*s == '\n') put('\r');
        put(*s);
    }
}

void console_dec(int32_t v) {
    char buf[12];
    unsigned n = 0;
    uint32_t u = v < 0 ? 0u - (uint32_t)v : (uint32_t)v;
    do {
        buf[n++] = (char)('0' + u % 10);
        u /= 10;
    } while (u);
    if (v < 0) put('-');
    while (n) put(buf[--n]);
}

void console_tenths(int32_t v) {
    if (v < 0) {
        put('-');
        v = -v;
    }
    console_dec(v / 10);
    put('.');
    put((char)('0' + v % 10));
}

void console_hex(uint32_t v, unsigned digits) {
    while (digits--) put("0123456789abcdef"[(v >> (digits * 4)) & 0xF]);
}
