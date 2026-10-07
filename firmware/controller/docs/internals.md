# Controller internals

How the controller firmware is structured and how its less visible machinery behaves. For what an owner sees, start with the [user guide](https://docs.powermanifold.io/guide/front-panel/).

## Architecture

The firmware runs on two cores. **Only core 1 accesses the backplane.**

- **Core 1 — engine** (`src/engine/`): 100 Hz supervisory loop. Round-robins
  the mux channels reading each blade (a gen-2 blade's INA226 and MPQ4242, a
  gen-3 blade's register file — see [Blade generations](#blade-generations)),
  runs the per-port
  state machine and the chassis power-budget arbiter, owns blade EN / presence
  / fan via the expander, services GLOBAL_ALERT# / EXP_INT#, renders the
  WS2812C status LEDs via PIO. When a new contract would exceed the chassis
  budget, strictly lower-priority ports are renegotiated downward first
  (worst priority first, 15 W floor); only if that isn't enough does the
  newcomer get clamped. Throttled ports recover automatically when budget
  frees up, highest priority first. Per-port priority: `port <n> priority`
  in the CLI (0 = highest, default = port number).
- **Core 0 — management** (`src/net/`, `src/cli.c`): lwIP over CYW43 WiFi
  and/or a WIZnet W6100 wired Ethernet controller (DHCP or a static
  address), MQTT (optionally over TLS) with Home Assistant discovery,
  embedded web UI + JSON API
  and a Prometheus endpoint, USB CDC maintenance console mirrored to a log
  ring and optionally a syslog host, Improv Wi-Fi setup
  on the same CYW43 radio.
- **Between them** (`src/ipc.c`): a command queue, an event queue, and a
  seqlock telemetry snapshot. Every management surface is a thin transport
  over the same command/telemetry interface.

The hardware watchdog is fed only while both cores make progress.

## GPIO map

Two boards run this firmware, and `src/pins.h` carries a map for each: the
production controller card (the default build, `pwrman_controller_card`) and
the Pico 2 W on the pcie-breakout, a development stand-in until the card
exists (`PICO_BOARD=pico2_w`, also the W6100-EVB-Pico2 in the same socket;
see [Boards](building.md#boards)).

| Signal | Pico 2 W carrier | Controller card | Notes |
| --- | --- | --- | --- |
| LED_DATA | GP2 | GP2 | WS2812C chain, PIO |
| GLOBAL_ALERT# | GP3 | GP7 | wire-OR of blade ALERT# lines; open-drain, 4.7 kΩ to 3.3 V on the card (a resistor wired on the pcie-breakout), pad pull-down cleared at init |
| SDA/SCL | GP4/GP5 | GP4/GP5 | I2C0; 4.7 kΩ pull-ups on the card (or wired on the pcie-breakout), none on the backplane's upstream side; pad pull-down cleared at init |
| EXP_INT# | GP6 | GP6 | TCA9539 interrupt; open-drain, internal pull-up only |
| MUX_RST# | GP7 | GP8 | TCA9548A reset; on the carrier released = high impedance (the backplane's 10 kΩ takes it to 5 V), asserted = driven low |
| EXP_RST# | GP8 | GP1 | TCA9539 reset, same drive as MUX_RST# |
| UPS_TX/UPS_RX | GP12/GP13 | GP12/GP13 | UART0, 9600 8N1, to a Mean Well LAD-xxxU UPS supply (the card's UPS header) |
| SPI0 MISO/CS/SCK/MOSI | GP16–GP19 | GP16–GP19 | W6100 wired Ethernet (WIZnet EVB-Pico2 pinout) |
| ETH_RST# | GP20 | GP20 | W6100 reset |
| ETH_INT# | GP21 | GP21 | W6100 interrupt, level-low while a frame waits |
| BUTTON | GP22 (a wire to GND) | GP22 | front-panel button, see [Front-panel button](https://docs.powermanifold.io/guide/front-panel/) |
| VIN_SENSE | — | GP28 / ADC2 | DC bus voltage through 120 kΩ / 10 kΩ, see [Bus voltage](#bus-voltage) |
| RM2 radio | — | GP23/24/25/29 | the Pico 2 W's own CYW43 wiring, so the radio code carries over unchanged |

The two reset lines differ in more than pin number. Both parts run on
the backplane's 5 V and want 3.5 V for a high, so the carrier never
drives a 3.3 V high onto them: released is a high-impedance input and
the backplane's 10 kΩ pull-up holds the line at 5 V, asserted drives it
low (`src/engine/rst_line.h`; a 3.3 V push-pull high let the expander
fall back to its power-on registers at random on the bench). The card
drives 2N7002 gates against the same pull-ups, so there the GPIO goes
*high* to assert a reset — `RST_ASSERTED_LEVEL` in `pins.h`, and the
drivers only ever write that. Either way a controller reset leaves both
lines released (the RP2350's boot-time pull-down keeps the card's FETs
off), which is what makes a [warm start](#warm-start) possible. Should
the expander ever come back at its power-on registers anyway, the
presence refresh notices the lost configuration word, rewrites outputs
and direction with every EN as it was, and logs `probe: expander reset`.
An expander that does not answer is not reset straight away. The mux is
reset after half a second, which frees a blade segment holding the bus and
does not affect port power. The expander itself is reset only after five
seconds without an answer, and its EN pattern is written back immediately.
See [Keeping ports powered](#keeping-ports-powered).

Behind the mux and the expander, port *n* (1-based wherever the firmware
talks to a person) is mux channel *n*−1, EN on P0(*n*−1), PRSNT# on
P1(*n*−1) and pixel *n* of the light-bar chain (pixel 0 is the chassis
light, ahead of port 1), and the backplane wires all four
to one socket, in silkscreen order from the management socket toward the
power input (sockets J3, J5, J7, J4, J6, J8 after the re-annotation; the
[architecture](https://docs.powermanifold.io/developers/architecture/#31-slot-numbering)
page has the table). Verified against the backplane netlist and layout on
2026-09-09. Facing the front panel, port 1 is on the left next to the
management card and port 6 on the right, each light above its port with the
chassis light left of port 1; the power-up sweep runs left to right from the
chassis light.

## Blade generations

Two charger blades fit the chassis, and a probe tells them apart by what
answers on the slot's mux channel: a gen-3 blade's register file at 0x3A
first, else a gen-2 blade's INA226 at 0x40. The port engine
(`src/engine/port_fsm.c`) does not depend on the generation.
`src/engine/blade.c` puts both behind one interface, and the status table,
the JSON and the MQTT telemetry carry the generation as `gen`. A chassis can
hold a mix, and the budget arbiter sheds and restores across generations by
priority alone.

- **Gen 2** (`hardware/charger-module` up to 0.14): an MPQ4242 negotiates
  PD on its own and an INA226 meters the port. The controller writes the
  part's PDO table after every power-up, asks it to re-advertise after a
  change, and arms the INA226's alert at 125 % of the blade's 5 A ceiling
  as an emergency trip.
- **Gen 3** (0.15 and later): an STM32G071 runs the port with ST's USB-PD
  stack, a TPS55288 converter and a TCPP02-M18 port protector, and speaks
  the register map in `firmware/charger-module/include/blade_regs.h`, which
  this firmware includes as-is. Its MCU runs from the slot's 5 V, so it
  answers with EN low, and its port stays dark until it has been given a
  current ceiling and a voltage cap and told to advertise — the probe does
  both. It re-advertises by itself whenever its limits change, meters VBUS
  and the port current itself, reports its converter, receptacle and MCU
  temperatures (the status table, the JSON, `/metrics` and two Home
  Assistant sensors per port carry them), and carries its own
  over-current, over-voltage and thermal protection. A fault takes the port down on the
  blade and stays latched, ALERT# low with it, until the controller has
  recorded it: the engine drops EN, clears the latch and re-probes after
  the cooldown, and a fault whose cause persists (a hot receptacle) comes
  straight back and repeats the cycle. A blade whose MCU restarts (its
  watchdog, a brown-out on the slot's 5 V) comes back unconfigured — or,
  once set to boot through its ROM bootloader, in that bootloader; either
  way the port notices, goes back through the probe (via a
  [bootloader trip](flash-and-updates.md#blade-firmware-updates) in the latter case) and is
  configured again with no EN cut and no fault recorded. Its firmware is
  programmed over the backplane by this controller.

The port fault bits — `fault` in the JSON and MQTT telemetry, `code` on a
fault event — are one vocabulary for both generations: bits 0–7 are the
MPQ4242's flags as a gen-2 blade reports them (`general`, `otw1`, `otw2`,
`ntc1`, `ntc2`, `cc`, `short-vbatt`, `vbatt-low`), bit 8 is an over-current
trip on either (`ocp`), and the rest are a gen-3 blade's: `ovp`, `vconn`,
`port-hot`, `converter`, `converter-hot`, `plug-hot` and `blade` (its own
parts or PD stack failed it). A fault event's `arg` carries the INA226 trip
flag for a gen-2 record and the blade's raw fault word for a gen-3 one, and
the fault text names a gen-3 fault in the blade's own words (`conv-ocp`,
`plug-hot`, `pd`), which is what the fault log, the console and the
`last_fault` attribute show.

## Warm start

A controller reboot — a firmware update, a `reboot` from the console or the
page, a watchdog or a HardFault — does not cut port power. The expander
that drives the blade EN lines is powered from the backplane, not the
controller, and neither board asserts its reset when the controller itself
resets, so it keeps its registers and every powered blade stays powered
while the firmware is away. At start the engine reads the expander before
touching it: if it still holds the configuration this firmware wrote, that
is a *warm start* and its output register is adopted as it stands; only an
expander in its power-on state (a *cold start*, after a power cut) is
reset and configured from scratch, outputs low first.

Every blade found powered is then taken back under supervision without
touching EN, in priority order, 50 ms apart: the mux channel is selected,
the blade identified (its generation with it), any fault latched while
the controller was down (an over-current trip, an MPQ4242 fault flag, a
gen-3 blade's fault register) is handled as a new fault — the usual path,
EN off, cooldown, re-probe — and the blade's configuration is compared,
read-only, with the settings and rewritten (and re-advertised to an
attached sink) only when it differs, so a live contract is normally not
interrupted. The port then reports `active`, with its contract reserved in
the budget. A blade that does not answer also stays powered, as it was
while the controller was down, and is shown as silent and asked again every
second ([Keeping ports powered](#keeping-ports-powered)); a port whose
boot policy is `off` (or `last` with the port last switched off) is
switched off at once; an EN left on with no blade seated behind it is
dropped. The fan is adopted the same way, and the policy then decides
whether it stays on. Whether it is a warm start at all rests on one read
of the expander, so an expander that does not answer that read is asked
again for half a second before its silence is taken for a cold start.

A warm start does not cover the time the controller was down: there is no
budget arbitration and no firmware fault response while it is away — only
the blades' own current and thermal limits — and the charge-complete hold
and sleep timers restart at the reboot. The console logs `engine: warm
start, ports 1 and 3 kept powered` (or `cold start`), `info` repeats it, and
the status JSON carries `warm_start`. The Pico 2 W carrier and the
controller card both behave this way; it is exercised against the simulator
in the host tests and still to be confirmed on a live backplane.

## Keeping ports powered

A port that is delivering power keeps delivering it unless one of the
following occurs: a fault the blade or the controller's own meter reports,
the blade being pulled, the port being switched off (by hand, by its sleep
timer or charged-off rule), or `port <n> update`. The budget never switches
a port off; it lowers what the port advertises.

The following events do not switch a port off. Each is handled as described:

- **The controller restarting**, for any reason and for however long: a
  warm start, above. The blade's watch timer does not run while the
  port's EN is high.
- **A blade that stops answering** while its port is powered. EN stays,
  the budget keeps the port's reservation, and nothing is decided from
  readings that have gone stale: no charge-complete, no throttling of that
  port. It is logged once (`probe: stopped answering`), `status` marks the
  port `[silent: last answer shown]`, the JSON and telemetry carry
  `silent`, the problem sensor lists it under `not answering`, and it is
  polled on; when it answers, its limits are checked against what it
  should hold and the port carries on. A gen-3 blade is also looked for in
  its bootloader, where a reset of its own MCU leaves it — that port is
  dark already, and the trip starts its firmware again. The same goes for
  a blade that will not answer at a warm start. Only a probe that *starts*
  from a dark port ends with EN off when nothing answers.
- **A newer blade firmware, or the boot option**: they wait for an empty
  port ([Blade firmware updates](flash-and-updates.md#blade-firmware-updates)).
- **A controller update that does not stick**: a trial image changes
  nothing about a blade.
- **A sagging DC bus**: the ports are capped at 3 A, never cut
  ([Bus voltage](#bus-voltage)).
- **One bad read.** A presence read that says a seated blade has left is
  taken again before it is believed; an expander whose configuration word
  cannot be read is not treated as one that lost it; an expander that does
  not answer is left holding its EN lines while the mux is reset, and is
  itself reset (EN pattern written straight back) only after five seconds
  out of reach.

As a result, a port may run unsupervised for a time, on the limits it was
last given and the blade's own protections. Every port does the same during
a controller restart.

## Bus voltage

The controller card watches the DC input: a 120 kΩ / 10 kΩ divider (100 nF
across the bottom leg) brings VIN to GP28/ADC2, so 3.3 V full scale is
42.9 V and the bus's 33 V clamp can never take the pin past the rail. Core 0
takes eight conversions every 100 ms and reports a one-second average,
about 10 mV a count. The ADC's reference is the card's 3.3 V rail and the
divider uses 1 % parts, so the reading can be off by about 2 %. To correct
it, measure the bus with a meter and run `vin cal 24.13` (then `save`) to
store the gain trim that makes them agree, in the `vin_cal` setting
(permille, 900 to 1100; `vin cal reset` clears it). The reading is `vin_v` in the status
JSON and the MQTT status, a *Bus voltage* sensor in Home Assistant,
`pwrman_bus_volts` on `/metrics`, part of the chassis summary on the page,
`vin` and a line in `info` and `status` on the console. The hardware is
rated for 20 to 28 V and the backplane opens the bus near 17.7 V (and closes
it again near 19.3 V), so a bus under 19 V or over 29 V (half a volt of
hysteresis either way) raises the problem indicator and logs a line; the
crossing of the 19 V flag, either way, also goes into the fault log, since
the cut a volt further down restarts the controller and would otherwise
leave no trace. The Pico 2 W carrier has no path from VIN to an ADC pin, so
there the monitor reports *not present* and every surface leaves it out.

**Bus-sag cap.** Before the bus gets near that cut, the firmware takes
load off it: while the bus reads under 20.0 V every port's advertised
current is capped at 3 A, chassis-wide, under whatever the port's own
limit is. No sink can then hold a 5 A PDO or APDO and no port delivers
more than 60 W; sinks that are attached are sent the new table and
renegotiate within it, and nothing is switched off. The cap comes off
once the bus has held 20.5 V for 30 s without a dip. At power-up the
first reading is judged against 20.5 V rather than 20.0 V, so a chassis
that has just been cut by the backplane and comes back on a marginal
supply brings its blades up under the cap and leaves it only once the
voltage has recovered, without immediately reapplying the full load. The
numbers are fixed, not settings: this is a safety cut-off (`src/bus_cap.c`). While it is on, the
problem indicator says so (`bus voltage sagging, 19.80 V, ports capped at
3 A`), `ceiling_ma` in the status JSON and MQTT status is 3000 (0
otherwise), the page's chassis summary and the console's `status` and
`info` note it, and a line is logged each way. A board without the
divider never caps.

## Fan

`auto` follows total chassis power with hysteresis (`fan auto [on_w off_w
[on_ma]]`, defaults 80/60 W, 30 s anti-flap hold) and also runs while any
port holds a contract over `on_ma` (default 3000 mA, so the everyday
5 V/3 A contract never trips it; 0 disables) — a 5 V/5 A contract is only
25 W of chassis load but heats the blade as I²R. A third rule reads the
gen-3 blades' thermometers: the fan runs while any blade's converter is
at or above 65 °C and that rule clears once every blade is under 55 °C.
Those two temperatures are fixed, sit well under the blade's own 100 °C
trip, and are provisional until the blades have been measured in the
closed chassis; a port without a reading (a gen-2 blade, an empty slot,
an open thermistor) takes no part. In auto the fan stays on until all
three rules are clear; `on`/`off` are manual overrides.

## Fault log

Faults and probe failures persist in the data partition (~256 records,
oldest dropped); `faults` lists them with power/contract at the moment of
the event and wall-clock time once SNTP has synced, and the same records
come out of `GET /api/v1/faults` and the page's *Fault log* panel. Every
boot adds a record saying why it happened, and the DC bus crossing its
19 V low flag adds one each way (`bus low: 18.70 V`), with the voltage.

## Wired Ethernet

The W6100 runs in MACRAW mode, as an ordinary lwIP netif, so
everything above it (DHCP, mDNS, MQTT, HTTP, OTA pull) is the same code as
over WiFi. Its MAC is locally administered, derived from the RP2350's
unique ID. When both links are up the wired one holds the default route and
WiFi stands by; replies always leave on the interface that owns their
address. `info` shows `eth:` link speed and address, and the status JSON
carries `"eth"`. The driver is verified against an emulated chip in the
host tests (`test_w6100.c`) and has run on a WIZnet W6100-EVB-Pico2 in the
management socket: link, DHCP, mDNS and the web UI over a cable. The
wired-beside-WiFi switchover has so far only run in the host tests, since
the EVB has no radio.

## UPS supply

A Mean Well LAD-xxxU security/UPS power supply (the `U` variants have the
serial port; the plain ones only have open-collector status pins) plugs
into the controller card's UPS header: 3.3 V TTL UART0 on GP12/GP13
through the card's 1 kΩ series resistors, 9600 8N1, grounds common through
the supply's V−. The link is the LAD manual's own frame — read 0x55 / write
0xAA, a length byte, a 16-bit address, data, CRC-8 (polynomial 0x07,
checked against the manual's worked examples) — not Modbus. The firmware
probes the header every 5 s until something answers, then reads the status
word, AC voltage, load current and battery voltage every second and the
per-block voltages and undervoltage cutoff every ten, spacing requests the
20 ms the supply asks for and giving up on a reply after 100 ms; five
missed replies in a row or five silent seconds mark it absent again. What
comes back is the `ups` block in the status JSON, the `ups_*` fields and
Home Assistant entities over MQTT, `pwrman_ups_*` on `/metrics`, the UPS
part of the chassis summary on the page, `ups` and a line in `info` on the
console, and log lines for AC power lost/restored, battery full and battery
faults. Running on battery, or a battery fault (missing, reversed,
under/overvoltage, unbalanced, discharge overload, a bad block), raises the
problem indicator. `ups buzzer off` silences the supply's alarm; the
setting lives in the supply and is lost when it restarts, as its manual
says of every write. The driver is tested against an emulated supply in
the host tests (`test_ups.c`) and is as yet untested with a real LAD
supply.

## Console log

Everything the firmware prints is mirrored into a 4 KB ring (boot banner,
link and broker events, settings saves, OTA progress, HTTP retries).
`GET /api/v1/log` and the page's *Console log* panel show it; `syslog
<host> [port]` (or `syslog_host` / `syslog_port`, or the page) ships every
complete line to a UDP syslog receiver as RFC 5424 (`<134>`, local0.info,
hostname = device name, timestamps once SNTP has synced) — including the
boot messages that were printed before the network came up, since the ring
holds them (the drain waits until the receiver's MAC is known, because lwIP
keeps a single packet per unresolved ARP entry; lines that waited in the
ring carry the time they were sent, not printed). A receiver that fell too
far behind gets one `log: N line(s) lost` line in place of what the ring
dropped. What is typed at the console is never mirrored, so a `wifi` or
`mqtt` line's password stays off the wire; the firmware's own output never
includes secrets. `info` shows the sink's state (off, resolving, the
address in use, or an unresolvable host).

## Boot reason

The banner, `info`, the status JSON (`boot`) and Home Assistant report why
the controller last started: power-on, brown-out, RUN pin, debugger,
watchdog timeout, a requested reboot, a firmware update, a trial image
reverting, a plain warm reset (a debugger's SYSRESETREQ), or a HardFault
with the core, PC, LR and CFSR that a probe would otherwise have been
needed for. Deliberate reboots and the fault handler stamp the watchdog
scratch registers before the reset, every boot leaves a sentinel there, and
the rest comes from the chip's reset-cause bits (which a SYSRESETREQ does
not update, hence the sentinel).

## Not yet implemented

- Front-panel display (planned as another consumer of the telemetry snapshot)
