#pragma once

// Backplane register map of the gen-3 charger blade (STM32G071 + TPS55288 +
// TCPP02-M18). Shared by the blade firmware and the controller's driver, so
// it includes nothing but this file's own constants.
//
// The blade is an I2C slave on its slot's mux channel. A write's first byte
// sets the register pointer, the rest are data; a read returns bytes from the
// pointer on. The pointer advances per byte. Multi-byte values are
// little-endian and consistent within one transfer: reads come from a
// snapshot taken when the blade is addressed, configuration writes take
// effect together at the end of the transfer. Unlisted registers read 0 and
// ignore writes.
//
// The MCU runs from the slot's 5 V, so the blade answers with EN low: the
// controller configures it first and raises EN afterwards. Out of reset the
// port is off (BLADE_CTL_PORT_EN clear, limits 0) until it has been
// configured.

// Clear of everything else the controller can see through the mux (0x70 mux,
// 0x74 expander, 0x40 INA226 / 0x61 MPQ4242 on a gen-2 blade) and of the
// STM32G0 ROM bootloader, which is what a blank blade answers as, and what a
// blade set up to boot through it (BLADE_BOOT_VIA_LOADER) answers as after
// every reset until the controller starts its firmware (blade_image.h).
#define BLADE_I2C_ADDR        0x3A
#define BLADE_LOADER_I2C_ADDR 0x51

#define BLADE_WHO_AM_I        0xB3
// The map below, as both the blade firmware and the controller's driver
// (firmware/controller/src/engine/blade3.c) speak it. Bumped when a change
// would mislead an older controller.
#define BLADE_PROTO_VERSION   2

// identity, read-only
#define BLADE_REG_WHO_AM_I    0x00
#define BLADE_REG_PROTO       0x01
#define BLADE_REG_FW_MAJOR    0x02
#define BLADE_REG_FW_MINOR    0x03
#define BLADE_REG_FW_PATCH    0x04
#define BLADE_REG_RESET_CAUSE 0x05 // BLADE_RESET_*, why the MCU last started
#define BLADE_REG_CAPS        0x06 // BLADE_CAP_*
#define BLADE_REG_BOOT        0x07 // BLADE_BOOT_*, how the MCU boots (PROTO 2)

// status, read-only
#define BLADE_REG_STATUS      0x10 // BLADE_ST_*
#define BLADE_REG_PDO         0x11 // object position of the contract, 0 = none
#define BLADE_REG_FAULT       0x12 // u16, BLADE_FAULT_*, latched until BLADE_CMD_CLEAR_FAULTS
#define BLADE_REG_CONTRACT_MV 0x14 // u16
#define BLADE_REG_CONTRACT_MA 0x16 // u16, the operating current the sink asked for

// telemetry, read-only
#define BLADE_REG_VBUS_MV     0x20 // u16, at the receptacle
#define BLADE_REG_IOUT_MA     0x22 // u16, port current (TCPP02 IANA)
#define BLADE_REG_VOUT_MV     0x24 // u16, converter output ahead of the VBUS switch
#define BLADE_REG_TEMP_CONV   0x26 // i16, 0.1 degC, NTC at the converter
#define BLADE_REG_TEMP_PLUG   0x28 // i16, 0.1 degC, NTC at the receptacle
#define BLADE_REG_TEMP_MCU    0x2A // i16, 0.1 degC

// configuration, read/write
#define BLADE_REG_CONTROL     0x40 // BLADE_CTL_*
#define BLADE_REG_COMMAND     0x41 // BLADE_CMD_*, write-only, reads 0
#define BLADE_REG_MAX_MA      0x42 // u16, current field of every advertised PDO
#define BLADE_REG_MAX_MV      0x44 // u16, voltage cap, see BLADE_MAX_MV_ALL
#define BLADE_REG_WATCH_S     0x46 // u8, seconds without the controller before the blade resets itself, 0 = never (PROTO 2)

#define BLADE_REG_END         0x47 // one past the last register

#define BLADE_RESET_POWER     (1u << 0) // power-on / brown-out
#define BLADE_RESET_PIN       (1u << 1) // NRST
#define BLADE_RESET_WATCHDOG  (1u << 2)
#define BLADE_RESET_SOFTWARE  (1u << 3)
#define BLADE_RESET_OTHER     (1u << 4) // option-byte load, low-power, window watchdog

