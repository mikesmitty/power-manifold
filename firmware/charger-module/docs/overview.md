# Charger blade firmware

Firmware for the gen-3 charger blade (`hardware/charger-module` from release
0.15.0 on): an STM32G071C8 as the USB-PD source controller, in place of the
MPQ4242 that negotiated on its own in the gen-2 blade.

> [!CAUTION]
> **Written, not yet run**
>
> ST's USB-PD stack is integrated as the port's policy engine, with the
> blade's own supervisor, power path and backplane register file around it.
> The image builds and links; everything outside `src/hw/` and `src/usbpd/` is
> covered by the host tests, against a simulated board. None of it has run on
> hardware yet. Bench bring-up may reveal faults the simulation did not.

The source is in [`firmware/charger-module`](../). The backplane interface and the update path are on [Blade register map and updates](register-map.md).

## What the blade is

| Part | Role | Bus |
| --- | --- | --- |
| U1 STM32G071C8T6 | PD controller (UCPD1), supervisor, backplane I2C slave | |
| U3 TPS55288 | buck-boost converter, 0.8–21 V in 20 mV steps, current limit in 50 mA steps | private I2C, 0x74 |
| U5 TCPP02-M18 | CC switches and protection, VCONN switch, VBUS switch gate driver (Q3/Q4), over-current trip across R27, VBUS discharge, current monitor | private I2C, 0x34 |
| U4 TLV7022 | VBUS over-voltage comparators on TCPP_EN | |
| U2 LP2985A-33 | 3.3 V from the slot's 5 V, always on | |

The MCU runs from the slot's 5 V rail, so it runs and answers the
controller whenever the blade is seated. The slot's EN line is the
TPS55288's EN/UVLO pin: with EN low the converter is in shutdown whatever the
firmware does, and its registers return to their defaults.

### Protection that does not depend on the firmware

- **Over-voltage:** comparator half A trips at the DAC-set threshold
  (`ovp.h`, 1.2 × the contract voltage), half B at a fixed 23.0 V. Either
  pulls TCPP_EN low, which puts the TCPP02 in its OFF state and opens the
  VBUS switch.
- **Over-current:** the TCPP02 trips at 42 mV across R27 (7 mΩ), about 6 A,
  and latches until the firmware writes its control register again.
- **Reset state:** PA15 does not drive TCPP_EN and R9 holds it low; the DAC
  pin floats and R2 holds the threshold at 0 V. A blank, halted or crashed
  MCU leaves the switch open. The watchdog restarts a stuck one.

The firmware's share is the order of operations (`power.c`):

- the threshold covers the higher of the two voltages for as long as the
  output moves between them: up, the threshold goes first; down, it follows
  once VBUS has arrived;
- the converter's output is enabled only behind the closed VBUS switch, and
  ramps VBUS up from 0 V;
- the converter runs forced PWM while its output has to come down. The board
  straps it for PFM (R22 75k), where it blocks reverse current and cannot
  pull its own output down: an unloaded 20 V would stay 20 V. The mode
  register overrides the strap for the duration, in one write that keeps VCC
  on the slot's 5 V;
- after any fault TCPP_EN is taken low, and everything is set up afresh when
  the controller clears the fault.

## How the port runs

The port is **armed** while the slot's EN is high and the converter has
answered since it rose, the controller has enabled the port and given it a
current ceiling, and no fault that pulls ALERT# is latched. Otherwise it is
dark: the TCPP02 in hibernate with its CC switches open, so a sink sees no
source at all.

Armed, ST's stack detects the sink, reads the cable's e-marker, advertises
the table `pdo.c` builds from the controller's limits and the cable, and
hands each accepted request to the power path. New limits from the
controller reach an attached sink as fresh Source_Capabilities.

Any of these latches a fault, takes the port down and pulls ALERT#:

| Fault | Source |
| --- | --- |
| `OVP` | TCPP_EN went low against the drive, however briefly |
| `OCP_VBUS`, `OCP_VCONN`, `OVP_CC`, `OTP_PORT` | TCPP02 flags |
| `CONV_SCP`, `CONV_OVP` | TPS55288 status |
| `OT_CONV`, `OT_PLUG` | an NTC past its limit (100 °C / 70 °C) for 50 ms, or reading open or shorted |
| `BUS` | TPS55288 or TCPP02 not answering |
| `VBUS` | VBUS did not reach vSafe5V or vSafe0V in time |
| `PD` | the stack did not start |

