#pragma once

// Gen-3 charger blade, hardware/charger-module. Everything here is from the
// KiCad netlist of release 0.16.1 (exported 2026-09-29); pin functions from
// DS12232 Rev 5.
//
// U1 STM32G071C8T6 (LQFP48), no crystal: HSI16 runs the core and the UCPD.
//
//   PA0  VBUS_SENSE     ADC_IN0   receptacle VBUS, R19 100k / R20 10k, C19 10 nF
//   PA1  IANA           ADC_IN1   TCPP02 current monitor, 42 V/V across R27 7 mOhm
//   PA2  USART_TX       USART2    test pad J1 (also the ROM bootloader's USART2)
//   PA3  USART_RX       USART2    test pad J6
//   PA4  OVP_REF        DAC_OUT1  tracking OVP threshold into U4 INA+, R2 100k to GND
//   PA8  UCPD_CC1       UCPD1     through the TCPP02 CC switch
//   PA9  GND                      UCPD1_DBCC1, grounded: source-only port
//   PA10 GND                      UCPD1_DBCC2, grounded
//   PA13 SWDIO                    J2 Tag-Connect pin 2
//   PA14 SWCLK                    J2 pin 4
//   PA15 TCPP_EN drive            through R8 4.7k; R9 100k holds the line low
//   PB0  NTC_CONV       ADC_IN8   TH1 10k B3380 to GND, R12 10k to 3.3 V
//   PB1  VOUT_SENSE     ADC_IN9   converter output, R17 100k / R18 10k, C18 1 nF
//   PB2  NTC_PLUG       ADC_IN10  TH2, R16 10k to 3.3 V
//   PB3  LED            D1 green, anode on +5 V through R7 1k: low = lit
//   PB4  TCPP_EN sense            the line itself: low while an OVP comparator holds it
//   PB5  ALERT#                   J3 B15, open drain, pulled up by the controller
//   PB6  SCL            I2C1      J3 A14, backplane segment, R4 4.7k to 3.3 V
//   PB7  SDA            I2C1      J3 B14, R3 4.7k to 3.3 V
//   PB8  EN                       J3 B16, also TPS55288 EN/UVLO; R1 100k to GND
//   PB10 USB_SCL        I2C2      private bus: TPS55288 0x74, TCPP02 0x34; R6 4.7k
//   PB11 USB_SDA        I2C2      R5 4.7k
//   PB12 CONVERTER_FLT            TPS55288 FB/INT, low = fault, R10 100k to 3.3 V
//   PB13 FLG#                     TCPP02 FLGn, low = fault, R13 47k to 3.3 V
//   PB15 UCPD_CC2       UCPD1
//
// Every other pin is unconnected and stays in its reset state (analog).

// GPIOA
#define PIN_VBUS_SENSE      0
#define PIN_IANA            1
#define PIN_USART_TX        2
#define PIN_USART_RX        3
#define PIN_OVP_REF         4
#define PIN_TCPP_EN_DRV     15
// GPIOB
#define PIN_NTC_CONV        0
#define PIN_VOUT_SENSE      1
#define PIN_NTC_PLUG        2
#define PIN_LED             3
#define PIN_TCPP_EN_SENSE   4
#define PIN_ALERT_N         5
#define PIN_SCL             6
#define PIN_SDA             7
#define PIN_EN              8
#define PIN_USB_SCL         10
#define PIN_USB_SDA         11
#define PIN_CONVERTER_FLT   12
#define PIN_FLG_N           13

#define AF_USART2           1 // PA2, PA3
#define AF_I2C              6 // PB6/PB7 I2C1, PB10/PB11 I2C2

#define ADC_CH_VBUS         0
#define ADC_CH_IANA         1
#define ADC_CH_NTC_CONV     8
#define ADC_CH_VOUT         9
#define ADC_CH_NTC_PLUG     10
#define ADC_CH_TEMP         12 // internal temperature sensor
#define ADC_CH_VREFINT      13

// Private bus. The TPS55288's MODE pin carries R22 75k: external VCC, 0x74,
// PFM. The TCPP02's I2C_ADD pin is grounded.
#define ADDR_TPS55288       0x74
#define ADDR_TCPP02         0x34

// Analog scaling
#define SENSE_DIV_NUM       11      // (100k + 10k) / 10k, VBUS_SENSE and VOUT_SENSE
#define IANA_UV_PER_MA      294     // 42 V/V x 7 mOhm (R27)
#define CONV_SHUNT_UOHM     10000   // R28, TPS55288 ISP/ISN
#define NTC_PULLUP_OHM      10000   // R12, R16

// VBUS over-voltage comparator U4 (TLV7022), both halves open drain on
// TCPP_EN. Half A trips at OVP_REF x 11, set by the DAC; half B is the fixed
// ceiling from R25 57.6k / R26 100k off the 3.3 V rail: 2.094 V x 11 = 23.0 V,
// under the TCPP02's 24 V absolute maximum.
#define OVP_CEILING_MV      23000
