# Power Manifold Controller Firmware

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
- **Core 0 — management** (`src/net/`, `src/cli.c`): CYW43 WiFi + lwIP,
  MQTT with Home Assistant discovery, embedded web UI + JSON API, USB CDC
  maintenance console.
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
| GP16–GP21 | *reserved* | wired Ethernet (W6100; WIZnet EVB-Pico2 pinout) |

## Building

Requires the [pico-sdk](https://github.com/raspberrypi/pico-sdk) (2.x) and an
`arm-none-eabi` toolchain.

```sh
export PICO_SDK_PATH=~/.pico-sdk/sdk/2.2.0   # or wherever the SDK lives
cmake -B build -G Ninja
ninja -C build
```

Flash `build/controller.uf2` over BOOTSEL, or `picotool load -f
build/controller.uf2`.

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

Connect to the USB console (any serial terminal, 115200) and provision:

```
wifi <ssid> <password>
mqtt <broker-host> [port user pass]
name pwrman
save
reboot
```

`help` lists everything else (port control and priorities, budget, fan
policy, LED brightness, the persistent fault log, OTA pull, `bootsel`).

## Management surfaces

- **Web UI / API**: `http://<name>.local/` status page — per port and
  chassis-wide, measured draw *and* the budget reservation held against it
  (an idle powered port draws 0 W but still reserves its 15 W base);
  `GET /api/v1/status`; `POST /api/v1/port/<n>` with
  `{"action":"enable"|"disable"|"hard_reset"|"src_cap"}`,
  `POST /api/v1/fan` with `{"on":true}` or `{"mode":"auto"}`,
  `POST /api/v1/budget` with `{"watts":N}`, and
  `POST /api/v1/update` with a firmware image as the body (Bearer token if
  `token` is set).
- **MQTT / Home Assistant**: telemetry under `pwrman/<name>/...` at 1 Hz,
  availability via LWT, faults/contract changes on `.../event`. Discovery
  publishes, per port: power/voltage/current/state sensors, a since-boot
  energy sensor (`total_increasing`, energy-dashboard ready), an enable
  switch, hard-reset and re-announce-caps buttons, and a priority number —
  plus chassis power/headroom/energy sensors, a power-budget number, a fan
  select (auto/on/off), and a firmware update entity fed from the retained
  `.../update/latest` pointer. Remote settings changes (budget, fan mode,
  priority) persist automatically a few seconds after the last change.
- **Fan**: `auto` follows total chassis power with hysteresis
  (`fan auto [on_w off_w [on_ma]]`, defaults 80/60 W, 30 s anti-flap hold)
  and also runs while any port holds a contract over `on_ma` (default
  3000 mA, so the everyday 5 V/3 A contract never trips it; 0 disables) —
  a 5 V/5 A contract is only 25 W of chassis load but heats the blade as
  I²R. In auto the fan stays on until both rules are clear; `on`/`off` are
  manual overrides.
- **Fault log**: faults and probe failures persist in the data partition
  (~256 records, oldest dropped); `faults` lists them with power/contract
  at the moment of the event and wall-clock time once SNTP has synced.

## Not yet implemented

- W6100 wired Ethernet netif (hardware path reserved, see GPIO map)
- BLE provisioning via RM2/BTstack (candidate for initial configuration)
- Front-panel display (planned as another consumer of the telemetry snapshot)
