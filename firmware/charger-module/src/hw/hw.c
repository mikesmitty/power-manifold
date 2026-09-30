#include "hw.h"

#include "stm32g0xx.h"

#include "blade_regs.h"
#include "board.h"

#define HSI_HZ 16000000u // reset clock: HSI16, no PLL

static volatile uint32_t ms;
static volatile bool ovp_trip;
static uint8_t reset_cause;

void (*hw_tick_hook)(void);

// ST's startup file runs __libc_init_array, which calls this; the C runtime's
// own start files are not linked
void _init(void) {}

void SysTick_Handler(void) {
    ms++;
    if (hw_tick_hook) hw_tick_hook();
}

// TCPP_EN falling: an over-voltage comparator has pulled it low, or the
// firmware has (which clears the latch afterwards)
void EXTI4_15_IRQHandler(void) {
    if (EXTI->FPR1 & (1u << PIN_TCPP_EN_SENSE)) {
        EXTI->FPR1 = 1u << PIN_TCPP_EN_SENSE;
        ovp_trip = true;
    }
}

static void pin_mode(GPIO_TypeDef *port, unsigned pin, unsigned mode) {
    port->MODER = (port->MODER & ~(3u << (pin * 2))) | (mode << (pin * 2));
}

static void pin_af(GPIO_TypeDef *port, unsigned pin, unsigned af) {
    volatile uint32_t *afr = &port->AFR[pin / 8];
    unsigned shift = (pin % 8) * 4;
    *afr = (*afr & ~(0xFu << shift)) | (af << shift);
    pin_mode(port, pin, 2);
}

static void pin_output(GPIO_TypeDef *port, unsigned pin, bool open_drain, bool level) {
    port->BSRR = level ? (1u << pin) : (1u << (pin + 16)); // level first: no glitch
    if (open_drain) port->OTYPER |= 1u << pin;
    else port->OTYPER &= ~(1u << pin);
    pin_mode(port, pin, 1);
}

static uint8_t read_reset_cause(void) {
    uint32_t csr = RCC->CSR;
    RCC->CSR |= RCC_CSR_RMVF;
    uint8_t cause = 0;
    if (csr & RCC_CSR_PWRRSTF) cause |= BLADE_RESET_POWER;
    if (csr & RCC_CSR_IWDGRSTF) cause |= BLADE_RESET_WATCHDOG;
    if (csr & RCC_CSR_SFTRSTF) cause |= BLADE_RESET_SOFTWARE;
    if (csr & (RCC_CSR_OBLRSTF | RCC_CSR_LPWRRSTF | RCC_CSR_WWDGRSTF)) cause |= BLADE_RESET_OTHER;
    // NRST follows every other cause (the pin is driven by the internal
    // reset too): it only counts on its own
    if (!cause && (csr & RCC_CSR_PINRSTF)) cause = BLADE_RESET_PIN;
    return cause;
}

static void watchdog_start(void) {
#ifdef BLADE_WATCHDOG
    RCC->APBENR1 |= RCC_APBENR1_DBGEN;
    DBG->APBFZ1 |= DBG_APB_FZ1_DBG_IWDG_STOP; // a halted core does not reset the blade
    IWDG->KR = 0xCCCC;          // start (also starts the LSI)
    IWDG->KR = 0x5555;          // unlock PR / RLR
    IWDG->PR = 3;               // LSI 32 kHz / 32: 1 ms per count
    IWDG->RLR = 1000 - 1;
    while (IWDG->SR) {}
    IWDG->KR = 0xAAAA;
#endif
}