Below the two temperature trips sit two warning levels, 85 °C at the
converter and 60 °C at the receptacle. When a reading stays at or above a
warning level for 50 ms, the blade sends the sink one PD Alert with the
over-temperature bit (PD 3 sinks only; PD 2 has no Alert message). Its
Get_Status reply then reports *Warning* with the over-temperature event
flag until the reading falls 5 °C below the level. It reports *Over
temperature* once a trip has latched, and *Normal* otherwise. The port
keeps running on a warning. The specification only requires the sink to
fetch Status in response. All four temperatures are provisional until
the blade has been measured in the closed chassis.

## Pin map

From the KiCad netlist of release 0.16.1; `src/board.h` carries the same
table with the component references.

| Pin | Net | Function |
| --- | --- | --- |
| PA0 | VBUS_SENSE | ADC_IN0, receptacle VBUS through 100k / 10k |
| PA1 | IANA | ADC_IN1, TCPP02 current monitor, 294 mV/A |
| PA2 / PA3 | USART_TX / USART_RX | USART2 console on test pads J1 / J6 |
| PA4 | OVP_REF | DAC_OUT1, tracking over-voltage threshold |
| PA8 / PB15 | UCPD_CC1 / UCPD_CC2 | UCPD1, through the TCPP02 |
| PA9, PA10 | GND | UCPD1 dead-battery pins, grounded for a source |
| PA13 / PA14 | SWDIO / SWCLK | Tag-Connect J2 |
| PA15 | TCPP_EN drive | through 4.7k; the comparators can pull the line low |
| PB0 / PB2 | NTC_CONV / NTC_PLUG | ADC_IN8 / ADC_IN10, 10k B3380 under 10k |
| PB1 | VOUT_SENSE | ADC_IN9, converter output through 100k / 10k |
| PB3 | LED | green, low = lit |
| PB4 | TCPP_EN sense | low while an over-voltage comparator holds the line |
| PB5 | ALERT# | slot B15, open drain |
| PB6 / PB7 | SCL / SDA | I2C1 slave, slot A14 / B14 |
| PB8 | EN | slot B16, input |
| PB10 / PB11 | USB_SCL / USB_SDA | I2C2 master, private bus |
| PB12 | CONVERTER_FLT | TPS55288 FB/INT, low = fault |
| PB13 | FLG# | TCPP02 FLGn, low = fault |

No crystal: HSI16 runs the core, and the UCPD is clocked from it by design.

## Source layout

Everything outside `src/hw/` and `src/usbpd/` is hardware-free and runs in
the host tests.

| File | What it holds |
| --- | --- |
| `include/blade_regs.h` | the backplane register map, shared with the controller's driver |
| `src/board.h` | pin map, addresses, analog scaling |
| `src/supervisor.c` | the blade's loop: EN, the controller's registers, faults, telemetry, arming |
| `src/port.c` | what the port offers and what it has agreed to |
| `src/power.c` | the power path and the order it is operated in |
| `src/pdo.c` | Source_Capabilities table from the limits; Request checking |
| `src/regmap.c` | the register file behind the I2C slave |
| `src/ovp.c` | over-voltage threshold and its DAC code |
| `src/sense.c` | ADC counts to mV, mA and degrees |
| `src/tps55288.c`, `src/tcpp02.c` | part drivers over `src/bus.h` |
| `src/usbpd/` | the glue ST's stack expects, under ST's file and function names: settings, policy callbacks, power interface, board power functions |
| `src/hw/` | pins, tick, watchdog, ADC, DAC, console, both I2C peripherals; register level on the CMSIS device header |
| `src/main.c` | start-up; hands the main loop to the stack |

### ST's stack

Fetched at configure time, none of it kept in this repository
(`cmake/st_usbpd.cmake`), at the versions X-CUBE-TCPP 4.2.0 ships together:

| Component | Version | Licence |
| --- | --- | --- |
| `stm32-mw-usbpd-core`, the policy engine and protocol layer, a binary library | 5.3.0, `PD3_CONFIG_SPR` | SLA0044: ST microcontrollers only |
| `stm32-mw-usbpd-device-g0`, the UCPD device layer | 3.5.2 | SLA0044 |
| `stm32g0xx_hal_driver`, of which only the LL drivers are built | 1.4.5 | BSD-3-Clause |

