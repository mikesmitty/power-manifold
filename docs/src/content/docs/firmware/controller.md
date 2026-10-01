---
title: Controller firmware
description: "Native pico-sdk firmware for the V2 management controller: building, flashing, updates, first-time setup, and management surfaces."
sidebar:
  order: 1
---

Native pico-sdk firmware for the V2 management controller, supervising up to
six charger blades of either generation — the MPQ4242 gen-2 blade and the
STM32G071 gen-3 blade — through the backplane TCA9548A I2C mux and TCA9539
GPIO expander.

Development target: a Raspberry Pi Pico 2 W (RP2350) on the
`hardware/pcie-breakout` carrier in the backplane's management socket.
Production target: the `hardware/controller` card for the same socket, an
RP2350A with a WIZnet W6100 wired-Ethernet controller and a Raspberry Pi RM2
radio module (the same CYW43439 as the Pico 2 W, so the WiFi and Bluetooth
stack carries over).

The [documentation site](https://mikesmitty.github.io/power-manifold/)
carries this guide alongside the hardware architecture and a hosted Wi-Fi
provisioner.

## Architecture

Two cores, one rule: **only core 1 touches the backplane.**

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
  address), MQTT with Home Assistant discovery, embedded web UI + JSON API
  and a Prometheus endpoint, USB CDC maintenance console mirrored to a log
  ring and optionally a syslog host, Improv Wi-Fi provisioning over BLE
  (BTstack on the same CYW43).
- **Between them** (`src/ipc.c`): a command queue, an event queue, and a
  seqlock telemetry snapshot. Every management surface is a thin transport
  over the same command/telemetry interface.

The hardware watchdog is fed only while both cores make progress.

## GPIO map

