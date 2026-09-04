---
title: Controller firmware
description: "Native pico-sdk firmware for the V2 management controller: building, flashing, updates, first-time setup, and management surfaces."
---

Native pico-sdk firmware for the V2 management controller, supervising up to
six MPQ4242 charger blades through the backplane TCA9548A I2C mux and TCA9539
GPIO expander.

Development target: a Raspberry Pi Pico 2 W (RP2350) on the backplane
management socket. Production target: a custom RP2350 board with a WIZnet
W6100 wired-Ethernet controller, possibly plus a Raspberry Pi RM2 radio
module (same CYW43439 as the Pico 2 W, so the WiFi/Bluetooth stack carries
over).

## Architecture

Two cores, one rule: **only core 1 touches the backplane.**

- **Core 1 — engine** (`src/engine/`): 100 Hz supervisory loop. Round-robins
  the mux channels reading each blade's INA226 and MPQ4242, runs the per-port
  state machine and the chassis power-budget arbiter, owns blade EN / presence
  / fan via the expander, services GLOBAL_ALERT# / EXP_INT#, renders the
  WS2812C status LEDs via PIO. When a new contract would exceed the chassis
  budget, strictly lower-priority ports are renegotiated downward first
  (worst priority first, 15 W floor); only if that isn't enough does the
  newcomer get clamped. Throttled ports recover automatically when budget
  frees up, highest priority first. Per-port priority: `port <n> priority`
  in the CLI (0 = highest, default = port number).
- **Core 0 — management** (`src/net/`, `src/cli.c`): lwIP over CYW43 WiFi
  and/or a WIZnet W6100 wired Ethernet controller, MQTT with Home Assistant
  discovery, embedded web UI + JSON API, USB CDC maintenance console,
  Improv Wi-Fi provisioning over BLE (BTstack on the same CYW43).
- **Between them** (`src/ipc.c`): a command queue, an event queue, and a
  seqlock telemetry snapshot. Every management surface is a thin transport
  over the same command/telemetry interface.

The hardware watchdog is fed only while both cores make progress.

## GPIO map

| GPIO | Signal | Notes |
| --- | --- | --- |
| GP2 | LED_DATA | WS2812C chain, PIO |
| GP3 | GLOBAL_ALERT# | wire-OR of blade ALERT# lines |
| GP4/GP5 | SDA/SCL | I2C0, pull-ups on backplane |
| GP6 | EXP_INT# | TCA9539 interrupt |
| GP7 | MUX_RST# | TCA9548A reset |
| GP8 | EXP_RST# | TCA9539 reset |
| GP16–GP19 | SPI0 MISO/CS/SCK/MOSI | W6100 wired Ethernet (WIZnet EVB-Pico2 pinout) |
| GP20 | ETH_RST# | W6100 reset |
| GP21 | ETH_INT# | W6100 interrupt, level-low while a frame waits |

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

### Network options

| Option | Default | Effect |
| --- | --- | --- |
| `NET_WIFI` | ON | CYW43 WiFi + Improv BLE provisioning; needs a CYW43 board (`pico2_w`) |
| `NET_ETH` | ON | W6100 wired Ethernet on SPI0 GP16–21, probed once at boot |

Both on is the production shape (RM2 radio + W6100). A Pico 2 W with nothing
on GP16–21 logs `eth: no W6100 answering` at boot and runs WiFi-only. For a
board with no radio, such as a WIZnet W6100-EVB-Pico2 in the same socket,
build a wired-only image (no cyw43 or BTstack blobs, about 230 KB). The
pico-sdk has no board file for that EVB, so `boards/` carries one; it mostly
exists to declare the EVB's 2 MB flash so the settings and fault-log sectors
land inside the chip:

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

`-DFAKE_BLADES=ON` swaps the four I2C drivers for a simulated backplane
(`src/engine/sim/`) and drives it through a repeating 60-second demo script —
attaches, budget contention with priority shedding, an over-current fault and
recovery, blade insertion/removal. Everything above the driver seam (state
machine, budget arbiter, MQTT/HA, web UI, CLI) is the real code, so the whole
management plane can be exercised on a bare Pico 2 W:

```sh
cmake -B build-fake -G Ninja -DFAKE_BLADES=ON
ninja -C build-fake
```

### Host-side tests

The engine core (state machine + budget arbiter) is hardware-free and runs
natively against the same simulated drivers:

