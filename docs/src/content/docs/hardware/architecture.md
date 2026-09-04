---
title: Architecture specification (V2)
description: As-built architecture of the six-blade managed USB-PD chassis, its interconnects, and the supervisory firmware model.
---

This technical specification details the architecture for a six-port managed USB Power Delivery (USB-PD) power supply. The design consolidates control from distributed per-blade microcontrollers into a centralized Raspberry Pi RP2350 management controller. The architecture incorporates localized I2C current/voltage sensing on each blade, centralized I2C channel multiplexing, backplane-mounted status LEDs with front-panel light pipes, a dedicated card-edge management slot, and a dual-core firmware supervisory state machine with dynamic, priority-aware power budgeting.

*Updated 2026-08-30 to match the as-built hardware (backplane v0.8.x, charger module v0.13.x, pcie-breakout) and the implemented controller firmware (`firmware/controller`). Early drafts described an 8-blade chassis; the built V2 chassis carries six charger blades plus the management slot.*

## 1. Executive Summary & Architectural Overview

The original architecture utilized an RP2040 microcontroller, dedicated QSPI flash memory, crystal oscillator, boot/reset buttons, and a NeoPixel on each individual charger blade. While functional, this introduced firmware maintenance overhead, higher per-port BOM costs, and severe signal integrity challenges when measuring analog sense signals across long PCB traces.
The consolidated V2 architecture decouples power delivery from supervisory management:

> * **Digitization at the Point of Load:** A dedicated digital current/power monitor (INA226) on each blade measures shunt voltage and bus voltage directly via Kelvin connections, transmitting telemetry digitally over I2C.
> * **Backplane Channel Isolation:** An 8-channel I2C switch (TCA9548A) on the backplane isolates capacitive trace loading and eliminates I2C address conflicts among identical MPQ4242 buck-boost controllers. Six channels serve the blades; two are spare.
> * **Centralized Intelligence:** A single dual-core RP2350 management controller supervises all six ports, enforces dynamic chassis power budgeting, and drives the status LEDs. During development the controller is a Raspberry Pi Pico 2 W (WiFi) seated in the management slot via a breakout carrier; the production controller is a custom RP2350 board with a WIZnet W6100 wired-Ethernet controller.
> * **Backplane LED Array & Light Pipes:** Moving the status LEDs to the backplane converts per-port LED data lines into a single serial daisy chain, freeing card-edge pins and eliminating phantom-power risks during blade insertion.

## 2. Component Selection (As Built)

