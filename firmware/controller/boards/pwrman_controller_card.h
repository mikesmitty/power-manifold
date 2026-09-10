// power-manifold controller card (hardware/controller): RP2350A with a
// W25Q128 (16 MB) flash, a WIZnet W6100-L on SPI0 GP16-21, a Raspberry Pi
// RM2 radio module on the Pico 2 W's CYW43 pin map, and a PCIe x1 card edge
// into the backplane's management slot. Select it with
//   -DPICO_BOARD=pwrman_controller_card
// (CMakeLists adds this directory to PICO_BOARD_HEADER_DIRS, and picks the
// 16 MB partition layout for it). The backplane signals sit on different
// GPIOs than on the Pico 2 W carrier and the two reset lines are driven
// through 2N7002 FETs, so src/pins.h keys its map off PWRMAN_CONTROLLER_CARD.
// Pins verified against the KiCad netlist, 2026-09-09.

#ifndef _BOARDS_PWRMAN_CONTROLLER_CARD_H
#define _BOARDS_PWRMAN_CONTROLLER_CARD_H

// pico_cmake_set PICO_PLATFORM=rp2350
pico_board_cmake_set(PICO_PLATFORM, rp2350)
// pico_cmake_set PICO_CYW43_SUPPORTED = 1
pico_board_cmake_set(PICO_CYW43_SUPPORTED, 1)

// For board detection
#define PWRMAN_CONTROLLER_CARD

// --- RP2350 VARIANT ---
#define PICO_RP2350A 1

// --- UART ---
// The only UART header is the UPS link (J5, JST SH: TX, GND, RX). Console
// output goes over USB (and RTT); this is where stdio_uart would land if it
// were ever enabled.
#ifndef PICO_DEFAULT_UART
#define PICO_DEFAULT_UART 0
#endif
#ifndef PICO_DEFAULT_UART_TX_PIN
#define PICO_DEFAULT_UART_TX_PIN 12
#endif
#ifndef PICO_DEFAULT_UART_RX_PIN
#define PICO_DEFAULT_UART_RX_PIN 13
#endif

// --- LED ---
// no PICO_DEFAULT_LED_PIN: the status LEDs are the backplane's WS2812 chain

// --- I2C (backplane, TCA9548A + TCA9539 upstream side) ---
#ifndef PICO_DEFAULT_I2C
#define PICO_DEFAULT_I2C 0
#endif
#ifndef PICO_DEFAULT_I2C_SDA_PIN
#define PICO_DEFAULT_I2C_SDA_PIN 4
#endif
#ifndef PICO_DEFAULT_I2C_SCL_PIN
#define PICO_DEFAULT_I2C_SCL_PIN 5
#endif

// --- SPI (the W6100) ---
#ifndef PICO_DEFAULT_SPI
#define PICO_DEFAULT_SPI 0
#endif
#ifndef PICO_DEFAULT_SPI_SCK_PIN
#define PICO_DEFAULT_SPI_SCK_PIN 18
#endif
#ifndef PICO_DEFAULT_SPI_TX_PIN
#define PICO_DEFAULT_SPI_TX_PIN 19
#endif
#ifndef PICO_DEFAULT_SPI_RX_PIN
#define PICO_DEFAULT_SPI_RX_PIN 16
#endif
#ifndef PICO_DEFAULT_SPI_CSN_PIN
#define PICO_DEFAULT_SPI_CSN_PIN 17
#endif

// --- FLASH ---
// W25Q128JVSIQ on the QSPI pins with the dedicated chip select. The generic
// Winbond second stage the Pico boards use (W25Q080 in name only) drives the
// whole W25Q family's quad read.
#define PICO_BOOT_STAGE2_CHOOSE_W25Q080 1

#ifndef PICO_FLASH_SPI_CLKDIV
#define PICO_FLASH_SPI_CLKDIV 2
#endif

// pico_cmake_set_default PICO_FLASH_SIZE_BYTES = (16 * 1024 * 1024)
pico_board_cmake_set_default(PICO_FLASH_SIZE_BYTES, (16 * 1024 * 1024))
#ifndef PICO_FLASH_SIZE_BYTES
#define PICO_FLASH_SIZE_BYTES (16 * 1024 * 1024)
#endif

// No PICO_VBUS_PIN / PICO_VSYS_PIN: USB VBUS is tied to the 5 V rail and GP29
// is the radio's clock, as on the Pico 2 W.

// pico_cmake_set_default PICO_RP2350_A2_SUPPORTED = 1
pico_board_cmake_set_default(PICO_RP2350_A2_SUPPORTED, 1)
#ifndef PICO_RP2350_A2_SUPPORTED
#define PICO_RP2350_A2_SUPPORTED 1
#endif

// --- RM2 radio (CYW43439), the Pico 2 W wiring ---
// cyw43 SPI pins can't be changed at runtime
#ifndef CYW43_PIN_WL_DYNAMIC
#define CYW43_PIN_WL_DYNAMIC 0
#endif

// gpio pin to power up the cyw43 chip (WL_ON and BT_ON tied together)
#ifndef CYW43_DEFAULT_PIN_WL_REG_ON
#define CYW43_DEFAULT_PIN_WL_REG_ON 23u
#endif

// gpio pin for spi data out to the cyw43 chip
#ifndef CYW43_DEFAULT_PIN_WL_DATA_OUT
#define CYW43_DEFAULT_PIN_WL_DATA_OUT 24u
#endif

// gpio pin for spi data in from the cyw43 chip
#ifndef CYW43_DEFAULT_PIN_WL_DATA_IN
#define CYW43_DEFAULT_PIN_WL_DATA_IN 24u
#endif

// gpio (irq) pin for the irq line from the cyw43 chip
#ifndef CYW43_DEFAULT_PIN_WL_HOST_WAKE
#define CYW43_DEFAULT_PIN_WL_HOST_WAKE 24u
#endif

// gpio pin for the spi clock line to the cyw43 chip
#ifndef CYW43_DEFAULT_PIN_WL_CLOCK
#define CYW43_DEFAULT_PIN_WL_CLOCK 29u
#endif

// gpio pin for the spi chip select to the cyw43 chip
#ifndef CYW43_DEFAULT_PIN_WL_CS
#define CYW43_DEFAULT_PIN_WL_CS 25u
#endif

#endif