```sh
cmake -S test -B test/build
cmake --build test/build
ctest --test-dir test/build --output-on-failure
```

CI runs these on every push/PR touching the firmware.

## Flash layout & updates

The flash is carved up by an RP2350 partition table — two A/B image slots the
bootrom picks between, plus a `data` partition holding persistent settings
(and, later, the fault log). Layouts live in `partitions/*.json`, one per
board; the build compiles the selected one (`PARTITION_TABLE_JSON`, default
`pico2w-4mb.json`) into `build/partition_table.uf2`:

| Offset | Size | Partition |
| --- | --- | --- |
| `0x000000` | 4K | partition table |
| `0x002000` | 1536K | `A` — image slot |
| `0x182000` | 1536K | `B` — image slot |
| `0x302000` | 1016K | `data` — settings ping-pong pair in the first two sectors |

The firmware never hardcodes these offsets: it looks partitions up **by ID**
through the bootrom at boot, so the same binary runs on any layout (the
16 MB production map in `prod-16mb.json` just makes everything bigger and
adds an `assets` partition). Boards with no partition table at all still
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
the notification it raises when it spots one) or the Web Bluetooth
provisioner at improv-wifi.com in a Chromium browser hands it the SSID and
password. *Identify* flashes the status LEDs so you know which box you're
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
panel unlocks with it. `help` lists everything else (port control,
priorities, names and current limits, budget, fan policy, LED brightness,
the persistent fault log, OTA pull, `bootsel`).

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