void hw_init(void) {
    reset_cause = read_reset_cause();

    RCC->IOPENR |= RCC_IOPENR_GPIOAEN | RCC_IOPENR_GPIOBEN;
    RCC->APBENR2 |= RCC_APBENR2_SYSCFGEN;
    (void)RCC->APBENR2;

    // Every pin but SWDIO/SWCLK resets to analog, which is where the ADC,
    // DAC and CC pins and the unconnected ones stay.
    pin_output(GPIOA, PIN_TCPP_EN_DRV, false, false);
    pin_output(GPIOB, PIN_ALERT_N, true, true);
    pin_output(GPIOB, PIN_LED, true, true); // open drain: the LED's anode is on 5 V
    pin_mode(GPIOB, PIN_EN, 0);
    pin_mode(GPIOB, PIN_TCPP_EN_SENSE, 0);
    pin_mode(GPIOB, PIN_CONVERTER_FLT, 0);
    pin_mode(GPIOB, PIN_FLG_N, 0);

    GPIOA->OTYPER &= ~(1u << PIN_USART_TX);
    pin_af(GPIOA, PIN_USART_TX, AF_USART2);
    pin_af(GPIOA, PIN_USART_RX, AF_USART2);
    GPIOA->PUPDR = (GPIOA->PUPDR & ~(3u << (PIN_USART_RX * 2))) | (1u << (PIN_USART_RX * 2)); // open pad: pull up

    // I2C pins open drain, pulled up on the board to 3.3 V
    GPIOB->OTYPER |= (1u << PIN_SCL) | (1u << PIN_SDA) | (1u << PIN_USB_SCL) | (1u << PIN_USB_SDA);
    pin_af(GPIOB, PIN_SCL, AF_I2C);
    pin_af(GPIOB, PIN_SDA, AF_I2C);
    pin_af(GPIOB, PIN_USB_SCL, AF_I2C);
    pin_af(GPIOB, PIN_USB_SDA, AF_I2C);

    // PB4, falling edge
    EXTI->EXTICR[PIN_TCPP_EN_SENSE / 4] =
        (EXTI->EXTICR[PIN_TCPP_EN_SENSE / 4] & ~(0xFFu << ((PIN_TCPP_EN_SENSE % 4) * 8))) |
        (1u << ((PIN_TCPP_EN_SENSE % 4) * 8)); // port B
    EXTI->FTSR1 |= 1u << PIN_TCPP_EN_SENSE;
    EXTI->FPR1 = 1u << PIN_TCPP_EN_SENSE;
    EXTI->IMR1 |= 1u << PIN_TCPP_EN_SENSE;
    NVIC_SetPriority(EXTI4_15_IRQn, 1);
    NVIC_EnableIRQ(EXTI4_15_IRQn);

    SysTick_Config(HSI_HZ / 1000);
    watchdog_start();
}

uint32_t hw_ms(void) {
    return ms;
}

void hw_delay_ms(uint32_t delay) {
    uint32_t t0 = ms;
    while (ms - t0 < delay) hw_watchdog_feed();
}

uint8_t hw_reset_cause(void) {
    return reset_cause;
}

void hw_watchdog_feed(void) {
#ifdef BLADE_WATCHDOG
    IWDG->KR = 0xAAAA;
#endif
}

bool hw_en(void) { return (GPIOB->IDR >> PIN_EN) & 1u; }
bool hw_converter_fault(void) { return !((GPIOB->IDR >> PIN_CONVERTER_FLT) & 1u); }
bool hw_port_fault(void) { return !((GPIOB->IDR >> PIN_FLG_N) & 1u); }
bool hw_tcpp_en_sense(void) { return (GPIOB->IDR >> PIN_TCPP_EN_SENSE) & 1u; }
bool hw_ovp_trip_latched(void) { return ovp_trip; }
void hw_ovp_trip_clear(void) { ovp_trip = false; }

void hw_tcpp_en(bool on) {
    GPIOA->BSRR = on ? (1u << PIN_TCPP_EN_DRV) : (1u << (PIN_TCPP_EN_DRV + 16));
}

void hw_alert(bool asserted) {
    GPIOB->BSRR = asserted ? (1u << (PIN_ALERT_N + 16)) : (1u << PIN_ALERT_N);
}

void hw_led(bool on) {
    GPIOB->BSRR = on ? (1u << (PIN_LED + 16)) : (1u << PIN_LED);
}

void hw_backplane_lock(void) {
    NVIC_DisableIRQ(I2C1_IRQn);
}

void hw_backplane_unlock(void) {
    NVIC_EnableIRQ(I2C1_IRQn);
}
