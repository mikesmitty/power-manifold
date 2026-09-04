// WIZnet W6100-EVB-Pico2: RP2350A + W6100 on SPI0 (GP16-21), 2 MB flash.
// The pico-sdk (2.2.0) ships headers for the W5100S variants only; this one
// follows wiznet_w5100s_evb_pico2.h with the W6100 wiring. Select it with
//   -DPICO_BOARD=wiznet_w6100_evb_pico2  (CMakeLists adds this directory to
// PICO_BOARD_HEADER_DIRS). The controller's own pin map (src/pins.h) already
// matches these pins; the point of the header is the flash size, which puts
// the settings/fault-log sectors inside the chip.

#ifndef _BOARDS_WIZNET_W6100_EVB_PICO2_H
#define _BOARDS_WIZNET_W6100_EVB_PICO2_H

// pico_cmake_set PICO_PLATFORM=rp2350
pico_board_cmake_set(PICO_PLATFORM, rp2350)

// For board detection
#define WIZNET_W6100_EVB_PICO2

// --- RP2350 VARIANT ---
#define PICO_RP2350A 1

// --- W6100 wiring (also SPI0's defaults below) ---
#ifndef W6100_EVB_PICO2_INTN_PIN
#define W6100_EVB_PICO2_INTN_PIN 21
#endif
#ifndef W6100_EVB_PICO2_RSTN_PIN
#define W6100_EVB_PICO2_RSTN_PIN 20
#endif

// --- UART ---
#ifndef PICO_DEFAULT_UART
#define PICO_DEFAULT_UART 0
#endif
#ifndef PICO_DEFAULT_UART_TX_PIN
#define PICO_DEFAULT_UART_TX_PIN 0
#endif
#ifndef PICO_DEFAULT_UART_RX_PIN
#define PICO_DEFAULT_UART_RX_PIN 1
#endif

// --- LED ---
#ifndef PICO_DEFAULT_LED_PIN
#define PICO_DEFAULT_LED_PIN 25
#endif

// --- I2C ---
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
#define PICO_BOOT_STAGE2_CHOOSE_W25Q080 1

#ifndef PICO_FLASH_SPI_CLKDIV
#define PICO_FLASH_SPI_CLKDIV 2
#endif

// pico_cmake_set_default PICO_FLASH_SIZE_BYTES = (2 * 1024 * 1024)
pico_board_cmake_set_default(PICO_FLASH_SIZE_BYTES, (2 * 1024 * 1024))
#ifndef PICO_FLASH_SIZE_BYTES
#define PICO_FLASH_SIZE_BYTES (2 * 1024 * 1024)
#endif

#define PICO_SMPS_MODE_PIN 23

#ifndef PICO_VBUS_PIN
#define PICO_VBUS_PIN 24
#endif
#ifndef PICO_VSYS_PIN
#define PICO_VSYS_PIN 29
#endif

// pico_cmake_set_default PICO_RP2350_A2_SUPPORTED = 1
pico_board_cmake_set_default(PICO_RP2350_A2_SUPPORTED, 1)
#ifndef PICO_RP2350_A2_SUPPORTED
#define PICO_RP2350_A2_SUPPORTED 1
#endif

#endif