- **Web UI / API**: `http://<name>.local/` status page — per port and
  chassis-wide, measured draw *and* the budget reservation held against it
  (an idle powered port draws 0 W but still reserves its 15 W base); click a
  port row for sparklines of its last 10 minutes of W / A / V, sampled from
  the page's own 1 Hz poll (history lives in the tab, so it starts when the
  page opens — Home Assistant keeps the long-term record);
  the *Settings* panel below the table covers the device name, broker, API
  token, chassis budget, fan policy, status LEDs, port names (up to 23
  characters each; blank means `Port N`, and the label shows in the table,
  the console and Home Assistant) and per-port current limits (500 to
  5000 mA: the current field of every PDO the port advertises, so the
  wattage ceiling scales with the voltage the device picks; a live port
  renegotiates at once, and the INA226 emergency trip stays at 125 % of
  the blade's 5 A ceiling regardless), and each port's state at power-up
  (`on`, `off` or `last`, see below). `GET /api/v1/status` (each port
  carries its `name`, `limit_ma` and `boot`); `GET` / `POST /api/v1/settings` with
  any subset of `{"name","mqtt_host","mqtt_port","mqtt_user","mqtt_pass",
  "token","budget_w","fan_mode","fan_on_w","fan_off_w","fan_on_ma",
  "led_brightness","led_boot","port_names","port_limits_ma","port_boot"}` (the last
  three are arrays of six; saved to flash at once; budget, fan, LEDs, names
  and limits apply live, `POST /api/v1/reboot` applies the name and
  broker);
  `GET /metrics` is a Prometheus text-exposition endpoint (chassis gauges,
  a `pwrman_info` line with firmware, slot and boot reason, and every port
  metric labelled `port` and `name`), never gated;
  `GET /api/v1/faults[?offset=N]` pages the fault log newest first (eight
  records a page, each with a human `text`) and `POST /api/v1/faults/clear`
  wipes it; the status JSON also carries `boot`, the reason for the last
  boot, and the *Fault log* panel on the page shows both;
  `POST /api/v1/port/<n>` with
  `{"action":"enable"|"disable"|"hard_reset"|"src_cap"}`,
  `POST /api/v1/fan` with `{"on":true}` or `{"mode":"auto"}`,
  `POST /api/v1/budget` with `{"watts":N}`, and
  `POST /api/v1/update` with a firmware image as the body. Mutations take a
  Bearer token once `token` is set; `/settings` always wants one — the
  token, or the setup secret from an Improv redirect while none is stored.
- **MQTT / Home Assistant**: telemetry under `pwrman/<name>/...` at 1 Hz,
  availability via LWT, faults/contract changes on `.../event`. Discovery
  publishes, per port (entity names carry the port's label, so `Port 3
  power` becomes `Desk phone power` after a rename; a rename re-publishes
  discovery and entity ids stay put): power/voltage/current/state sensors, a since-boot
  energy sensor (`total_increasing`, energy-dashboard ready), an enable
  switch, hard-reset and re-announce-caps buttons, priority and
  current-limit numbers, and a power-up state select (on/off/last) —
  plus chassis power/headroom/energy sensors, a power-budget number, a fan
  select (auto/on/off), an LED brightness number, a diagnostic *Last boot
  reason* sensor, and a firmware update
  entity fed from the retained `.../update/latest` pointer. Each port's
  state sensor carries `last_fault` / `last_fault_at` attributes (the
  newest fault or probe failure, as text and epoch seconds). Remote settings
  changes (budget, fan mode, priority, current limit, power-up state, LED
  brightness) persist automatically a few seconds after the last change.
- **Port state at power-up**: each port has a boot policy — `on` (the
  default), `off` (stays disabled until switched on), or `last` (comes back
  however it was last switched, from the console, the API, the page or the
  Home Assistant switch; the firmware records every switch and, under
  `last`, saves it a few seconds later). Set it with `port <n> boot
  on|off|last`, the `port_boot` settings array, the page's *Settings*
  panel, or the per-port Home Assistant select; `status` shows it in the
  `boot` column. Before this, an HA-disabled port came back enabled after
  a power cut.
- **Staggered power-up**: the blades found seated at boot are enabled one
  at a time, 250 ms apart, in priority order (slot order among equals;
  boot-disabled ports hold no slot), so six sinks do not inrush and
  negotiate on the DC input at once and the highest-priority port claims
  the budget first. Blades seated later, and ports switched on later, are
  immediate as before.
- **Status LEDs**: one WS2812 per slot behind the front-panel light pipes.
  Dim white empty, cyan blink probing, amber idle, blue (below 19 V) or
  green (19 V and up) active, the same colour pulsing at 1 Hz when
  throttled, red blink at 5 Hz on a fault, off when disabled. A short comet
  crosses the chain every 3 s while something needs attention: blue while
  the BLE provisioning window is open, white while no link has an address.
  Power-up runs a sweep across the six pixels (`led boot white|rainbow`),
  which also proves the chain order. `led 0` blanks the chain but a faulted
  port keeps blinking at a floor level; brightness is also a Home Assistant
  number and a field in the web *Settings* panel.
- **Wired Ethernet**: the W6100 runs in MACRAW mode, so it is just another
  lwIP netif and everything above it (DHCP, mDNS, MQTT, HTTP, OTA pull) is
  the same code as over WiFi. Its MAC is locally administered, derived from
  the RP2350's unique ID. When both links are up the wired one holds the
  default route and WiFi stands by; replies always leave on the interface
  that owns their address. `info` shows `eth:` link speed and address, and
  the status JSON carries `"eth"`. Not yet exercised on hardware: the driver
  is verified against an emulated chip in the host tests (`test_w6100.c`).
- **Fan**: `auto` follows total chassis power with hysteresis
  (`fan auto [on_w off_w [on_ma]]`, defaults 80/60 W, 30 s anti-flap hold)
  and also runs while any port holds a contract over `on_ma` (default
  3000 mA, so the everyday 5 V/3 A contract never trips it; 0 disables) —
  a 5 V/5 A contract is only 25 W of chassis load but heats the blade as
  I²R. In auto the fan stays on until both rules are clear; `on`/`off` are
  manual overrides.
- **Fault log**: faults and probe failures persist in the data partition
  (~256 records, oldest dropped); `faults` lists them with power/contract
  at the moment of the event and wall-clock time once SNTP has synced, and
  the same records come out of `GET /api/v1/faults` and the page's *Fault
  log* panel. Every boot adds a record saying why it happened.
- **Boot reason**: the banner, `info`, the status JSON (`boot`) and Home
  Assistant report why the controller last started: power-on, brown-out,
  RUN pin, debugger, watchdog timeout, a requested reboot, a firmware
  update, a trial image reverting, a plain warm reset (a debugger's
  SYSRESETREQ), or a HardFault with the core, PC, LR and CFSR that a probe
  would otherwise have been needed for. Deliberate reboots and the fault
  handler stamp the watchdog scratch registers before the reset, every boot
  leaves a sentinel there, and the rest comes from the chip's reset-cause
  bits (which a SYSRESETREQ does not update, hence the sentinel).

## Not yet implemented

- Front-panel display (planned as another consumer of the telemetry snapshot)
