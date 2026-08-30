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
  WS2812C status LEDs via PIO.
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

## First-time setup

Connect to the USB console (any serial terminal, 115200) and provision:

```
wifi <ssid> <password>
mqtt <broker-host> [port user pass]
name pwrman
save
reboot
```

`help` lists everything else (port control, budget, fan, LED brightness,
`bootsel` for firmware updates).

## Management surfaces

- **Web UI / API**: `http://<name>.local/` status page;
  `GET /api/v1/status`; `POST /api/v1/port/<n>` with
  `{"action":"enable"|"disable"|"hard_reset"|"src_cap"}` and
  `POST /api/v1/fan` with `{"on":true}` (Bearer token if `token` is set).
- **MQTT**: telemetry under `pwrman/<name>/...` at 1 Hz, commands on
  `.../port/<n>/set` and `.../fan/set`, faults/contract changes on
  `.../event`, availability via LWT. Home Assistant discovers every port's
  sensors and switches automatically when a broker is configured.

## Not yet implemented

- OTA via the RP2350 bootrom A/B partitions (try-before-you-buy); BOOTSEL for now
- Throttle victim selection by port priority (currently clamps the newcomer)
- W6100 wired Ethernet netif (hardware path reserved, see GPIO map)
- BLE provisioning via RM2/BTstack (candidate for initial configuration)
- Front-panel display (planned as another consumer of the telemetry snapshot)