Two boards run this firmware, and `src/pins.h` carries a map for each: the
Pico 2 W on the pcie-breakout (the default `pico2_w` build, also the
W6100-EVB-Pico2 in the same socket) and the production controller card
(`PICO_BOARD=pwrman_controller_card`, see [Boards](#boards)).

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
| BUTTON | GP22 (a wire to GND) | GP22 | front-panel button, see [Front-panel button](#front-panel-button) |
| VIN_SENSE | — | GP28 / ADC2 | DC bus voltage through 120 kΩ / 10 kΩ, see [Bus voltage](#bus-voltage) |
| RM2 radio | — | GP23/24/25/29 | the Pico 2 W's own CYW43 wiring, so the WiFi and BLE code carries over unchanged |

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
An expander that merely fails to answer is not reset for it: the mux is
(after half a second; that frees a blade segment holding the bus and
costs no port anything), and only after five seconds out of reach is the
expander itself reset and its EN pattern written straight back — see
[Keeping ports powered](#keeping-ports-powered).

Behind the mux and the expander, port *n* (1-based wherever the firmware
talks to a person) is mux channel *n*−1, EN on P0(*n*−1), PRSNT# on
P1(*n*−1) and pixel *n*−1 of the chain, and the backplane wires all four
to one socket, in silkscreen order from the management socket toward the
power input (sockets J3, J5, J7, J4, J6, J8 after the re-annotation; the
[architecture](../../hardware/architecture/#31-slot-numbering-verified-against-the-netlist-2026-09-09)
page has the table). Verified against the backplane netlist and layout on
2026-09-09. Facing the front panel, port 1 is on the left next to the
management card and port 6 on the right, each pixel below and to the right
of its port; the power-up sweep runs left to right.

## Building

Requires the [pico-sdk](https://github.com/raspberrypi/pico-sdk) (2.x) with
its `lib/btstack` submodule checked out (BLE provisioning), an
`arm-none-eabi` toolchain, and Python 3 (BTstack's `compile_gatt.py` turns
`src/net/improv_profile.gatt` into a header at build time).

```sh
export PICO_SDK_PATH=~/.pico-sdk/sdk/2.2.0   # or wherever the SDK lives
cmake -B build -G Ninja
ninja -C build
```

Flash `build/controller.uf2` over BOOTSEL, or `picotool load -f
build/controller.uf2`.

The image carries the gen-3 blade firmware it programs blades with (see
[Blade firmware updates](#blade-firmware-updates)): `-DBLADE_IMAGE=<path>`
names the `charger-module.bin` to bundle, and without it the build takes a
sibling `../charger-module/build/charger-module.bin` when one is there
(build `firmware/charger-module` first). A build with neither still starts
blades that hold a good image, but cannot program a blank one, and says so
at configure time.

### Boards

| `PICO_BOARD` | Board | Flash | Partition layout |
| --- | --- | --- | --- |
| `pico2_w` (default) | Pico 2 W on the pcie-breakout | 4 MB | `pico2w-4mb.json` |
| `wiznet_w6100_evb_pico2` | WIZnet W6100-EVB-Pico2 in the same socket, no radio | 2 MB | none (runs unpartitioned) |
| `pwrman_controller_card` | the production controller card, `hardware/controller` | 16 MB | `prod-16mb.json` |

The pico-sdk knows the Pico 2 W; `boards/` carries the other two. The EVB's
header mostly exists to declare its 2 MB flash so the settings and
fault-log sectors land inside the chip. The card's header declares its
16 MB W25Q128, the RM2 radio on the Pico 2 W's CYW43 pins, and defines
`PWRMAN_CONTROLLER_CARD`, which switches `src/pins.h` to the card's
[GPIO map](#gpio-map) and the inverted reset drivers; the build picks the
16 MB partition layout for it. Pins were checked against the card's KiCad
netlist; the card itself has not been fabricated yet.

```sh
cmake -B build-card -G Ninja -DPICO_BOARD=pwrman_controller_card
ninja -C build-card
```

Bench shortcut for the Pico 2 W carrier: `-DCARRIER_INTERNAL_PULLUPS=ON`
swaps the three 4.7 kΩ resistors the breakout needs on SDA, SCL and
ALERT# for the RP2350's own pad pull-ups (50–80 kΩ) and slows the bus to
100 kHz so their slow edges fit inside the clock's low period. Once a mux
channel is open, that blade segment's 4.7 kΩ pull-ups on the backplane
pass through the switch and stiffen the upstream bus too. It is a
one-or-two-blade bench build: at 100 kHz six powered blades' per-tick
reads no longer fit the 10 ms engine tick. Refused for the controller
card, which carries the resistors.

```sh
cmake -B build-pull -G Ninja -DCARRIER_INTERNAL_PULLUPS=ON
ninja -C build-pull
```

### Network options

| Option | Default | Effect |
| --- | --- | --- |
| `NET_WIFI` | ON | CYW43 WiFi + Improv BLE provisioning; needs a CYW43 board (`pico2_w`, the card) |
| `NET_ETH` | ON | W6100 wired Ethernet on SPI0 GP16–21, probed once at boot |

Both on is the production shape (RM2 radio + W6100). A Pico 2 W with nothing
on GP16–21 logs `eth: no W6100 answering` at boot and runs WiFi-only. For a
board with no radio, such as the W6100-EVB-Pico2, build a wired-only image
(no cyw43 or BTstack blobs, about 230 KB):

```sh
cmake -B build-eth -G Ninja -DPICO_BOARD=wiznet_w6100_evb_pico2 -DNET_WIFI=OFF
ninja -C build-eth
```

With 2 MB there is no room for the A/B partition layout, so the EVB runs the
image unpartitioned (`boot: slot raw`) and OTA is unavailable on it; flash it
over SWD or BOOTSEL. Give the W6100 its time after reset: the driver waits
100 ms before the first register read, because at 10 ms the chip does not
answer yet.

### Fake-blade mode (no backplane needed)

`-DFAKE_BLADES=ON` swaps the five I2C drivers for a simulated backplane
(`src/engine/sim/`) and drives it through a repeating 60-second demo script —
attaches, budget contention with priority shedding, an over-current fault and
recovery, blade insertion/removal, with gen-3 blades in ports 4 and 5 and
gen-2 ones elsewhere (a fake-blade build bundles no blade firmware: its
simulated gen-3 blades take the boot-option trip through their bootloader
and are started on what they hold). Everything above the driver seam (state
machine, budget arbiter, MQTT/HA, web UI, CLI) is the real code, so the whole
management plane can be exercised on a bare Pico 2 W:

```sh
cmake -B build-fake -G Ninja -DFAKE_BLADES=ON
ninja -C build-fake
```

The console's `sim` command drives the simulated backplane by hand, so the
fault paths — the fault log, the *Problem* sensor, events, auto-recovery,
charge-complete — can be exercised on a bare board instead of only in the
host tests. `sim pause` stops the demo script first (it would otherwise
overwrite injected state within seconds; `sim run` restarts it from its
baseline), then `sim seat|unseat <n>`, `sim gen <n> 2|3` (which blade
generation the slot holds, before seating it), `sim attach <n> <mV> <mA>`,
`sim detach <n>`, `sim load <n> <pct>` (measured draw as a percentage of
the contract current — `sim load 3 1` with `charged 20000 1` shows a
charge-complete within a minute), `sim fault <n> ocp` (an over-current
trip: the INA226 on a gen-2 blade, the VBUS switch on a gen-3 one),
`sim fault <n> otw1|ntc1|cc|...|clear` (MPQ4242 fault bits, sticky until
cleared), `sim fault <n> ovp|conv-ocp|plug-hot|pd|...|clear` (a gen-3
blade's faults, held as a condition that re-latches until cleared),
`sim restart <n>` (a gen-3 blade's MCU restarts and loses its
configuration), `sim temp <n> <conv> <plug>` / `sim temp <n> auto` (pin a
gen-3 blade's thermometers in °C, or hand them back to the model that
warms them with the load), `sim probe <n> ina|mpq|blade|ok` (the next
probes find a silent chip), and `sim mux fail|ok` / `sim expander fail|ok` for the
bus-level failures
the engine recovers from by resetting the mux and expander. `sim` alone
prints the list; on a real-blade build the command says so and does
nothing.

### Host-side tests

The engine core (state machine + budget arbiter) is hardware-free and runs
natively against the same simulated drivers, and so does everything else
with a seam under it (the settings store against a flash in RAM, the bus
monitor against an emulated ADC, the button, the UPS protocol):

```sh
cmake -S test -B test/build
cmake --build test/build
ctest --test-dir test/build --output-on-failure
```

CI runs these on every push/PR touching the firmware.

## Flash layout & updates

The flash is carved up by an RP2350 partition table — two A/B image slots the
bootrom picks between, plus a `data` partition holding persistent settings
and the fault log. Layouts live in `partitions/*.json`, one per
board; the build compiles the selected one (`PARTITION_TABLE_JSON`, default
`pico2w-4mb.json`) into `build/partition_table.uf2`:

| Offset | Size | Partition |
| --- | --- | --- |
| `0x000000` | 4K | partition table |
| `0x002000` | 1536K | `A` — image slot |
| `0x182000` | 1536K | `B` — image slot |
| `0x302000` | 1016K | `data` — settings ping-pong pair in the first two sectors |

The firmware never hardcodes these offsets: it looks partitions up **by ID**
through the bootrom at boot, so the same binary runs on any layout. The
16 MB map for the controller card (`prod-16mb.json`, the default for that
board) makes the image slots 4 MB each, adds a 7 MB `assets` partition, and
leaves the top 4 KB sector unpartitioned, which picotool insists on for the
RP2350-E10 erratum workaround. Boards with no partition table at all still
work — settings fall back to the legacy top-of-flash sectors and `info`
reports `slot raw`.

**One-time install** (per board): enter BOOTSEL and drag
`partition_table.uf2`, then `controller.uf2`. The bootrom routes the app UF2
into an image slot by itself. Settings saved by older raw-layout firmware are
found and migrated into the data partition on first boot.

**Updates**: dragging a newer `controller.uf2` in BOOTSEL lands in the
*inactive* slot, and the bootrom boots whichever slot holds the higher
image version (wired to `FW_VERSION`, so releases order themselves). Caveat
for local iteration: two builds with the *same* version tie-break to slot A —
either bump `FW_VERSION` locally or target a slot explicitly with
`picotool load -f -p <0|1> build/controller.uf2`.

**OTA**: push a firmware image to the update endpoint over the LAN — the
release `controller.uf2` and the raw `controller.bin` both work:

```
curl --data-binary @controller.uf2 http://<name>.local/api/v1/update
```

(add `-H "Authorization: Bearer <token>"` if an API token is set). The body
streams into the *inactive* slot with the try-before-you-buy flag forced on
the written image, gets verified by read-back, and the controller then
reboots into it as a trial. A half-finished or failed upload leaves nothing
bootable behind — the slot's first sector is erased before the transfer and
written last.

The same pipeline also *pulls*: `update <http-url>` on the CLI fetches an
image over plain HTTP (serve it from any LAN box, `python3 -m http.server`
included), and Home Assistant gets an update entity — publish a retained
release pointer to `pwrman/<name>/update/latest` as
`{"version":"x.y.z","url":"http://.../controller.uf2"}` and HA shows the
update and installs it with one click.

**Try-before-you-buy**: a TBYB-flagged image boots as a *trial* — `info`
shows `slot B (TRIAL, uncommitted)` — and commits itself only after 10 s of
continuous health (engine heartbeat, network up if one is configured). Until
then any reboot, watchdog bite, or the 10-minute deadline reverts to the
previous image, so a broken OTA push heals itself.

**Settings across a revert.** The settings record only ever grows: a new
layout appends its fields after the old ones, so a record written by any
later firmware begins with everything an earlier one knows. Since 0.11 each
record also states its own length, which is where its crc sits, and a
firmware that meets a record of a layout it has never seen checks that crc
and reads the part it understands. The image a failed trial reverts to
therefore comes back with the WiFi, the names and the limits it had; its
next save writes its own layout, and the newer firmware defaults its own
fields again when it returns, as after any upgrade. An image on trial also
keeps every save to one of the two sectors, so the record the previous
firmware wrote is still there if the trial is reverted, and a save cut
short by a reset cannot take it. What a revert loses is at most what was
changed during the trial. Firmware before 0.11 reads no layout newer than
its own, so for the first update from it the one-sector rule is what keeps
the configuration: it finds its own record in the other sector.

Version ordering: pushing a **newer** build sticks by version comparison, and
pushing a strictly **older** one sticks too — the bootrom records the
deliberate downgrade and erases the newer slot's image when the trial commits,
which is the rollback path. Only pushing the **same** version doesn't reliably
survive a power cycle (ties break to slot A); that's the local-iteration case,
so bump `FW_VERSION` or use `picotool load -f -p <0|1>` at the bench.

## First-time setup

Two ways onto the WiFi. Over Bluetooth, with nothing plugged in: an
unprovisioned controller advertises the standard
[Improv Wi-Fi](https://www.improv-wifi.com/ble/) BLE service, so the Home
Assistant companion app (Settings → Devices → Add → *Improv via BLE*, or
the notification it raises when it spots one) or the
[hosted provisioner](https://mikesmitty.github.io/power-manifold/setup/wifi-provisioning/)
(Web Bluetooth, so a Chromium browser; improv-wifi.com's page works the
same way) hands it the SSID and password. *Identify* flashes the status LEDs so you know which box you're
talking to; on success the credentials are saved and the client is
redirected to the web UI, whose *Settings* panel finishes the job from the
same phone: MQTT broker, device name, and an API token. The redirect
carries a one-shot setup secret (`http://<ip>/?s=…`) that unlocks the
panel for 10 minutes while no token exists yet; the panel makes you choose
a token, which locks the API and retires the secret. *Reboot* in the same
panel applies the name and broker. Or over the USB console (any serial
terminal, 115200):

```
wifi <ssid> <password>
mqtt <broker-host> [port user pass]
name pwrman
token <t>
save
reboot
```

A box provisioned this way (or a wired-only one that simply took a DHCP
lease) has no setup secret: set a `token` on the console and the *Settings*
panel unlocks with it. `help` lists everything else; see
[Console](#console) for the full set.

BLE only ever carries the WiFi credentials, and only while a provisioning
window is open: automatically while the device has no credentials or has
been off the network for 5 minutes, and on request for 10 minutes via
`improv on` on the console or the *Open BLE provisioning* button in Home
Assistant (`improv off` closes it and holds the automatic windows until
the next `improv on` or reboot). Outside a window the Bluetooth controller
is powered down. A join that is rejected or gets no address within 30 s
reports *unable to connect* and the previous credentials come back. The
GATT traffic is plaintext, same trust posture as the rest of the
management plane (a trusted LAN, and now radio range of a box you can
press buttons on). `info` and the status JSON (`"ble"`) show the window
state.

## Management surfaces

Every surface is a thin transport over the same command queue and telemetry
snapshot, so whatever one of them can do the others can too: the web page,
the JSON API, MQTT with Home Assistant discovery, the USB console and a
Prometheus scrape. Mutations over HTTP take a Bearer token once `token` is
set, MQTT trusts the broker, and the console trusts whoever holds the cable.

### Web UI

`http://<name>.local/` is one page served from the firmware. The port table
shows each port's label, state (with *charged* once a sink has finished),
contract, measured draw *and* the budget reservation held against it (an
idle powered port draws 0 W but still reserves its 15 W base), priority and
power-up policy; the chassis line below it carries total, reservation,
budget, fan, link, a note when the LEDs are dimmed and the UPS supply's
state when one answers, and any problem shows in red under that. Click a port row for sparklines of its last 10 minutes of
W / A / V, sampled from the page's own 1 Hz poll (history lives in the tab,
so it starts when the page opens — Home Assistant keeps the long-term
record). Further down: the *Fault log* panel, the *Console log* panel (the
last 4 KB the firmware printed) and the *Settings* panel — device name,
broker, API token, chassis budget, fan policy, status LEDs and their
schedule, timezone offset, addressing, syslog host, charge-complete
thresholds, and per port a name (up to 23 characters; blank means `Port N`,
and the label shows in the table, the console and Home Assistant), a
current limit (500 to 5000 mA: the current field of every PDO the port
advertises, so the wattage ceiling scales with the voltage the device
picks, except that no PDO promises more than 100 W: the 21 V PPS range
alone stops at 4.75 A; a live port renegotiates at once, and on a gen-2
blade the INA226 emergency trip
stays at 125 % of the blade's 5 A ceiling regardless), a voltage cap (the
highest PDO the port advertises: 5, 9, 12, 15 or 20 V, the last being the
whole table; fixed PDOs above the cap and PPS ranges reaching past it are
withheld, so a 5 V cap makes a legacy-safe port and 9 V keeps a phone off
its 12 V step), the state at power-up, *off when charged* and a sleep
timer. *Export* downloads the
settings as JSON and *Import…* posts one back (see [Backup](#backup)). The
panel unlocks with the API token, or with the one-shot setup secret from an
Improv redirect while no token exists yet.

### HTTP API

| Method and path | Auth | Purpose |
| --- | --- | --- |
| `GET /api/v1/status` | none | Everything the page shows: per port `name`, `state`, `gen` (the blade generation, 0 until probed), `v` / `i` / `p` / `e`, `pdo`, `contract_w`, `prio`, `limit_ma`, `max_v`, `boot`, `attached`, `charged`, `fault` (the port fault bits, see [Blade generations](#blade-generations)), `t_conv` / `t_plug` / `t_mcu` (a gen-3 blade's thermometers in °C, `null` without a reading), `progress` (how much of a firmware image has been written while the state is `updating`, 0–100), `update_due` (the blade's firmware will be updated once the port is empty), `silent` (the port is powered and its blade is not answering; the readings are its last); chassis `blade_fw` (the gen-3 blade firmware this build carries, `null` without one), `total_w`, `reserved_w`, `budget_w`, `headroom_w`, `energy_kwh`, `fan` / `fan_mode`, `alert`, `rssi`, `eth`, `ble`, `uptime_s`, `fw`, `slot`, `trial`, `boot` (the reason for the last boot), `warm_start` (the ports kept their power through it), `vin_v` (the DC bus voltage, `null` on a board without the divider), `problem` / `problems`, `led_mode` / `led_now`, and a `ups` object (`present`, and with a supply answering `ac`, `on_battery`, `charging`, `full`, `fault`, `mains_v`, `batt_v`, `load_a`, `uvp_v`, `cells`, `status`) |
| `GET /metrics` | none | Prometheus text exposition: the chassis gauges, a `pwrman_info` line with firmware, slot and boot reason, `pwrman_bus_volts` / `pwrman_bus_voltage_ok` on the controller card, the `pwrman_ups_*` gauges while a UPS answers, every port metric labelled `port` and `name`, and `pwrman_port_temperature_celsius` with a `sensor` label (`converter`, `plug`, `mcu`) for each reading a gen-3 blade gives |
| `GET /api/v1/faults[?offset=N]` | none | The fault log newest first, eight records a page, each with a human `text` |
| `GET /api/v1/log` | token | The console's last 4 KB as text; gated like the mutations because it names networks and hosts |
| `GET /api/v1/settings` | token or setup secret | Every setting except the secrets, with `mqtt_pass_set` / `token_set` flags in their place |
| `GET /api/v1/settings/export[?secrets=1]` | token or setup secret | The same object with every setting, plus `format` and `fw`, ready to be posted back |
| `POST /api/v1/settings` | token or setup secret | Any subset of the keys below, saved to flash at once; the reply says whether a reboot is needed |
| `POST /api/v1/port/<n>` | token | `{"action":"enable"}`, `"disable"`, `"hard_reset"`, `"src_cap"` or `"update"` (write the bundled gen-3 blade firmware over whatever the blade runs) |
| `POST /api/v1/fan` | token | `{"on":true}`, `{"on":false}` or `{"mode":"auto"}` |
| `POST /api/v1/budget` | token | `{"watts":N}` |
| `POST /api/v1/faults/clear` | token | Wipes the fault log |
| `POST /api/v1/reboot` | token | Reboots after flushing any pending settings save |
| `POST /api/v1/update` | token | OTA push, body = firmware image (see [Flash layout & updates](#flash-layout--updates)) |

*Token* means a Bearer token once `token` is set and nothing before that;
`/settings` always wants one — the token, or the setup secret from an Improv
redirect while none is stored. The settings keys are `name`, `wifi_ssid`,
`wifi_pass`, `mqtt_host`, `mqtt_port`, `mqtt_user`, `mqtt_pass`, `token`,
`budget_w`, `fan_mode`, `fan_on_w`, `fan_off_w`, `fan_on_ma`,
`led_brightness`, `led_boot`, `led_dim`, `led_night`, `led_idle_min`,
`tz_offset_min`, `ip_mode`, `ip`, `netmask`, `gateway`, `dns`,
`syslog_host`, `syslog_port`, `charged_mw`, `charged_min`, `vin_cal`, and
the six-element arrays `port_names`, `port_limits_ma`, `port_max_v` (5, 9,
12, 15 or 20), `port_priorities`, `port_boot`, `port_auto_off` and
`port_sleep_min`. Budget, fan, LEDs, names, limits, voltage caps,
priorities, power-up policy, charge thresholds, DNS and syslog apply live;
the name, WiFi, broker and addressing wait for
`POST /api/v1/reboot`.

### MQTT

Point the firmware at a broker with `mqtt <host> [port user pass]` or the
settings panel. Everything lives under `pwrman/<name>/`:

| Topic | Direction | Payload |
| --- | --- | --- |
| `availability` | published, retained | `online`, and `offline` by LWT |
| `status` | published at 1 Hz, retained | chassis `total_w`, `reserved_w`, `budget_w`, `headroom_w`, `energy_kwh`, `fan`, `fan_mode`, `alert`, `rssi`, `uptime_s`, `led`, `fw`, `boot`, `problem`, `problems`, `charged_mw`, `charged_min`, `led_mode`, and the UPS supply's `ups` (present), `ups_ac`, `ups_on_battery`, `ups_charging`, `ups_batt_v`, `ups_mains_v`, `ups_load_a`, and the bus voltage `vin` (fitted), `vin_v` |
| `port/<n>/telemetry` | published at 1 Hz | `state`, `gen`, `v`, `i`, `p`, `e`, `pdo`, `contract_w`, `prio`, `limit_ma`, `max_v`, `boot`, `charged`, `auto_off`, `sleep_min`, `fault`, `last_fault`, `last_fault_at`, `t_conv` / `t_plug` / `t_mcu` (°C, `null` without a reading), `progress` (0–100 while `updating`), `update_due`, `silent` |
| `event` | published as they happen | `{"port","event","kind","code","arg","text","ts"}` — `kind` is the Home Assistant vocabulary listed below, `text` is filled for faults, probe failures and auto-off |
| `update/state` | published, retained | `installed_version` and `latest_version` |
| `update/latest` | subscribed, retained | the release pointer `{"version":"x.y.z","url":"http://…/controller.uf2"}`, published by CI or by hand |
| `port/<n>/set` | subscribed | `ON`, `OFF`, `hard_reset` or `src_cap` |
| `port/<n>/priority/set` | subscribed | 0–255, 0 = highest |
| `port/<n>/limit/set` | subscribed | 500–5000 mA |
| `port/<n>/volt/set` | subscribed | voltage cap in volts: `5`, `9`, `12`, `15` or `20` (`9 V` works too) |
| `port/<n>/boot/set` | subscribed | `on`, `off` or `last` |
| `port/<n>/autooff/set` | subscribed | `ON` or `OFF` |
| `port/<n>/sleep/set` | subscribed | minutes, 0 = off |
| `charged_mw/set`, `charged_min/set` | subscribed | the charge-complete thresholds |
| `budget/set` | subscribed | watts |
| `fan/set` | subscribed | `auto`, `on` or `off` |
| `led/set` | subscribed | brightness 0–255 |
| `improv/set` | subscribed | `open` — a ten-minute BLE provisioning window |
| `update/set` | subscribed | `install` — pull the `update/latest` URL into the inactive slot |

Settings changed over MQTT persist automatically a few seconds after the
last change.

### Home Assistant

Discovery is automatic and one device carries everything. Entity names
carry the port's label, so `Port 3 power` becomes `Desk phone power` after
a rename; a rename re-publishes discovery and the entity ids stay put.

Per port:

- power, voltage, current and state sensors, and a since-boot energy sensor
  (`total_increasing`, energy-dashboard ready); the state sensor carries
  `last_fault` / `last_fault_at` attributes (the newest fault or probe
  failure, as text and epoch seconds);
- an enable switch, hard-reset and re-announce-caps buttons;
- priority and current-limit numbers, a *voltage cap* select (5 to 20 V),
  a power-up state select (on/off/last), an *off when charged* switch and
  a *sleep timer* number;
- a *charging* binary sensor (device class `battery_charging`: a sink is
  attached and not yet charged);
- on a port holding a gen-3 blade, *converter temperature* and *plug
  temperature* sensors (the entities appear once the blade has been
  probed, are retracted when a gen-2 blade takes the slot, and read
  *unknown* while the port is off);
- an *events* entity fed from `.../event` — event types `inserted`,
  `ready`, `attached`, `detached`, `removed`, `enabled`, `disabled`,
  `contract`, `throttled`, `restored`, `fault`, `probe_failed`, `charged`,
  `charging`, `auto_off` and `updated` (a gen-3 blade's firmware was
  written, the version in `text`), with the raw `code` / `arg` and the
  fault, auto-off or update `text` as attributes, so an automation triggers on
  `event.<label>_events` directly instead of templating over the state
  sensor.

Chassis:

- total power, budget headroom and total energy sensors;
- a power-budget number, a fan select (auto/on/off), an LED brightness
  number, and *Charged below* (mW) and *Charged after* (min) numbers;
- diagnostic sensors for the last boot reason and the LED mode, and a
  *Problem* binary sensor whose `detail` attribute names what needs
  attention;
- an *Open BLE provisioning* button, and a firmware update entity fed from
  the retained `update/latest` pointer whose Install pulls the URL into the
  inactive slot;
- on the controller card, a *Bus voltage* sensor (the DC input, from the
  card's divider; retracted on a board without one);
- while a UPS supply answers on the UPS header: *UPS AC input* (device
  class `plug`), *UPS on battery* and *UPS charging* binary sensors, and
  UPS battery voltage, mains voltage and load current sensors. They are
  published when the supply first answers and retracted when it is absent,
  so a chassis without one shows none.

### Console

USB CDC at 115200 from any serial terminal, or RTT through a debug probe.
`help` prints the list; on a fake-blade build `sim` adds the fault injection
commands described [above](#fake-blade-mode-no-backplane-needed).

| Command | Purpose |
| --- | --- |
| `status` | port table (state — `upd NN%` while a blade is being written — contract, draw, current limit, voltage `cap`, priority, `boot` policy, `chg` once charged, a gen-3 blade's `conv` and `plug` temperatures) and chassis power |
| `info` | firmware, slot and boot reason, links and addressing, UPS supply, bus voltage, broker, syslog sink, LED schedule and local time, problems, simulator state |
| `wifi <ssid> [pass]` | WiFi credentials |
| `improv [on\|off]` | BLE provisioning window |
| `mqtt <host> [port user pass]` | broker; an empty host disables MQTT |
| `ip dhcp` / `ip static <addr> <mask> <gw>` | addressing: the wired link if a W6100 is fitted, else WiFi |
| `dns <addr>\|auto` | resolver override |
| `syslog <host> [port]` / `syslog off` | mirror the console to a UDP syslog host |
| `name <device-name>` | hostname and topic id |
| `token <t>\|clear` | API bearer token |
| `budget <watts>` | chassis power budget |
| `port <n> on\|off\|reset\|srccap\|update` | port control; `update` writes the bundled gen-3 blade firmware over whatever the blade runs |
| `port <n> priority <0-255>` | 0 = highest; sheds from the bottom |
| `port <n> name <text>\|clear` | label for the web UI and Home Assistant |
| `port <n> limit <500-5000>` | advertised current ceiling in mA, every PDO |
| `port <n> volt 5\|9\|12\|15\|20` | voltage cap: the highest PDO advertised (20 = the whole table) |
| `port <n> boot on\|off\|last` | state at power-up |
| `port <n> autooff on\|off` | switch off once the sink is charged |
| `port <n> sleep <min>\|off` | switch off this long after a sink attaches |
| `charged <mW> <minutes>` | charge-complete thresholds; 0 mW switches detection off |
| `blades [auto on\|off \| bootopt on\|off \| watch <s>\|off]` | the bundled gen-3 blade firmware and the update policy (see [Blade firmware updates](#blade-firmware-updates)) |
| `fan on\|off\|auto [on_w off_w [on_ma]]` | fan policy |
| `led <0-255>`, `led boot white\|rainbow` | LED brightness and power-up sweep |
| `led dim <0-255>`, `led night <HH:MM> <HH:MM>\|off`, `led idle <minutes>\|off` | dimmed level, night window, idle dimming |
| `tz <+HH:MM\|-HH:MM>` | local time offset for the night window |
| `faults [clear]` | persistent fault log |
| `ups [buzzer on\|off]` | UPS supply readings, status bits, per-block voltages and link counters; `buzzer off` silences its alarm until the supply restarts |
| `vin [cal <volts>\|cal reset]` | DC bus voltage with the raw count and gain trim; `cal 24.13` trims the reading to a meter's (then `save`) |
| `button [short\|long]` | front-panel button input and state; `short` (wake the chain) / `long` (open BLE) act as if it had been pressed |
| `export` | every setting as JSON, without passwords |
| `update <http-url>` | OTA pull into the inactive slot |
| `stack` | per-core stack high-water marks |
| `i2c scan <ch\|none>`, `i2c read <ch> <addr> <reg> [n]`, `i2c write <ch> <addr> <reg> <val>`, `i2c en <port> on\|off` | bench access to the backplane bus, run on the engine core: scan a mux channel (`none` = the upstream side), read or write a register, drive a blade's EN; a healthy blade segment answers `0x40 0x61 0x70 0x74` |
| `save`, `defaults`, `reboot`, `bootsel` | settings and lifecycle |

What is typed at the console is never mirrored to the log ring or a syslog
host, so a `wifi` or `mqtt` line's password stays off the wire.

## Features

### Blade generations

Two charger blades fit the chassis, and a probe tells them apart by what
answers on the slot's mux channel: a gen-3 blade's register file at 0x3A
first, else a gen-2 blade's INA226 at 0x40. The port engine
(`src/engine/port_fsm.c`) does not care which; `src/engine/blade.c` puts
both behind one interface, and the status table, the JSON and the MQTT
telemetry carry the generation as `gen`. A chassis can hold a mix, and the
budget arbiter sheds and restores across generations by priority alone.

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
  [bootloader trip](#blade-firmware-updates) in the latter case) and is
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

### Blade firmware updates

A gen-3 blade is programmed in the chassis, over the backplane, by the
controller: the STM32G0's ROM bootloader speaks its I2C protocol (AN4221)
on the same pins as the blade's register file, at 0x51, and this firmware
carries the blade firmware it was built with (`BLADE_IMAGE`, [Building](#building);
`blades` on the console and `blade_fw` in the status JSON say which). The
blade side is in the [blade firmware's README](../charger-module/README.md#firmware-updates-over-the-backplane).

**When it happens.** The probe finds a blade in its bootloader — factory
blank, reset into it, or set to boot through it — and takes it through a
trip (`updating` in the state column, with a progress percentage): read
the bootloader's version and the chip's ID, read the image header out of
the flash, then either start what is there (it is the bundled version, and
the bootloader's checksum of the flash matches the bundle), or erase and
write the bundle, check it the same way and start it. That port is dark
already, so the trip costs nothing.

A blade *running* its firmware has to be sent to the bootloader, and that
takes its port down for the few seconds the trip lasts. The controller
wants it in two cases: the firmware is not the bundled version (`blades
auto on`, the default: the chassis keeps its blades on the firmware it was
tested with, downgrades included — turn it off to run a bench build on a
chassis, and then a blade in its bootloader is started on whatever good
image it holds), and its option bytes are still the factory ones (`blades
bootopt on`, the default), to have them programmed, once, so every reset
lands in the bootloader from then on. When both are wanted the option goes
first and the image is written under it, in the same trip. Both settings
ride along with the rest (`blade_auto_update`, `blade_boot_via_loader` in
the settings JSON).

Neither is worth a port's power, so **a port with something plugged in is
never taken down for them**. They are done

- at a probe that starts from a dark port — a blade just seated, the
  chassis powering up, a port switched on, the retry after a fault — before
  the port is given power, a sink already plugged in or not;
- once a powered port has had nothing plugged in for ten seconds.

A port that is charging something keeps charging on the firmware its blade
has, for days if that is how long the device stays: `status` marks it
`[blade update due when idle]`, the status JSON and the telemetry carry
`update_due`. `port <n> update` (the `update` action) is the way to say
"now": it rewrites the bundle onto the blade whatever is plugged in and
whatever the blade runs.

And nothing is done on the controller's own account **while its image is
on trial**. A controller update that carries a newer blade firmware
changes no blade until the trial has committed (ten seconds of health, see
[Flash layout & updates](#flash-layout--updates)); one that never gets there — a link
too poor to count as healthy, a crash — is reverted having touched
nothing, so the old image does not find blades it has to take back.
(`blades` says so while it lasts.) A blade found in its bootloader during
the trial is started on what it holds.

**Why the boot option.** With it, any reset — the watchdog, a crash, a
brown-out — puts the blade where the controller can reach it, and a
rewrite that is interrupted at any point just starts over. Without it the
way into the bootloader is the flash's empty flag, and the bootloader
clears that flag when it starts (AN2606, 48.3.1): a reset halfway through
a rewrite then boots a flash with no first page, and the blade stays dead
until it is reseated. That is why the option is set before the first
rewrite, and why the controller puts off its own restarts (an OTA reboot,
a trial revert) while a blade is mid-trip — the bootloader resets the
blade if the controller goes quiet for a second in the middle of a
command.

**The watch** (`blades watch`, `blade_watch_s`, default 120 s) is the way
to a blade whose firmware runs but has stopped answering. It is not a
timeout on the controller: a blade only resets into its bootloader when
its EN is low *and* it has not been addressed for that long. A controller
that is rebooting, updating or gone leaves EN where it was, and the ports
run on. `port <n> update` on a silent port is what uses it: EN goes low,
the blade finds its own way to the bootloader within the watch time, and
the retry after each cooldown picks it up there. Switching the port off
and on does the same, slower. `blades watch off` leaves only a reseat.

**What can go wrong.** Each trip that ends with a blade back in its
bootloader without its firmware having come up as wanted counts; after
three the port is held in FAULT (`probe: update (crash loop)`) and stays
there — no retry every cooldown — until the blade is reseated, the port
re-enabled, or `port <n> update` is given. A blade that *does* run but
cannot be brought to what was wanted (an option that will not take) is
not faulted for it: after three tries the controller logs `probe: update
(not taking)` once, leaves the port in service and stops asking until one
of those three things happens. A trip that fails outright (`probe: update (no image)` for
a blank blade in a build without one, `(write)`, `(verify)`, `(bootloader
silent)`, `(wrong chip)`) is a probe failure like any other, retried after
the cooldown within the same allowance. The image is erased a page at a
time, first page first, and written first page last, its header chunk
last of all, so a blade that loses power midway reads as blank next time
and is simply written again; a controller that reboots midway finds the
blade still in its bootloader and starts the trip over. (A page per erase
command with a pause after each is ST's workaround for bootloader V11.3,
which acknowledges an erase before the flash has finished; V11.4, the
current one, does not need it.) The work is sliced one step per engine
tick, so the other five ports keep being served; a whole image takes
well under ten seconds. Events: `update` with `written`, `started` or
`boot option` and the version in `text`; the HA events entity sees
`updated` when an image was written.

### Charge-complete and auto-off

While a sink is attached the engine watches its measured draw; once it has
stayed under `charged_mw` (default 500 mW, a full phone trickles well under
that) for `charged_min` (default 10) the port reads *charged* — `chg` in
the `status` attach column, `charged` in the status JSON and telemetry, a
`charged` event, the Home Assistant *charging* sensor going off, the port's
status LED turning solid magenta and the page's state cell saying so. A
device that starts drawing again for as long
reads as charging again (`charging` event), so a laptop draining while
plugged in is not mistaken for full; detach and re-attach start over.
`charged <mW> <minutes>` (or the settings keys, the page, or the two Home
Assistant numbers) tunes it, 0 mW switches detection off. Two per-port
auto-off policies build on it: *off when charged* (`port <n> autooff
on|off`, `port_auto_off`, the page's checkboxes, an HA switch) switches the
port off the moment it reads charged, and the *sleep timer* (`port <n>
sleep <minutes>|off`, `port_sleep_min`, the page, an HA number; up to 24 h)
switches it off that long after a sink attaches whatever the draw — a
bedside port that should never float a battery all night. Both end in the
ordinary disabled state (an `auto_off` event says which policy fired), so
the port comes back with `port <n> on`, the API, the page or the HA switch,
and a `last` boot policy remembers it as off.

### Port state at power-up

Each port has a boot policy — `on` (the default), `off` (stays disabled
until switched on), or `last` (comes back however it was last switched,
from the console, the API, the page or the Home Assistant switch; the
firmware records every switch and, under `last`, saves it a few seconds
later). Set it with `port <n> boot on|off|last`, the `port_boot` settings
array, the page's *Settings* panel, or the per-port Home Assistant select;
`status` shows it in the `boot` column. Before this, an HA-disabled port
came back enabled after a power cut.

### Staggered power-up

The blades found seated at boot are enabled one at a time, 250 ms apart, in
priority order (slot order among equals; boot-disabled ports hold no slot),
so six sinks do not inrush and negotiate on the DC input at once and the
highest-priority port claims the budget first. Blades seated later, and
ports switched on later, are immediate as before. Blades that are already
powered when the firmware starts (a [warm start](#warm-start)) have nothing
to inrush and are adopted ahead of the queue.

### Warm start

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
nobody was watching (an over-current trip, an MPQ4242 fault flag, a gen-3
blade's fault register) is treated as a
fault now — the usual path, EN off, cooldown, re-probe — and the blade's
configuration is compared, read-only, with the settings and rewritten
(and re-advertised to an attached sink) only when it differs, so a live
contract normally rides through untouched. The port then reports `active`
with its contract reserved in the budget as if nothing had happened. A
blade that does not answer stays powered all the same — it ran without
the controller until now — and is shown as silent and asked again every
second ([Keeping ports powered](#keeping-ports-powered)); a port whose
boot policy is `off` (or `last` with the port last switched off) is
switched off at once; an EN left on with no blade seated behind it is
dropped. The fan is adopted the same way, and the policy then decides
whether it stays on. Whether it is a warm start at all rests on one read
of the expander, so an expander that does not answer that read is asked
again for half a second before its silence is taken for a cold start.

What a warm start cannot do is make up for the time the controller was
down: there is no budget arbitration and no firmware fault response while
it is away — only the blades' own current and thermal limits — and the
charge-complete hold and sleep timers restart at the reboot. The console
logs `engine: warm start, ports 1 and 3 kept powered` (or `cold start`),
`info` repeats it, and the status JSON carries `warm_start`. The
Pico 2 W carrier and the controller card both behave this way; it is
exercised against the simulator in the host tests and still to be
confirmed on a live backplane.

### Keeping ports powered

A port that is delivering power keeps delivering it unless something
requires otherwise. What does: a fault the blade or the controller's own
meter reports, the blade being pulled, the port being switched off (by
hand, by its sleep timer or charged-off rule), and `port <n> update`.
The budget never cuts a port; it lowers what the port advertises.

What does not, and what the firmware does instead:

- **The controller restarting**, for any reason and for however long: a
  warm start, above. The blades' own watch does not count the controller's
  silence against a port whose EN is high.
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
  port ([Blade firmware updates](#blade-firmware-updates)).
- **A controller update that does not stick**: a trial image changes
  nothing about a blade.
- **One bad read.** A presence read that says a seated blade has left is
  taken again before it is believed; an expander whose configuration word
  cannot be read is not treated as one that lost it; an expander that does
  not answer is left holding its EN lines while the mux is reset, and is
  itself reset (EN pattern written straight back) only after five seconds
  out of reach.

The price is a port that may run for a while with nobody watching it, on
the limits it was last given and the blade's own protections — which is
what every port does whenever the controller restarts.

### Status LEDs

One WS2812 per slot behind the front-panel light pipes. Dim white empty,
cyan blink probing, amber idle, blue (below 19 V) or green (19 V and up)
active, the same colour pulsing at 1 Hz when throttled, solid magenta once
the attached sink reads *charged* (see [Charge-complete and
auto-off](#charge-complete-and-auto-off)), red blink at 5 Hz on a fault,
off when disabled. A short comet crosses the chain every 3 s
while something needs attention: blue while the BLE provisioning window is
open, white while no link has an address. Power-up runs a sweep across the
six pixels (`led boot white|rainbow`), which also proves the chain order.
A short press of the front-panel button flashes the whole chain white for
a moment and wakes it to full brightness for 30 s; while the button is
held past its long-press point the chain fills in blue toward the factory
reset and turns all red as it fires (see [Front-panel
button](#front-panel-button)). Both show even on a blanked or dimmed chain.
`led 0` blanks the chain but a faulted port keeps blinking at a floor
level; brightness is also a Home Assistant number and a field in the web
*Settings* panel. Two schedules drop the chain to a dimmed level (`led dim
<0-255>`, default 4, just visible in a dark room): a night window in local
time (`led night 22:00 07:00`, may wrap midnight; `led night off`) and idle
dimming (`led idle 30`: no port event — plug, unplug, fault — for that
long; any event brings full brightness back, and a tap of the front-panel
button brings it back for 30 s from either schedule). Local time is SNTP's UTC plus
`tz <+HH:MM|-HH:MM>` (there is no timezone database, so adjust it at DST
changes); until the clock has synced the night window is ignored and idle
dimming still works. `info` shows the schedule, the current mode and the
local time; the status JSON carries `led_mode` / `led_now`, the page notes
a dimmed chain in its chassis line, Home Assistant gets a diagnostic *LED
mode* sensor, and the settings keys are `led_dim`, `led_night`
(`"HH:MM-HH:MM"` or `""`), `led_idle_min` and `tz_offset_min`.

### Addressing

DHCP on every link by default. `ip static <addr> <netmask> <gateway>` (or
the page's *Addressing* block, or the `ip_mode`/`ip`/`netmask`/`gateway`
settings keys) gives the box a fixed address: it goes on the wired link
when a W6100 is fitted and on WiFi otherwise — never both, so WiFi standing
by behind a cable stays on DHCP. `dns <addr>` sets the resolver in either
mode and always wins (DHCP rewrites the servers on every renewal, so the
firmware puts the configured one back); with none set, static mode resolves
through the gateway. Addressing applies at the next boot, DNS at once;
`info` shows the mode, netmask, gateway and resolver in use.

### Backup

`GET /api/v1/settings/export` returns every setting as one JSON object
(plus `format` and `fw`), without the WiFi password, MQTT password and API
token unless `?secrets=1` is added; the page's *Export* button downloads it
(a checkbox includes the secrets) and `export` prints it on the console.
Restoring is `POST /api/v1/settings` with that file as the body — the
page's *Import…* button does exactly that — so an export without secrets
restores everything else and leaves the box's own passwords and token in
place. Unknown keys are ignored and a bad value refuses the whole file, so
a file from a newer or older firmware imports what both understand.

### Console log

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

### Wired Ethernet

The W6100 runs in MACRAW mode, so it is just another lwIP netif and
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

### UPS supply

A Mean Well LAD-xxxU security/UPS power supply (the `U` variants have the
serial port; the plain ones only have open-collector status pins) plugs
into the controller card's UPS header: 3.3 V TTL UART0 on GP12/GP13
through the card's 1 kΩ series resistors, 9600 8N1, grounds common through
the supply's V−. The link is the LAD manual's own frame — read 0x55 / write
0xAA, a length byte, a 16-bit address, data, CRC-8 (polynomial 0x07,
checked against the manual's worked examples) — not Modbus. The firmware
probes the header every 5 s until something answers, then reads the status
word, mains voltage, load current and battery voltage every second and the
per-block voltages and undervoltage cutoff every ten, spacing requests the
20 ms the supply asks for and giving up on a reply after 100 ms; five
missed replies in a row or five silent seconds mark it absent again. What
comes back is the `ups` block in the status JSON, the `ups_*` fields and
Home Assistant entities over MQTT, `pwrman_ups_*` on `/metrics`, the UPS
part of the chassis line on the page, `ups` and a line in `info` on the
console, and log lines for mains lost/restored, battery full and battery
faults. Running on battery, or a battery fault (missing, reversed,
under/overvoltage, unbalanced, discharge overload, a bad block), raises the
problem indicator. `ups buzzer off` silences the supply's alarm; the
setting lives in the supply and is lost when it restarts, as its manual
says of every write. The driver runs against an emulated supply in the
host tests (`test_ups.c`) and has not yet met a real LAD.

### Bus voltage

The controller card watches the DC input: a 120 kΩ / 10 kΩ divider (100 nF
across the bottom leg) brings VIN to GP28/ADC2, so 3.3 V full scale is
42.9 V and the bus's 33 V clamp can never take the pin past the rail. Core 0
takes eight conversions every 100 ms and reports a one-second average,
about 10 mV a count. The ADC's reference is the card's 3.3 V rail and the
divider is 1 % parts, so the reading can be a couple of percent off; put a
meter on the bus and `vin cal 24.13` (then `save`) stores the gain trim
that makes them agree, in the `vin_cal` setting (permille, 900 to 1100;
`vin cal reset` clears it). The reading is `vin_v` in the status JSON and
the MQTT status, a *Bus voltage* sensor in Home Assistant,
`pwrman_bus_volts` on `/metrics`, part of the chassis line on the page,
`vin` and a line in `info` and `status` on the console. The hardware is
rated for 20 to 28 V and the backplane opens the bus near 18.6 V,
so a bus under 19 V or over 29 V (half a volt of hysteresis either way)
raises the problem indicator and logs a line; nothing derates the budget
from it yet. The Pico 2 W carrier has no path from VIN to an ADC pin, so
there the monitor reports *not fitted* and every surface leaves it out.

### Front-panel button

The controller card's button (SW3, on GP22 through 1 kΩ with 100 nF
across the input, against the pad's pull-up) is read from the main loop
and debounced over 30 ms. Three gestures:

- **short press** (released within 3 s): *wake* — a chain dimmed by the
  night window or idle dimming, or blanked with `led 0`, comes up to full
  brightness for 30 s so the port status can be read, and the whole chain
  flashes white for a moment to say the press registered;
- **long press** (held past 3 s, then released): open the BLE
  provisioning window for 10 minutes, as `improv on` and the Home
  Assistant button do;
- **hold to the end** (10 s without release): factory reset — settings
  back to defaults and saved, the fault log cleared, and a reboot. From
  3 s on the chain fills up in blue, one pixel per 1.2 s or so, as a
  warning: letting go while it is filling is still the long press; when
  all six are lit it turns red and the reset fires without waiting for
  the release.

Each gesture logs a `button:` line. `button` on the console shows whether
the board has the input and whether it is held; `button short` and
`button long` run the first two handlers without a button, so they can be
tried on the bench. The identify pattern (blue halves at 2 Hz) stays what
it was: Improv's way of pointing out the box being provisioned. The Pico 2 W carrier has no button, but GP22 is free
there and a wire from it to ground behaves as one. Not yet tried on a real
button: the card is unfabricated.

### Fan

`auto` follows total chassis power with hysteresis (`fan auto [on_w off_w
[on_ma]]`, defaults 80/60 W, 30 s anti-flap hold) and also runs while any
port holds a contract over `on_ma` (default 3000 mA, so the everyday
5 V/3 A contract never trips it; 0 disables) — a 5 V/5 A contract is only
25 W of chassis load but heats the blade as I²R. In auto the fan stays on
until both rules are clear; `on`/`off` are manual overrides. A gen-3
blade's temperature readings are not a fan input yet.

### Fault log

Faults and probe failures persist in the data partition (~256 records,
oldest dropped); `faults` lists them with power/contract at the moment of
the event and wall-clock time once SNTP has synced, and the same records
come out of `GET /api/v1/faults` and the page's *Fault log* panel. Every
boot adds a record saying why it happened.

### Problem indicator

One aggregate "needs attention" flag — any port in `fault`, the engine
stalled, a trial firmware image not yet committed, the wired link down
while WiFi carries the traffic, or the UPS supply running on its battery
or reporting a battery fault — with a short description naming the ports
(`faults: Port 2, Desk; trial firmware uncommitted`). It is the `problem` /
`problems` pair in the status JSON and MQTT status, a line in `info`, a red
line on the page, and a diagnostic *Problem* binary sensor in Home
Assistant whose `detail` attribute carries the description.

### Boot reason

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
