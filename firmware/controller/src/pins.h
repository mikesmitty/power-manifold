#pragma once

// Controller GPIO map. Two boards run this firmware:
//   - the Pico 2 W on the pcie-breakout (the development carrier; also the
//     W6100-EVB-Pico2 in the same socket): the default PICO_BOARD=pico2_w;
//   - the production controller card, hardware/controller:
//     PICO_BOARD=pwrman_controller_card (boards/), which defines
//     PWRMAN_CONTROLLER_CARD.
// I2C, the Ethernet pins and the UPS link are common to both; the other
// backplane signals and the reset drivers differ.
//
// GP16-GP21 are RESERVED for the wired-Ethernet path. WIZnet's EVB-Pico2
// boards put the Ethernet chip on SPI0 GP16-19 with RSTn=GP20 / INTn=GP21, so
// a W6100-EVB-Pico2 drops into the dev socket unchanged, and the controller
// card copies the same mapping so firmware carries over.

#ifdef PWRMAN_CONTROLLER_CARD
// hardware/controller, from the KiCad netlist (2026-09-09)
#define PIN_EXP_RST_N   1  // TCA9539 reset, via Q1 (2N7002, 100k gate pull-down)
#define PIN_LED_DATA    2  // WS2812C chain on backplane (6 pixels), via PIO
#define PIN_I2C_SDA     4  // I2C0 to backplane; 4.7k to 3.3 V on the card (R32)
#define PIN_I2C_SCL     5  // 4.7k to 3.3 V on the card (R41)
#define PIN_EXP_INT_N   6  // TCA9539 INT#: blade presence change (internal pull-up)
#define PIN_ALERT_N     7  // GLOBAL_ALERT#; 4.7k to 3.3 V on the card (R43)
#define PIN_MUX_RST_N   8  // TCA9548A reset, via Q2 (2N7002, 100k gate pull-down)
#define PIN_BUTTON      22 // front-panel button SW3 to GND (1k series, 100 nF), see button.h
#define PIN_VIN_SENSE   28 // ADC2: VIN through 120k / 10k (R13/R14), see vin.h
#define VIN_ADC_INPUT   2
// The reset lines are driven through N-channel FETs against the backplane's
// 10k pull-ups to 5 V: the GPIO drives a gate, so HIGH asserts the reset,
// and the RP2350's boot-time pad pull-down leaves both FETs off. A
// controller reboot therefore never resets the expander; the warm-start
// path (engine.c) relies on that.
#define RST_ASSERTED_LEVEL 1
#else
// Pico 2 W on the pcie-breakout: the breakout header's pins, push-pull
#define PIN_LED_DATA    2  // WS2812C chain on backplane (6 pixels), via PIO
#define PIN_ALERT_N     3  // GLOBAL_ALERT#: wire-OR of all blade ALERT# lines
#define PIN_I2C_SDA     4  // I2C0 to backplane (TCA9548A + TCA9539 upstream side)
#define PIN_I2C_SCL     5
#define PIN_EXP_INT_N   6  // TCA9539 INT#: blade presence change
#define PIN_MUX_RST_N   7  // TCA9548A hardware reset
#define PIN_EXP_RST_N   8  // TCA9539 hardware reset
#define PIN_BUTTON      22 // no button on the carrier; a wire from GP22 to GND acts as one (button.h)
// The carrier drives the reset lines directly: LOW asserts. While the Pico
// itself is in reset, its pad pull-down against the backplane's 10k pull-up
// should still leave the line above the expander's input-high threshold
// (per datasheet; verify at bring-up), so a controller reboot does not
// reset the expander here either.
#define RST_ASSERTED_LEVEL 0
#endif

#define I2C_BUS         i2c0
#define I2C_BAUD        (400 * 1000)

// Wired Ethernet: WIZnet W6100 on SPI0, the EVB-Pico2 mapping (NET_ETH)
#define PIN_ETH_MISO    16 // SPI0 RX
#define PIN_ETH_CS_N    17 // driven as a GPIO
#define PIN_ETH_SCK     18
#define PIN_ETH_MOSI    19 // SPI0 TX
#define PIN_ETH_RST_N   20
#define PIN_ETH_INT_N   21 // level-low while socket 0 has a frame waiting
#define ETH_SPI         spi0
#define ETH_SPI_HZ      (20 * 1000 * 1000)

// UPS serial link: a Mean Well LAD-xxxU supply on the controller card's UPS
// header (JST SH: TX, GND, RX), 3.3 V TTL through 1k series resistors, the
// LAD's fixed 9600 8N1. The cable crosses: our TX lands on the LAD's UART_RX
// (CN2 pin 13), its UART_TX (pin 14) on our RX. Free pins on the Pico 2 W
// carrier too, so the same image probes for a supply on every board.
#define PIN_UPS_TX      12 // UART0 TX
#define PIN_UPS_RX      13 // UART0 RX
#define UPS_UART        uart0
#define UPS_UART_IRQ    UART0_IRQ
#define UPS_BAUD        9600

// I2C addresses, verified against the backplane netlist (2026-08-30):
// TCA9548A A0=A1=A2=GND, TCA9539 A0=A1=GND. Blade parts sit behind the mux,
// one blade per channel, so identical per-blade addresses never conflict.
#define ADDR_TCA9548A   0x70
#define ADDR_TCA9539    0x74
#define ADDR_INA226     0x40  // per blade
#define ADDR_MPQ4242    0x61  // per blade

// TCA9548A channel N carries slot N+1 (SC0/SD0 = SCL1/SDA1); channels 6-7 spare.

// TCA9539 bit map (from backplane netlist, via 330R series networks RN5-RN8):
//   P00-P05 = EN1..EN6      output, active high (blades hold 100k pull-downs)
//   P06     = FAN           output, active high
//   P07     = tied to GND   keep as input
//   P10-P15 = PRES1#..PRES6# input, low = blade fully seated
//   P16,P17 = tied to GND   keep as input
#define TCA9539_EN_BIT(port)    (port)         // port 0-5 -> P00-P05
#define TCA9539_FAN_BIT         6              // P06
#define TCA9539_PRES_BIT(port)  (8 + (port))   // port 0-5 -> P10-P15
#define TCA9539_CONFIG_P0       0x80           // P07 input, P00-P06 outputs
#define TCA9539_CONFIG_P1       0xFF           // all of port 1 inputs
