#pragma once

// Controller GPIO map (Pico 2 W on the pcie-breakout / backplane J11 socket).
//
// GP16-GP21 are RESERVED for the wired-Ethernet path. WIZnet's EVB-Pico2
// boards put the Ethernet chip on SPI0 GP16-19 with RSTn=GP20 / INTn=GP21, so
// a W6100-EVB-Pico2 drops into the dev socket unchanged, and the production
// board (custom RP2350 + W6100, possibly with an RM2 radio module) should
// copy the same mapping so firmware carries over.

#define PIN_LED_DATA    2  // WS2812C chain on backplane (6 pixels), via PIO
#define PIN_ALERT_N     3  // GLOBAL_ALERT#: wire-OR of all blade ALERT# lines
#define PIN_I2C_SDA     4  // I2C0 to backplane (TCA9548A + TCA9539 upstream side)
#define PIN_I2C_SCL     5
#define PIN_EXP_INT_N   6  // TCA9539 INT#: blade presence change
#define PIN_MUX_RST_N   7  // TCA9548A hardware reset
#define PIN_EXP_RST_N   8  // TCA9539 hardware reset

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