| Subsystem / Function | IC / Part | Location | Key Features & Justification |
| :---- | :---- | :---- | :---- |
| **Buck-Boost Controller** | MPS MPQ4242 (GVE-0000, all-OTP-defaults variant) | Charger Blade (x6) | Integrated 4-switch buck-boost with I2C programmability, 100W PD output capability, programmable current limit and VOUT. Negotiates PD autonomously; the controller constrains its advertised PDO set. |
| **Power & Current Monitor** | TI INA226 | Charger Blade (x6) | 16-bit ΔΣ digital power monitor with hardware alert thresholding and built-in averaging. Shares the blade's 10 mΩ shunt via Kelvin taps. |
| **I2C Multiplexer** | TI TCA9548A | Backplane | 8-channel bidirectional I2C switch with hardware reset (MUX_RST#). Isolates bus capacitance and allows identical I2C addresses on all blades. Channel N carries slot N+1; channels 6–7 spare. 4.7 kΩ pull-up pairs per downstream segment. |
| **GPIO Expander** | TI TCA9539 | Backplane | 16-bit I2C GPIO expander with interrupt output and hardware reset (EXP_RST#). P00–P05 = blade EN1–6 (active high), P06 = fan switch, P10–P15 = PRES#1–6 (active low); P07/P16/P17 grounded spares. All pins through 330 Ω series networks. |
| **Main Supervisor MCU** | Raspberry Pi RP2350 | Management slot | Dual Cortex-M33 @ 150 MHz, 520 KB SRAM, PIO state machines. Dev vehicle: Pico 2 W (adds CYW43439 WiFi/BT) on the pcie-breakout carrier. |
| **Ethernet Controller** | WIZnet W6100 | Production controller (planned) | Hardwired dual-stack IPv4/IPv6 TCP/IP + 10/100 MAC/PHY over SPI. Wired successor to the Pico 2 W's WiFi; SPI0 GP16–19 + RSTn GP20 + INTn GP21, matching WIZnet's EVB-Pico2 boards so firmware carries over unchanged. |
| **Radio Module** | Raspberry Pi RM2 (optional) | Production controller (planned) | CYW43439 — the same silicon as the Pico 2 W — so the WiFi stack and a future BLE provisioning path carry over to the custom board. |
| **Port Status Indicator** | WS2812C-2020-V6 | Backplane (x6) | Addressable RGB LEDs in a single serial chain (220 Ω series into the first pixel), paired with front-panel rigid light pipes. The V6 variant runs from the 3.3 V rail — the backplane has no 5 V rail. |
| **Backplane Power Path** | LM74700 + IRFS7530, 20 A fuse, SMCJ33A | Backplane | Ideal-diode reverse-polarity protection at the DC input (Micro-Fit 3.0 2x3 connector), fused at 20 A, TVS-clamped (§8.4). |
| **Logic Rail** | TI TPS5430 | Backplane | VIN → 3.3 V buck feeding the backplane ICs, the LED chain, and the management slot's 3.3 V fingers. |
| **Fan Switch** | AO3400A | Backplane | Low-side MOSFET driven from expander P06 (100 kΩ gate pull-down, SS34 flyback across the fan connector). |

## 3. PCIe x1 Card-Edge Interconnect Pinout (Charger Blades 1–6)

The standard 36-pin PCIe x1 card-edge form factor provides high current capacity, mechanical key isolation, and staggered presence-detection pins (a seating check — the system is not serviced live; see §8.5). The layout places high-current DC rails (Pins 1–11) and low-voltage digital signals (Pins 12–18) on opposite sides of the mechanical key notch.

| Pin | Side A (Component Side) | Side B (Solder Side) | Pin | Functional Description & Electrical Requirements |
| :---: | :---- | :---- | :---: | :---- |
| **A1** | **PRSNT1#** *(Short Pin)* | **PGND** | **B1** | Presence sense loop lead (A1, short pin; see §8.5). B1 joins the main power return and serves as the pin-1-end sacrificial ground. |
| **A2** | **VIN** | **PGND** | **B2** | Main DC input power rail (24V nominal, 20V–28V operating; bounded by the MPQ4242's 36V VIN limit) and high-current power return; fused again on the blade. Parallel opposed pair placement minimizes inductive loop area (EMI reduction). Total 6-pin continuous capacity: ~6.6A; worst-case draw ~5.3A at 100W from the 20V floor. |
| **A3** | **VIN** | **PGND** | **B3** |  |
| **A4** | **VIN** | **PGND** | **B4** |  |
| **A5** | **VIN** | **PGND** | **B5** |  |
| **A6** | **VIN** | **PGND** | **B6** |  |
| **A7** | **VIN** | **PGND** | **B7** |  |
| **A8** | **PGND** | **PGND** | **B8** | Power ground plane extension and thermal dissipation fingers. |
| **A9** | **PGND** | **PGND** | **B9** | (B9 carries EXP_RST# at the management slot only; grounded at charger slots.) |
| **A10** | **PGND** | **GND** | **B10** | Power-to-signal ground transition boundary. (B10 carries LED_DATA at the management slot only.) |
| **A11** | **GND** *(Shield Guard)* | **GND** *(Shield Guard)* | **B11** | High-isolation guard pins flanking the mechanical key notch. |
| **--- MECHANICAL KEY / POLARIZING NOTCH ---** |  |  |  |  |
| **A12** | **3V3** | **3V3** | **B12** | Regulated 3.3V logic supply for INA226 and MPQ4242 logic bias, sourced by the backplane's TPS5430 buck (I2C/ALERT# pull-ups are on the backplane, not the blade). |
| **A13** | **GND** | **GND** | **B13** | Dedicated quiet logic ground reference plane. |
| **A14** | **SCL** | **SDA** | **B14** | Dedicated I2C Clock (A14) and Data (B14) from the TCA9548A mux channel. SDA/SCL pull-ups to 3V3 reside on the backplane (one 4.7 kΩ pair per downstream segment); blades carry no I2C pull-ups. |
| **A15** | **GND** *(Shield Guard)* | **ALERT#** | **B15** | Active-low open-drain fault/interrupt line from INA226 & MPQ4242, wire-OR'd across all blades into GLOBAL_ALERT#; shielded by A15. Pull-up resides on the backplane; blades carry none. |
| **A16** | **GND** | **EN** | **B16** | Hardware enable/shutdown signal (B16) driven by the backplane GPIO expander; 100 kΩ pull-down on the blade holds it off. |
| **A17** | **GND** *(Shield Guard)* | **PRSNT2#** *(Short Pin)* | **B17** | Presence sense loop return (B17). Shorter pin length ensures PRSNT# asserts only at full seating. |
| **A18** | **GND** *(End Guard)* | **GND** *(End Guard)* | **B18** | Outer edge ESD guard and termination reference. |

## 4. Management Interconnect Architecture

### 4.1. The Decision: Management Slot vs. Detached Controller

Two interconnect options were evaluated for joining the RP2350 controller to the backplane:

> * **Option A (rejected): detached controller.** The management PCB mounts to the rear/side chassis panel and a ~10-conductor ribbon or FFC carries the upstream bus (I2C, LED_DATA, GLOBAL_ALERT#, EXP_INT#, the reset lines, and power) from the backplane. Rejected: an internal cable assembly, a second mounting system, and no mechanical commonality with the blades.
> * **Option B (chosen, 2026-08-28): front-access management slot.** The controller rides a card that slides into a seventh card-edge socket on the backplane, alongside the six charger slots — the "Slot 9" of the original 8-blade draft. No internal cables, uniform card-guide alignment, and the same connector family as the blades.

The 10-signal interconnect set from Option A survives as the development carrier's breakout header (§4.3).

### 4.2. Management Slot Pinout (As Built)

The management socket reuses the blade connector and pin geography: power half before the key notch, logic half after it. Management-only signals occupy pins that are grounded at the charger slots, so the two socket types stay drop-in compatible at the PCB level. Netlist-verified:

| Pin | Side A (Component Side) | Side B (Solder Side) | Pin | Management Slot Functionality |
| :---: | :---- | :---- | :---: | :---- |
| **A1** | **GND** | **GND** | **B1** | No presence circuit on the management slot (see §8.5). |
| **A2–A7** | **VIN** | **PGND** | **B2–B7** | Main DC bus (24V nominal, 20V–28V operating) available to the management card for its own regulation if desired; the dev carrier leaves it unused. |
| **A8–A9** | **PGND** | **PGND / EXP_RST#** | **B8–B9** | B9 = TCA9539 hardware reset line. |
| **A10** | **PGND** | **LED_DATA** | **B10** | Single-wire WS2812C chain data into the backplane (220 Ω series). |
| **A11** | **GND** *(Shield Guard)* | **GND** *(Shield Guard)* | **B11** | Notch guard ground shield. |
| **--- MECHANICAL KEY / POLARIZING NOTCH ---** |  |  |  |  |
| **A12** | **3V3** | **3V3** | **B12** | Backplane-sourced 3.3 V (TPS5430): the management card needs no logic regulator of its own. |
| **A13** | **GND** | **GND** | **B13** | Quiet digital ground plane reference. |
| **A14** | **SCL** | **SDA** | **B14** | Upstream I2C master bus to the TCA9548A mux and TCA9539 expander. |
| **A15** | **EXP_INT#** | **GLOBAL_ALERT#** | **B15** | Expander interrupt (blade insertion/removal) and the wire-OR'd blade ALERT# line. |
| **A16** | **GND** | **MUX_RST#** | **B16** | TCA9548A hardware reset — clears a hung downstream I2C segment without power cycling. |
| **A17** | **GND** | **GND** | **B17** | Grounded (no presence loop). |
| **A18** | **GND** *(End Guard)* | **GND** *(End Guard)* | **B18** | Outer edge ESD ground guard. |

Cross-insertion note: the sockets are mechanically identical. A charger blade seated in the management slot finds MUX_RST# (idle high) on its EN finger and will run unmanaged on its MPQ4242 OTP defaults — not hazardous, but label the chassis so it doesn't surprise anyone.

### 4.3. Development Carrier: pcie-breakout + Pico 2 W

`hardware/pcie-breakout` is the current management card: a passive adapter from the management-slot card edge to a 2.54 mm 1x10 header, wired point-to-point to a Raspberry Pi Pico 2 W per the firmware GPIO map:

| Header Pin | Signal | Pico 2 W GPIO |
| :---: | :---- | :---- |
| 1 | VIN | — (unused) |
| 2 | 3V3 | — (logic reference; Pico is USB-powered during dev) |
| 3 | GND | GND |
| 4 | SCL | GP5 (I2C0) |
| 5 | SDA | GP4 (I2C0) |
| 6 | EXP_INT# | GP6 |
| 7 | EXP_RST# | GP8 |
| 8 | MUX_RST# | GP7 |
| 9 | LED_DATA | GP2 (PIO) |
| 10 | GLOBAL_ALERT# | GP3 |

GP16–21 stay reserved for the wired-Ethernet path (W6100 per the EVB-Pico2 mapping), so the same firmware image spans the dev carrier and the production board.

### 4.4. Production Controller (Planned)

A custom RP2350 management card for the same slot: W6100 wired Ethernet (RJ45 MagJack flush with the faceplate), USB-C maintenance/console port, and optionally a Raspberry Pi RM2 radio module for WiFi plus BLE-based initial provisioning. Same GPIO map as §4.3.

**Flash (decided 2026-08-30): RP2350A + external W25Q128 (16 MB) on the primary QSPI chip-select.** The RP2354's 2 MB in-package flash was rejected: under A/B OTA it leaves ~960 KB per image slot, and the CYW43439 WiFi blob alone is 220 KB of the current 448 KB build — BLE and web-UI assets would crowd the ceiling, permanently, since in-package flash can't be upsized. A hybrid (RP2354 + second chip on QMI CS1) was also rejected: the RP2354 shares its QSPI pads with the stacked die, so a second chip puts the bus on the board anyway, while the SDK has no CS1 flash driver and the app slots stay capped at 2 MB. Because RP2350A and RP2354A share the QFN-60 footprint, the layout keeps a lean assembly variant open — populate RP2354A and DNP the external flash (never both: same chip-select). QMI CS1 (GPIO0 on this package) stays free for possible PSRAM.

## 5. Mechanical Enclosure & Thermal Strategy

The mechanical design supports modular serviceability, high-current thermal conduction, and low-noise network integration:

> * **Front-Panel Light Pipes:** Rigid optical light pipes (3.0mm diameter) channel light from the six backplane WS2812C LEDs directly to the front faceplate above each USB-C port, maintaining slot status visibility even when a blade is unseated.
> * **Management Slot Front-Panel Alignment:** The management card's connectors (RJ45 MagJack and maintenance USB-C on the production board) align flush with the front faceplate alongside ports 1–6.
> * **Ethernet Grounding & ESD:** The RJ45 shield bonds to the chassis through its EMI spring fingers; magnetics center taps terminate Bob-Smith style into the chassis-bonded ground region (§8.3 — the earlier capacitive CHGND barrier was deleted in favor of deliberate multipoint bonding).
> * **Thermal Distribution:** Each blade uses 1oz outer and inner copper planes with arrays of thermal vias under the MPQ4242 exposed pad to transfer heat into the card-edge ground planes (PGND Pins A8-A10, B2-B9).

## 6. Firmware Architecture & Supervisory State Machine (As Implemented)

Implemented in `firmware/controller` (native pico-sdk C). The descriptions below match the shipped code.

### 6.1. Dual-Core RP2350 Execution Model

The two cores split along one rule: **only core 1 touches the backplane.**

> * **Core 1 — Engine (hard real-time power supervision):**
>   * 100 Hz supervisory tick: round-robins the TCA9548A channels reading each blade's INA226 and MPQ4242, runs the per-port state machine and the budget arbiter.
>   * Sole owner of the I2C bus and the TCA9539 (blade EN, presence, fan). Presence is re-read every 100 ms and on EXP_INT#.
>   * Services GLOBAL_ALERT# (edge interrupt + level check every tick): sweeps powered ports, kills offenders via EN.
>   * Renders the WS2812C chain via PIO and publishes a seqlock telemetry snapshot plus a heartbeat.
> * **Core 0 — Management plane:**
>   * CYW43 WiFi + lwIP: MQTT client with Home Assistant discovery and LWT availability, mDNS, SNTP, embedded web UI + JSON REST API, USB CDC maintenance CLI.
>   * Owns settings (ping-pong flash sectors, CRC-protected) and feeds the hardware watchdog — but only while the core-1 heartbeat stays fresh, so either core stalling reboots the system.
> * **Between them:** a command queue (core 0 → 1), an event queue (1 → 0), and the telemetry seqlock. Every management surface is a thin transport over the same command/telemetry interface.

### 6.2. Per-Port Firmware State Machine

The MPQ4242 negotiates PD contracts autonomously, so there is no in-line "negotiating" state: the engine constrains the advertised PDO set ahead of time and reacts to the contracts it observes via STATUS2/STATUS3.

| State | Entry Condition / Trigger | Actions & Hardware Control | LED Indication | Next State Transitions |
| :---- | :---- | :---- | :---- | :---- |
| **ABSENT** | PRES# high (no blade in slot). | EN low, mux channel unused, budget released. | Dim White | → PROBE on PRES# low (→ DISABLED if administratively off). |
| **PROBE** | Blade seated. | One attempt per tick: select mux channel, probe/configure INA226 (alert at 125% of the port's current limit, latched), probe/configure MPQ4242 (GPIO fns, peak CL, CC blank time, spread-spectrum dither, 5 Fixed + 2 PPS PDO set, PDO current ceiling), assert EN. 3 failures → FAULT. | Blinking Cyan (2 Hz) | → IDLE on success. → FAULT on repeated probe failure. |
| **IDLE** | Powered, advertising, no sink. | 15 W base budget reservation held. Poll INA/MPQ each tick. | Solid Amber | → ACTIVE on sink attach. → ABSENT on removal. |
| **ACTIVE** | Sink attached, contract in place. | Full contract wattage reserved. Telemetry at tick rate; contract changes tracked against the budget. | Solid Blue (<19V) / Solid Green (≥19V) | → IDLE on detach. → THROTTLED on budget denial. → FAULT on alert. |
| **THROTTLED** | Contract would exceed the chassis budget. | Advertisement clamped to the granted wattage (never below 15 W); the refused ask is remembered for recovery (§6.3). | Pulsing (1 Hz) in the port's active colour | → ACTIVE when freed budget covers the original ask. → IDLE on detach. → FAULT on alert. |
| **FAULT** | INA226 over-current alert or MPQ4242 fault bits. | EN low immediately, budget released, fault latched for diagnostics, 5 s cooldown. | Blinking Red (5 Hz) | → PROBE after cooldown (auto-retry). → ABSENT on removal. |
| **DISABLED** | Administrative off (CLI/MQTT/REST). | EN low, budget released. | Off | → PROBE on enable. → ABSENT on removal. |

> * **Chain-wide indications:** the power-up sweep lights the six pixels in slot order (white or rainbow, `led boot`) and doubles as a chain-order check on a fresh chassis. While a chassis condition needs attention a two-pixel comet crosses the chain every 3 s over the port colours: blue while the BLE provisioning window is open, white while no link holds an address. Improv *identify* alternates the two halves of the chain in blue at 2 Hz and outranks everything. Master brightness (`led`, the web *Settings* panel, or the Home Assistant number) scales all of it; at 0 the chain is dark except a faulted port, which keeps blinking at a floor level.

### 6.3. Dynamic Power Budgeting & Priority-Aware Allocation

To operate safely within a fixed chassis supply rating (default budget 360 W, i.e. 15 A at 24 V; six 100 W ports = 600 W aggregate peak capacity), the supervisor maintains contract-based power accounting:

> * **Allocated Budget vs. Real-Time Consumption:** When a device negotiates a contract, the supervisor reserves the full contract wattage; measured draw is telemetry, not accounting. Every powered port additionally holds a 15 W base reservation (5 V @ 3 A):
>   P_headroom = P_chassis_max − SUM(P_reserved[i])
> * **Priority Shedding:** Each port carries a priority (0 = highest; default = port number; `port <n> priority` in the CLI). When a new contract would exceed the budget, strictly lower-priority powered ports are renegotiated downward first — worst priority first, largest reservation breaking ties, never below the 15 W floor — via an MPQ4242 current-limit rewrite and source-capability re-advertisement, without dropping VBUS. Equal priority is never shed: first come, first served among peers.
> * **Self-Clamp Fallback:** If shedding lower-priority ports cannot free enough, the requesting port itself is clamped to whatever headroom remains.
> * **Recovery:** A throttled port remembers the ask that was refused. When the whole refused amount fits the freed budget (detach, renegotiation, shed elsewhere), the reservation is claimed first, then the full advertisement is restored and the sink renegotiates upward. When only part of it fits, the clamp steps up by the available headroom instead — in ≥5 W increments, at most one step per second per port — so partially freed watts flow immediately rather than waiting for the full ask. Freed headroom always goes top-down by priority: a lower-priority throttled port yields while any hungrier higher-priority one exists, since that port can take the headroom partially too (EVT_THROTTLE codes: 0 clamped, 1 restored, 2 partial step).

### 6.4. Implementation Notes

> * **INA226 Configuration Constants:** R_SHUNT = 10 mΩ (the blade's low-side shunt, shared with the MPQ4242 RSENS current loop via Kelvin taps). Shunt full-scale ±81.92 mV → ±8.19 A measurable range (5 A contract max = 50 mV; the 125% alert point at 6.25 A = 62.5 mV fits comfortably). Calibration: current_LSB = 0.25 mA → CAL register = 0.00512 / (current_LSB × R_SHUNT) = **2048**; power LSB = 25 × current_LSB = **6.25 mW**. Address straps A0 = A1 = GND → **0x40** on every blade (identical addresses are fine — each blade sits on its own TCA9548A segment; no conflict with the MPQ4242 at 0x61). The over-current alert uses the latched shunt-over-limit function (SOL + LEN) programmed at 125% of the port's current limit during PROBE, so a trip persists until read back during FAULT diagnosis. The threshold sits above everything a healthy blade can sustain — the MPQ4242's PD engine sets its CC limit (IOUT_LIM) to the contract current with ±5–10% accuracy and clamps + hard-resets within its CC blank time — so the INA226 (comparing the ~19 ms averaged shunt reading) fires only when blade-side limiting has failed. 125% also keeps the connector safe without further supervision: a trip at 6.25 A × 20 V = 125 W bounds card-edge VIN input current to ~6.6 A at the 20 V floor, right at the pin rating. The blades use the GVE-0000 all-OTP-defaults part, and PROBE overrides several OTP defaults: the boost peak limit down from 20 A to 8 A (academic at 20–28 V in, where the converter rarely leaves buck and the 6.7 A SWB valley limit governs bursts), the CC blank time to an explicit 16 ms — datasheet Rev 1.0 lists two conflicting defaults (2 ms and 16 ms) — keeping the ≤10 ms PD overload tiers usable, spread-spectrum dither on (±11% around 420 kHz, ships disabled) to soften switching-harmonic peaks into the input filter, and the advertised PDO table to 5 Fixed PDOs (5V, 9V, 12V, 15V, 20V) + 2 PPS APDOs (3.3V–11.0V, 3.3V–21.0V), trading the redundant 16V PPS APDO for native 12V Fixed support so PD trigger boards, Switch docks, and laptops all negotiate their preferred rails without compromise. A maximally aggressive overload sink on a 100 W contract averages ~6.05 A over the ~19 ms window, under the 6.25 A alert point with margin, and bursts are capped by the 6.7 A valley limit.
> * **TCA9539 Initialization Order (Critical):** The expander's Output Port registers power up as **0xFF (all high)** while every pin defaults to input (hi-Z); the blades' 100 kΩ EN pull-downs hold all ports off in that state. Firmware MUST write the Output Port registers to **0x0000 first**, and only then write the Configuration registers (P0 = 0x80: EN1–6 + fan as outputs, P07 input; P1 = 0xFF: all inputs) to make the EN pins outputs. Reversing the order drives every EN high the instant the config write lands — enabling all blades simultaneously, bypassing the per-port probe/budget sequence. Same rule applies after any EXP_RST# assertion or brown-out re-init.
> * **Expander/Bus Failure Recovery:** Five consecutive expander read failures trigger a MUX_RST# hardware reset (frees a hung downstream segment) and a TCA9539 re-init — which drops every EN. Safe-side by design: the ports re-probe and renegotiate from scratch.
> * **Flash Layout (partition table):** An RP2350 partition table (per-board JSON in `partitions/`, compiled to `partition_table.uf2` and flashed once) carves the flash into A/B image slots plus a `data` partition — on the 4 MB dev Pico: table at 0, 2 × 1536 KB slots, 1016 KB data; the 16 MB production map adds an `assets` partition. The firmware never hardcodes offsets: partitions are located **by 64-bit ID** through the bootrom at boot, so one binary spans every layout, and raw-offset reads go through the untranslated XIP alias (0x1C000000) so they stay correct when the bootrom maps slot B at the XIP base. Each image carries an IMAGE_DEF version derived from FW_VERSION (major, minor×256+patch), so the bootrom boots the newer slot and BOOTSEL drag-drop updates land in the inactive slot. A TBYB-flagged image boots as a *trial* and self-commits (bootrom explicit-buy) only after 10 s of continuous health — engine heartbeat plus network up if configured — otherwise any reboot, watchdog bite, or the 10-minute deadline reverts to the previous image.
> * **OTA:** `POST /api/v1/update` streams a firmware image (release UF2 or raw .bin, auto-detected; picotool's E10 marker block skipped) into the inactive A/B slot, chosen via `rom_pick_ab_partition_during_update`. Writes go sector-by-sector through `flash_safe_execute`; the image's first sector — the only place the bootrom looks for an IMAGE_DEF — is erased up front and written last with the TBYB flag patched in (refused for hashed/signed images, which the flag edit would invalidate), so an interrupted transfer never leaves a bootable half-image. After CRC/read-back verification the controller reboots via a bootrom flash-update boot (the only path that boots a TBYB image), handing off to the trial/commit flow above. One transfer at a time; a stalled client is deposed after 30 s; an uncommitted trial refuses updates so the known-good fallback slot is never overwritten.
> * **Settings:** Ping-pong pair of 4 KB sectors with sequence numbers and CRC32 (a power failure mid-write leaves the previous copy intact), homed in the first two sectors of the `data` partition. Unpartitioned boards fall back to the legacy top-of-flash pair; a freshly partitioned board finds legacy settings there, migrates them into the data partition on first boot, and retires the legacy copies.

### 6.5. Management Plane

> * **MQTT (primary remote surface):** telemetry under `pwrman/<name>/...` at 1 Hz, retained chassis status, commands on `.../port/<n>/set` (ON/OFF/hard_reset/src_cap), `.../port/<n>/priority/set` and `.../fan/set` (auto/on/off), faults/contract/throttle events on `.../event`, availability via LWT. Home Assistant discovery publishes, per port: power/voltage/current/state sensors, a since-boot energy sensor (kWh, `total_increasing` — energy-dashboard ready), an enable switch, hard-reset/re-announce buttons and a priority number; plus chassis power/headroom/energy sensors, a fan mode select, and a firmware `update` entity fed from the retained `.../update/latest` pointer (`{"version","url"}`) whose Install pulls the URL over plain HTTP into the OTA core. Remote settings mutations persist via a debounced save.
> * **Fan policy:** auto mode (default) follows total chassis power with hysteresis (settings `fan_on_w`/`fan_off_w`, defaults 80/60 W) and a 30 s anti-flap hold; manual on/off overrides from any surface drop it out of auto.
> * **Fault log:** faults and probe failures persist in the data partition right after the settings pair — a 64 KB ring of one-record 256 B flash pages (~256 records, oldest sector recycled), each carrying event, port power/contract at that instant, uptime and SNTP wall-clock time. CLI `faults` / `faults clear`.
> * **Web UI + REST:** broker-independent local surface; live status page (incl. per-port energy), `GET /api/v1/status`, `POST /api/v1/port/<n>` (enable/disable/hard_reset/src_cap), `POST /api/v1/fan` ({"on":bool} or {"mode":"auto"}), `POST /api/v1/update` (OTA push); bearer-token auth optional.
> * **USB CLI:** provisioning (WiFi/MQTT credentials, device name), status, budget/priority/port control, fan policy, fault log, `update <http-url>` OTA pull, `bootsel`.
> * **Planned:** BLE provisioning via the RM2 on the production board; front-panel display as another consumer of the telemetry snapshot.

### 6.6. Verification

The engine core (state machine + budget arbiter) is hardware-free and runs host-side against simulated drivers (`test/`, in CI on every firmware change): probe/fault/recovery paths, contract accounting, priority shedding and recovery ordering. The same simulated backplane compiles into the firmware (`-DFAKE_BLADES=ON`) and loops a scripted demo — attaches, budget contention, an over-current fault — so the full management plane runs on a bare Pico 2 W with no backplane.

## 7. Blade BOM Cost Comparison Summary

Consolidating the architecture reduces the total manufacturing cost per blade by approximately **$0.59 (-10.6%)**, while removing firmware flashing bottlenecks across separate per-blade processors.

| Component / Function | Original Blade Cost (800U) | Consolidated Blade Cost (800U) | Cost Variance |
| :---- | ----: | ----: | ----- |
| Base PCBA & Passives Subsystem | $2.06 | $1.48 | -$0.58 |
| MPS MPQ4242 IC + Assembly | $2.81 | $2.81 | $0.00 |
| 5A USB-C Receptacle + Assembly | $0.24 | $0.24 | $0.00 |
| Shipping per Blade | $0.44 | $0.44 | $0.00 |
| **Total BOM Cost per Blade** | **$5.56** | **$4.97** | **-$0.59 (-10.6%)** |
| **6-Blade Set Total (1 Chassis)** | **$33.36** | **$29.82** | **-$3.54 / chassis** |

## 8. Grounding, Chassis Bonding & ESD Protection Architecture

### 8.1. Two-Tier Grounding Model

A three-domain, star-grounded topology is not physically realizable in this architecture: the MPQ4242 requires AGND and PGND joined at the IC on every blade, and the backplane's buck regulator joins the logic-rail return to the DC bus return — the system inherently contains seven or more domain ties, so no "single star point" can exist. Splitting copper planes anyway would force every I2C, EN, ALERT#, and PRSNT# trace to cross a plane slit with a broken return path. V2 therefore uses two tiers:

* **Circuit Ground (GND):** One electrically continuous ground net per board and across the card-edge interconnect. "PGND" survives as layout geography — named copper zones that confine high-current switching returns to defined regions (§8.2) — not as a separate net. All GND and PGND card-edge fingers land on the same net.
* **Chassis (CHGND):** The metal enclosure, faceplate, card guides, and connector shells, deliberately multipoint-bonded to circuit ground (§8.3). Chassis is an ESD/EMI spreading structure, never an intentional current path.

Measurement integrity is preserved by architecture rather than plane splits: the INA226 samples the shunt differentially through Kelvin connections (immune to ground offset), its bus-voltage reading tolerates millivolt-scale ground shift as a <0.05% error on a 5–20V rail, and I2C noise margins (~400mV at 3.3V) dwarf any achievable inter-board ground offset.

### 8.2. Board-Level Ground-Zone Layout

**Charger blade (4-layer, 2oz outer/inner):**

* **L2 — solid, unbroken ground plane.** No slots, no splits. It serves simultaneously as power return, logic reference, and the thermal spreader that carries MPQ4242 heat to the card-edge fingers (§5). Every ground via from every zone lands on it.
* **L1 power spine ("PGND" zone):** VIN entry pour from fingers A2–A7/B2–B7 → input bulk and ceramic capacitors → MPQ4242 power pins and inductor → output capacitors → current-sense shunt → USB-C receptacle. Both hot loops (input-capacitor and output-capacitor) are minimum-area and strictly local.
* **L1 logic strip ("GND" zone):** Runs from the logic-half fingers (3V3, SCL/SDA, ALERT#, EN) forward past the MPQ4242 digital pins to the INA226 at the port. All logic routing stays over this strip; no logic trace passes under the inductor or switch-node copper.
* **MPQ4242 analog corner:** A small quiet pour for the ICOMP compensation network, ISENS+/− filtering, and SEL components, joined to the plane at/under the IC exactly as the MPS reference layout shows. This is the only single-point-tie structure in the system.
* **Kelvin sense routing:** Shunt sense lines run as a tight differential pair over solid ground, laterally clear of the inductor and switch node.

Zones are drawn as separate outlines on the *same net* (multiple zones, one net) — they connect through the L2 plane rather than by touching on L1, so pours cannot silently merge across the geographic boundary, and a stray trace over the wrong zone degrades gracefully instead of losing its return path.

**Backplane:** One solid ground plane under the entire board; all seven sockets' GND and PGND fingers land on it. VIN distributes on dedicated copper pours on the outer layers only (standard 1oz outer / 0.5oz inner 4-layer stack — verified adequate: minimum trace/space on the board is 0.16mm-class, and the VIN corridor is pour-based). The DC input (−) terminal lands on the plane adjacent to a chassis-bonded mounting hole, so the heavy-current corridor runs between the DC entry and the slots' power-half fingers; the TCA9548A / TCA9539 / LED / management-slot region is placed outside that corridor. I2C, EN, ALERT#, and PRSNT# route over the unbroken plane — no split exists for them to cross.

### 8.3. Chassis Bonding & Enclosure (Metal Case)

The chassis and circuit ground form one deliberately multipoint-bonded ground system (PC-chassis model):

* **Bonded standoffs:** Plated, unmasked mounting holes (annular ring stitched with vias) on the backplane, fastened with star washers into conductive metal standoffs.
* **USB-C shells:** Each receptacle's four through-hole shell tabs solder into the blade ground pour with multiple vias; shell contact with the faceplate cutouts provides additional chassis bonds by design.
* **RJ45 MagJack (production controller):** 360° EMI spring fingers against the enclosure cutout are the shield's chassis bond. The magnetics center taps terminate Bob-Smith style — 75 Ω per pair into a common node, then 1 nF / 2 kV into the chassis-bonded ground region at the jack.
* **DC entry:** The input (−) terminal, a bonded standoff, and the ground plane meet in one corner of the backplane, so the highest-current cable entry references chassis at its point of entry.
* **Deleted from prior drafts:** The 1 nF / 1 MΩ CHGND-to-GND barrier network and the 2.0 mm isolation moat. With shells, spring fingers, and card guides bonding the chassis at many points regardless, a capacitive isolation barrier cannot exist in practice; deliberate multipoint bonding replaces it. An ESD strike to any shell or panel spreads through the chassis and returns via many bonds, with only a small fraction traversing any single board.

### 8.4. Port ESD & DC Input Transient Suppression

* **CC1, CC2 & Data Lines (each blade):** Ultra-low capacitance TVS diode array (TI TPD4E05U06 or ST USBLC6-2SC6, C_IO < 0.5 pF) directly at the receptacle pads to meet IEC 61000-4-2 Level 4 (±15 kV air, ±8 kV contact).
* **V_BUS Transient Clamping (each blade):** Unidirectional SMBJ24A TVS (24V stand-off fits the ~21V max VBUS, 600W peak pulse) between V_BUS and ground at the connector — USB cables *are* hot-plugged by users, and abrupt disconnection of a 100W load produces an inductive load dump. (SMBJ28A was rejected: it clamps too high for the sink and would pass a 28V VIN-fault through.)
* **DC Bus Clamping (backplane input):** Unidirectional SMCJ33A TVS across VIN and ground at the DC input connector, behind the 20 A fuse and LM74700 ideal-diode stage — 33V stand-off above the 28V operating ceiling, breakdown from ~36.7V. The MPQ4242's 36V VIN(max) is the binding constraint that caps the bus at the 24V class (48V operation is not feasible), so the clamp's job is holding realistic supply-side transients within the converter's absolute-maximum headroom above 36V: connection ring when the DC cord meets ~2 mF of aggregate chassis capacitance, and inductive kick if the cord or supply drops out under load. Energize the external supply *after* the DC cord is connected (or fit an anti-spark/NTC element at the entry) to tame connection inrush.

### 8.5. Card-Edge Guarding & Presence Detection

The system is **not hot-pluggable**: installed USB-C cables preclude removing the front plate while in service, so blades are only inserted or removed with the chassis de-energized. The presence circuit is therefore a population and seating check, not a hot-swap mechanism — it needs no precharge path, no extra finger stagger beyond the standard PCIe scheme, and no extraction-race firmware.

* **Finger heights:** Standard PCIe CEM two-length geometry. All power, ground, and signal fingers are full height; only A1 and B17 are short (CEM short-pin height per the connector drawing). Because the loop closes last, PRSNT# asserts only at *full* seating — a half-inserted blade reads absent and is never enabled.
* **Presence loop (A1 → B17):** The blade connects A1 to B17 with a plain trace — no components, no power dependency. The backplane grounds A1 at every charger slot. The loop closes only when both ends of the connector are seated, so an angled or partial insertion cannot false-positive. B17 has a 10 kΩ pull-up to 3V3 and enters a TCA9539 input through a 330 Ω series resistor; the expander INT output provides EXP_INT#.
* **Management slot:** No presence circuit — A1 and B17 are simply grounded (§4.2). The management card is installed as part of commissioning, with the chassis de-energized like everything else; the firmware, not the backplane, is the thing that notices whether a controller is present.
* **Firmware use:** Presence is enumerated at boot and re-read every 100 ms plus on EXP_INT# — empty slots are never probed and their EN lines never assert. As cheap defense-in-depth (not a hot-plug feature), any runtime PRSNT# deassertion — a seating or vibration fault — immediately drives that slot's EN low.
* **Edge guards:** A18/B18 remain full-height sacrificial grounds at the outer end, discharging handling static before signal fingers seat during bench assembly; A11/B11 guard the key notch. A16 is committed to GND (no floating RESERVED finger adjacent to EN), and B1 joins the main power return — mirroring A18/B18, both ends of the connector row terminate in solid ground.
