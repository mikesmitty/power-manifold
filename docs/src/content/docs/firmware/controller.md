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

The [documentation site](https://mikesmitty.github.io/power-manifold/)
carries this guide alongside the hardware architecture and a hosted Wi-Fi
provisioner.

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

The console's `sim` command drives the simulated backplane by hand, so the
fault paths — the fault log, the *Problem* sensor, events, auto-recovery,
charge-complete — can be exercised on a bare board instead of only in the
host tests. `sim pause` stops the demo script first (it would otherwise
overwrite injected state within seconds; `sim run` restarts it from its
baseline), then `sim seat|unseat <n>`, `sim attach <n> <mV> <mA>`,
`sim detach <n>`, `sim load <n> <pct>` (measured draw as a percentage of
the contract current — `sim load 3 1` with `charged 20000 1` shows a
charge-complete within a minute), `sim fault <n> ocp` (INA226 trip),
`sim fault <n> otw1|ntc1|cc|...|clear` (MPQ4242 fault bits, sticky until
cleared), `sim probe <n> ina|mpq|ok` (the next probes find a silent chip),
and `sim mux fail|ok` / `sim expander fail|ok` for the bus-level failures
the engine recovers from by resetting the mux and expander. `sim` alone
prints the list; on a real-blade build the command says so and does
nothing.

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
budget, fan, link and a note when the LEDs are dimmed, and any problem shows
in red under that. Click a port row for sparklines of its last 10 minutes of
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
picks; a live port renegotiates at once, and the INA226 emergency trip
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
| `GET /api/v1/status` | none | Everything the page shows: per port `name`, `state`, `v` / `i` / `p` / `e`, `pdo`, `contract_w`, `prio`, `limit_ma`, `max_v`, `boot`, `attached`, `charged`, `fault`; chassis `total_w`, `reserved_w`, `budget_w`, `headroom_w`, `energy_kwh`, `fan` / `fan_mode`, `alert`, `rssi`, `eth`, `ble`, `uptime_s`, `fw`, `slot`, `trial`, `boot` (the reason for the last boot), `problem` / `problems`, `led_mode` / `led_now` |
| `GET /metrics` | none | Prometheus text exposition: the chassis gauges, a `pwrman_info` line with firmware, slot and boot reason, and every port metric labelled `port` and `name` |
| `GET /api/v1/faults[?offset=N]` | none | The fault log newest first, eight records a page, each with a human `text` |
| `GET /api/v1/log` | token | The console's last 4 KB as text; gated like the mutations because it names networks and hosts |
| `GET /api/v1/settings` | token or setup secret | Every setting except the secrets, with `mqtt_pass_set` / `token_set` flags in their place |
| `GET /api/v1/settings/export[?secrets=1]` | token or setup secret | The same object with every setting, plus `format` and `fw`, ready to be posted back |
| `POST /api/v1/settings` | token or setup secret | Any subset of the keys below, saved to flash at once; the reply says whether a reboot is needed |
| `POST /api/v1/port/<n>` | token | `{"action":"enable"}`, `"disable"`, `"hard_reset"` or `"src_cap"` |
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
`syslog_host`, `syslog_port`, `charged_mw`, `charged_min`, and the
six-element arrays `port_names`, `port_limits_ma`, `port_max_v` (5, 9,
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
| `status` | published at 1 Hz, retained | chassis `total_w`, `reserved_w`, `budget_w`, `headroom_w`, `energy_kwh`, `fan`, `fan_mode`, `alert`, `rssi`, `uptime_s`, `led`, `fw`, `boot`, `problem`, `problems`, `charged_mw`, `charged_min`, `led_mode` |
| `port/<n>/telemetry` | published at 1 Hz | `state`, `v`, `i`, `p`, `e`, `pdo`, `contract_w`, `prio`, `limit_ma`, `max_v`, `boot`, `charged`, `auto_off`, `sleep_min`, `fault`, `last_fault`, `last_fault_at` |
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
- an *events* entity fed from `.../event` — event types `inserted`,
  `ready`, `attached`, `detached`, `removed`, `enabled`, `disabled`,
  `contract`, `throttled`, `restored`, `fault`, `probe_failed`, `charged`,
  `charging` and `auto_off`, with the raw `code` / `arg` and the fault or
  auto-off `text` as attributes, so an automation triggers on
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
  inactive slot.

### Console

USB CDC at 115200 from any serial terminal, or RTT through a debug probe.
`help` prints the list; on a fake-blade build `sim` adds the fault injection
commands described [above](#fake-blade-mode-no-backplane-needed).

| Command | Purpose |
| --- | --- |
| `status` | port table (state, contract, draw, current limit, voltage `cap`, priority, `boot` policy, `chg` once charged) and chassis power |
| `info` | firmware, slot and boot reason, links and addressing, broker, syslog sink, LED schedule and local time, problems, simulator state |
| `wifi <ssid> [pass]` | WiFi credentials |
| `improv [on\|off]` | BLE provisioning window |
| `mqtt <host> [port user pass]` | broker; an empty host disables MQTT |
| `ip dhcp` / `ip static <addr> <mask> <gw>` | addressing: the wired link if a W6100 is fitted, else WiFi |
| `dns <addr>\|auto` | resolver override |
| `syslog <host> [port]` / `syslog off` | mirror the console to a UDP syslog host |
| `name <device-name>` | hostname and topic id |
| `token <t>\|clear` | API bearer token |
| `budget <watts>` | chassis power budget |
| `port <n> on\|off\|reset\|srccap` | port control |
| `port <n> priority <0-255>` | 0 = highest; sheds from the bottom |
| `port <n> name <text>\|clear` | label for the web UI and Home Assistant |
| `port <n> limit <500-5000>` | advertised current ceiling in mA, every PDO |
| `port <n> volt 5\|9\|12\|15\|20` | voltage cap: the highest PDO advertised (20 = the whole table) |
| `port <n> boot on\|off\|last` | state at power-up |
| `port <n> autooff on\|off` | switch off once the sink is charged |
| `port <n> sleep <min>\|off` | switch off this long after a sink attaches |
| `charged <mW> <minutes>` | charge-complete thresholds; 0 mW switches detection off |
| `fan on\|off\|auto [on_w off_w [on_ma]]` | fan policy |
| `led <0-255>`, `led boot white\|rainbow` | LED brightness and power-up sweep |
| `led dim <0-255>`, `led night <HH:MM> <HH:MM>\|off`, `led idle <minutes>\|off` | dimmed level, night window, idle dimming |
| `tz <+HH:MM\|-HH:MM>` | local time offset for the night window |
| `faults [clear]` | persistent fault log |
| `export` | every setting as JSON, without passwords |
| `update <http-url>` | OTA pull into the inactive slot |
| `stack` | per-core stack high-water marks |
| `save`, `defaults`, `reboot`, `bootsel` | settings and lifecycle |

What is typed at the console is never mirrored to the log ring or a syslog
host, so a `wifi` or `mqtt` line's password stays off the wire.

## Features

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
ports switched on later, are immediate as before.

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
`led 0` blanks the chain but a faulted port keeps blinking at a floor
level; brightness is also a Home Assistant number and a field in the web
*Settings* panel. Two schedules drop the chain to a dimmed level (`led dim
<0-255>`, default 4, just visible in a dark room): a night window in local
time (`led night 22:00 07:00`, may wrap midnight; `led night off`) and idle
dimming (`led idle 30`: no port event — plug, unplug, fault — for that
long; any event brings full brightness back). Local time is SNTP's UTC plus
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

### Fan

`auto` follows total chassis power with hysteresis (`fan auto [on_w off_w
[on_ma]]`, defaults 80/60 W, 30 s anti-flap hold) and also runs while any
port holds a contract over `on_ma` (default 3000 mA, so the everyday
5 V/3 A contract never trips it; 0 disables) — a 5 V/5 A contract is only
25 W of chassis load but heats the blade as I²R. In auto the fan stays on
until both rules are clear; `on`/`off` are manual overrides.

### Fault log

Faults and probe failures persist in the data partition (~256 records,
oldest dropped); `faults` lists them with power/contract at the moment of
the event and wall-clock time once SNTP has synced, and the same records
come out of `GET /api/v1/faults` and the page's *Fault log* panel. Every
boot adds a record saying why it happened.

### Problem indicator

One aggregate "needs attention" flag — any port in `fault`, the engine
stalled, a trial firmware image not yet committed, or the wired link down
while WiFi carries the traffic — with a short description naming the ports
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
- Front-panel button semantics; the development carrier has no button, so
  this waits for the production controller board
