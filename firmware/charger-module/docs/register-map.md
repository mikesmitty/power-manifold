# Blade register map and updates

The interface between the controller and a gen-3 blade: the register file the controller reads and writes, and the ROM-bootloader path the controller uses to program the blade's firmware. The controller side of updates is in [Flash layout and updates](../../controller/docs/flash-and-updates.md#blade-firmware-updates).

## Backplane register map

The blade is an I2C slave at **0x3A** on its slot's mux channel. `blade_regs.h`
is the reference; this is its summary. The controller's driver
(`firmware/controller/src/engine/blade3.c`) includes that header as-is, and
`PROTO` reads 2: the version both sides speak (2 added `BOOT`, `WATCH_S` and
the reset and boot-option commands).

A write's first byte sets the register pointer; reads and further writes
advance it. Multi-byte values are little-endian. A read sees one snapshot,
taken when the blade is addressed; configuration written in one transfer
takes effect together at its end.

| Reg | Name | Access | |
| --- | --- | --- | --- |
| 0x00 | WHO_AM_I | R | 0xB3 |
| 0x01 | PROTO | R | register map version |
| 0x02–0x04 | FW_MAJOR / MINOR / PATCH | R | |
| 0x05 | RESET_CAUSE | R | why the MCU last started |
| 0x06 | CAPS | R | bit 0: PPS |
| 0x07 | BOOT | R | bit 0: every reset lands in the ROM bootloader (see [Firmware updates](#firmware-updates-over-the-backplane)) |
| 0x10 | STATUS | R | attached, contract, PPS, VBUS on, 5 A cable, configured, fault, EN |
| 0x11 | PDO | R | object position of the contract |
| 0x12 | FAULT | R, u16 | latched until the clear command |
| 0x14 / 0x16 | CONTRACT_MV / CONTRACT_MA | R, u16 | |
| 0x18 / 0x19 | HR_SENT / HR_RECEIVED | R, u8 | PD hard resets since the MCU started, sent by the blade and by the sink, each wrapping at 256 |
| 0x20 / 0x22 / 0x24 | VBUS_MV / IOUT_MA / VOUT_MV | R, u16 | |
| 0x26 / 0x28 / 0x2A | TEMP_CONV / TEMP_PLUG / TEMP_MCU | R, i16 | 0.1 °C |
| 0x40 | CONTROL | R/W | bit 0: port enable |
| 0x41 | COMMAND | W | 1 re-send capabilities, 2 hard reset, 3 clear faults, 4 reset into the ROM bootloader, 5 program the boot option and reset. 4 and 5 take the port down, so each is accepted only when the next byte of the same transfer is its complement (0xFB, 0xFA). A single corrupted byte on the bus cannot trigger them |
| 0x42 | MAX_MA | R/W, u16 | current ceiling, stored clamped to 5000 |
| 0x44 | MAX_MV | R/W, u16 | voltage cap; 20000 and up = no cap, the 21 V PPS range included |
| 0x46 | WATCH_S | R/W, u8 | with EN low, seconds without a transaction from the controller before the blade resets itself into the bootloader; 0 = never (the reset state). With EN high the blade never resets on silence |

How it differs from the gen-2 blade, for the controller:

- The blade answers with EN low, so it can be configured before it is
  powered, and there is no wake delay to wait out.
- Out of reset the port is off and the limits are 0: a blade advertises
  nothing until the controller has configured it. A blade that restarted
  shows `CONFIGURED` clear and the `RESET` fault.
- Telemetry comes from the blade itself; there is no INA226.
- ALERT# is low while any fault but `RESET` is latched.
- A blank or erased blade runs the STM32 ROM bootloader, which answers at
  0x51 on the same pins; so does a blade set to boot through it, after
  every reset, until the controller starts the firmware. That is how the
  controller programs blades — the next section.

The advertised table is the gen-2 one: fixed 5, 9, 12, 15 and 20 V, PPS
3.3–11 V and 3.3–21 V. Every object's current is the lowest of `MAX_MA`, what
the cable carries (3 A unless its e-marker says 5 A) and what keeps the object
within 100 W, so the 21 V range stops at 4.75 A.

## Firmware updates over the backplane

The STM32G0's ROM bootloader speaks its I2C protocol (AN4221) on the same
pins as the register file, at 0x51, and the controller carries this
firmware's image inside its own, so blades are programmed in the chassis
with nothing but the backplane. The controller side is described in
[the controller docs](../../controller/docs/flash-and-updates.md#blade-firmware-updates); this is the
blade's part of it.

**The image.** `blade_image.h` is the contract. Right after the vector
table, at 0xC0, the linker places a 16-byte header — magic, version,
protocol, length — that the controller reads out of a blade sitting in the
bootloader to decide whether to start what is there or replace it. The
image carries no checksum: the controller holds the reference copy and asks
the bootloader for the CRC of the flash. `build/charger-module.bin` is the
image, and what the controller bundles.

**Getting to the bootloader.** A blank chip boots into it (the flash's
empty check), and so does a chip whose option bytes say so: `nBOOT_SEL=1,
nBOOT0=0` sends *every* reset — power-on, the watchdog, a crash, a software
reset — to the bootloader, where the port is dark and the controller can
reach the MCU whatever its firmware was doing. `BOOT` bit 0 reports the
option as loaded; `BLADE_CMD_BOOT_OPT` programs it (RM0444 3.4.2, from the
firmware, then an option-byte reload, which is a reset) and the controller
sends it once per blade when its setting says so. With factory option bytes
the chip boots the firmware directly, and `BLADE_CMD_RESET` still gets to
the bootloader by declaring the flash empty for the reset that follows
(RM0444 2.5.4); the firmware clears that again when it starts. This method
works for one reset only. The bootloader (V11.3 and later) clears the empty
flag itself, so a second reset before the image is complete boots a flash
with no first page, and the chip does not run until its next power-on
(AN2606, 48.3.1). The controller therefore sets the option first and
rewrites under it.

**What the bootloader is** (AN2606 Rev 70, section 48 and the timing
tables): ID 0xB4 at 0x1FFF6FFE for V11.4, the current one, speaking I2C
protocol V1.2, which has the no-stretch commands and the checksum the
controller uses. It answers within 25 µs of being addressed, 0.4 ms after
reset, and resets the chip when the host pauses for 1 s between two
frames of one command. It refreshes the independent watchdog only if the
option bytes start it in hardware; the firmware's own is not running
after a reset. V11.3 acknowledges an erase before it has finished (the
controller erases a page per command and waits 40 ms, ST's workaround) and
stretches the clock on the first I2C address match on packages without
PC11, which this one is; V11.2 and earlier predate the peripheral reset on
entry. The bootloader version is a single byte, read at bring-up.

**Coming back.** The bootloader's Go command starts the firmware in place.
`hw_init` resets every peripheral the bootloader configured, `SystemInit`
points the vector table at the flash (`USER_VECT_TAB_ADDRESS`), and the
`RESET` fault and `CONFIGURED` clear tell the controller it is looking at
a fresh start, as after any reset.

**The watch.** `WATCH_S` recovers a blade the controller cannot reach over
I2C because its firmware is running but no longer responding. The controller
takes the slot's EN low, and a blade that sees EN low and has not been
addressed for `WATCH_S` seconds resets into the bootloader, where it answers
again. The reset requires EN low, so it is always initiated by the
controller. Silence alone never resets a blade. A controller that is
restarting, updating itself or absent leaves EN unchanged, and a running
port keeps running on its last limits for the duration. The watch is off
until the controller sets it, so a blade on the bench is not reset.

**On the bench.** With the boot option programmed, `reset run` from a
debugger lands in the bootloader too, so a bring-up blade is easier kept on
factory option bytes until the controller side is in use. OpenOCD's
`stm32l4x` driver (which serves the G0) can flip the bit by hand:
`stm32l4x option_write 0 0x20 0x00000000 0x04000000` clears `nBOOT0`,
`... 0x04000000 0x04000000` sets it back, `stm32l4x option_load 0` applies
either. RM0444 warns that losing power during an option-byte write can
leave the chip locked; the firmware feeds the watchdog and disables
interrupts around its own write, and the controller only ever asks for it
once.