#define BLADE_CAP_PPS         (1u << 0)

// Every reset lands in the ROM bootloader (option bytes nBOOT_SEL=1,
// nBOOT0=0): the firmware only runs once the controller has checked it and
// started it. Clear on a chip with factory option bytes, which boots its
// firmware directly; BLADE_CMD_BOOT_OPT sets it.
#define BLADE_BOOT_VIA_LOADER (1u << 0)

#define BLADE_ST_ATTACHED     (1u << 0) // a sink is on the port
#define BLADE_ST_CONTRACT     (1u << 1) // explicit PD contract in place
#define BLADE_ST_PPS          (1u << 2) // ...and it is a PPS one
#define BLADE_ST_VBUS_ON      (1u << 3) // VBUS switch closed
#define BLADE_ST_CABLE_5A     (1u << 4) // the cable's e-marker allows 5 A
#define BLADE_ST_CONFIGURED   (1u << 5) // configuration written since the MCU started
#define BLADE_ST_FAULT        (1u << 6) // BLADE_REG_FAULT is non-zero
#define BLADE_ST_EN           (1u << 7) // the slot's EN line as the blade sees it

#define BLADE_FAULT_OVP        (1u << 0)  // VBUS over-voltage comparator opened the switch
#define BLADE_FAULT_OCP_VBUS   (1u << 1)  // TCPP02 VBUS over-current latch
#define BLADE_FAULT_OCP_VCONN  (1u << 2)  // TCPP02 VCONN over-current
#define BLADE_FAULT_OVP_CC     (1u << 3)  // TCPP02 CC line over-voltage
#define BLADE_FAULT_OTP_PORT   (1u << 4)  // TCPP02 over-temperature
#define BLADE_FAULT_CONV_SCP   (1u << 5)  // TPS55288 output short
#define BLADE_FAULT_CONV_OCP   (1u << 6)  // TPS55288 current limit, outside a PPS contract
#define BLADE_FAULT_CONV_OVP   (1u << 7)  // TPS55288 output over-voltage
#define BLADE_FAULT_OT_CONV    (1u << 8)  // converter NTC over its limit
#define BLADE_FAULT_OT_PLUG    (1u << 9)  // receptacle NTC over its limit
#define BLADE_FAULT_BUS        (1u << 10) // TPS55288 or TCPP02 stopped answering
#define BLADE_FAULT_VBUS       (1u << 11) // VBUS outside the window for its state
#define BLADE_FAULT_PD         (1u << 12) // PD recovery exhausted
#define BLADE_FAULT_RESET      (1u << 15) // the MCU restarted; cause in BLADE_REG_RESET_CAUSE

// ALERT# is low while any of these is latched. A restart is not among them:
// it costs the port its contract but needs nobody's urgent attention, and the
// controller sees it as BLADE_ST_CONFIGURED gone.
#define BLADE_FAULT_ALERTING   ((uint16_t)~BLADE_FAULT_RESET)

#define BLADE_CTL_PORT_EN     (1u << 0) // present Rp and advertise (EN must be high as well)

#define BLADE_CMD_SRC_CAP      1 // re-send Source_Capabilities
#define BLADE_CMD_HARD_RESET   2 // PD hard reset
#define BLADE_CMD_CLEAR_FAULTS 3
// The port goes dark and the MCU resets into the ROM bootloader, whatever
// the option bytes say (PROTO 2). The controller programs or starts it from
// there.
#define BLADE_CMD_RESET        4
// Program the option bytes for BLADE_BOOT_VIA_LOADER and reset (PROTO 2).
// Once per chip; a no-op reset when they are already so.
#define BLADE_CMD_BOOT_OPT     5

// Limits of the blade hardware; values written past them are stored clamped.
#define BLADE_MAX_MA_LIMIT    5000
// Voltage cap: fixed PDOs above it and PPS ranges reaching above it are
// withheld, 5 V always stays.
//
// 20 V is the exception. Taken literally, a 20 V cap would withhold the
// 3.3 - 21 V PPS range, which reaches 1 V past it. Instead 20000 or more
// means no cap at all: the whole table is advertised, the 21 V range
// included. The controller's PORT_VOLT_MAX_MV works the same way.
#define BLADE_MAX_MV_ALL      20000