`PD3_CONFIG_SPR` is the smallest build of the core with both PPS and the
structured VDMs that read a cable's e-marker. The stack runs without an RTOS:
`USBPD_DPM_Run` is the main loop and calls the supervisor once per turn.
It uses UCPD1, DMA1 channels 2 and 4, and TIM1.

The stack identifies itself with vendor ID 0xFFFF, the value the
specification gives to vendors without one from the USB-IF.

A built image contains ST's library. SLA0044 allows passing it on in binary
form for ST microcontrollers, with ST's copyright notice, conditions and
disclaimer in what accompanies it.

## Building

Needs CMake 3.18, Ninja and the GNU Arm toolchain (`arm-none-eabi-gcc`) on
the path, and network access at configure time for the CMSIS headers and
ST's stack.

```sh
cmake -S . -B build -G Ninja
ninja -C build
```

produces `build/charger-module.elf`, `.bin` and `.hex`: 44 KB of the 64 KB
of flash. `-DBLADE_WATCHDOG=OFF` builds without the independent watchdog.
The controller's build picks `build/charger-module.bin` up from here as the
image it bundles (its `BLADE_IMAGE` option).

### Host tests

```sh
cmake -S test -B test/build && cmake --build test/build
ctest --test-dir test/build --output-on-failure
```

## Flashing

SWD on the Tag-Connect footprint J2 (TC2030: 1 = 3.3 V, 2 = SWDIO, 3 = NRST,
4 = SWCLK, 5 = GND), with any CMSIS-DAP probe. The blade needs its 5 V: seat
it in a powered backplane, or feed 5 V to the slot's 5 V fingers.

```sh
openocd -f interface/cmsis-dap.cfg -f target/stm32g0x.cfg \
    -c "program build/charger-module.elf verify reset exit"
```

A factory-fresh chip starts its ROM bootloader until it has been programmed
and power-cycled once — or, seated in a chassis whose controller carries an
image, until the controller programs it over the backplane.

## Console

USART2 on the test pads J1 (TX) and J6 (RX), 115200 8N1, 3.3 V. Transmit
only. One line a second, and a line for every change of EN and of the
controller's configuration:

```
en=1 armed=1 st=8b vdda=3301 vout=9012 vbus=8987 iout=1480 contract=9000/2000 conv=41.8 plug=33.1 fault=0000
```

## Bring-up checklist

In this order. Steps 1 to 4 need no sink.

1. **5 V only, EN low.** The LED flashes once a second and the console
   prints the banner and the report line: `vdda` within a few percent of
   3300, both NTCs near room temperature, `fault=8000` (the restart).
2. **The controller, or an I2C adapter on the slot's SDA/SCL,** reads 0xB3
   from register 0x00 at address 0x3A.
3. **Bus voltage applied and EN high:** `EN high`, and no `BUS` fault 50 ms
   later. `vout` stays at 0.
4. **Writing `MAX_MA`, `MAX_MV` and `CONTROL`** prints the configuration;
   `armed=1` follows. CC1 and CC2 at the receptacle now carry the 3 A pull-up.
5. **A sink emulator, 5 V only at first.** Watch VBUS on a scope from
   attach: a ramp from 0 V, no step. Then each fixed voltage up and down,
   unloaded, which is the case that needs forced PWM to come down in time.
6. **PPS,** across both ranges, and into current limit.
7. **An e-marked 5 A cable:** `st` gains the 5 A bit and the 20 V object
   offers 5 A.
8. **The trips,** with a bench supply on the converter output through a
   diode: the comparator at 1.2 × the contract, the fixed one at 23 V, and
   the time from either to the switch opening, which the TCPP02's datasheet
   does not give.

## What comes next

1. **Bring-up** on the first gen-3 blade, per the checklist — the update
   path included: the ROM bootloader's answer on the backplane bus, a
   controller-programmed blank blade, and the option-byte trip.
2. **Alert and Status at a sink.** Written and host-tested, not yet tested
   against a real sink: a PD analyser or a logging sink should show one
   Alert with the over-temperature bit when a thermometer passes its warning
   level, and Status answering *Warning*. The four temperatures (85 / 60 °C
   warn, 100 / 70 °C trip) are set from the same thermocouple session as the
   controller's fan rule.
