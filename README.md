# Power Manifold

A modular, managed USB-C Power Delivery supply for racks of single-board
computers. Six hot-swappable 100 W charger blades sit in a backplane behind
one RP2350 management controller that budgets power across the chassis,
drives a status LED per port, and exposes everything over MQTT, HTTP, and a
serial console.

**Documentation:** <https://mikesmitty.github.io/power-manifold/>

## How it works

- **Charger blades** (`hardware/charger-module`). Each blade is an MPS
  MPQ4242 four-switch buck-boost USB-PD source with a TI INA226 power monitor
  on the blade's shunt. Blades negotiate PD on their own; the controller
  constrains which PDOs they advertise. The blade is a PCIe x1 card edge, so
  it slots in and out of the backplane without tools.
- **Backplane** (`hardware/backplane`). Six blade slots and a management
  slot. A TCA9548A I2C mux gives every blade its own bus segment, a TCA9539
  expander handles blade enable, presence detect, and the fan, and six
  WS2812C LEDs feed front-panel light pipes. DC input is 20–28 V through a
  Micro-Fit 3.0 connector with ideal-diode reverse protection, a 20 A fuse,
  and a TVS clamp. An AP64352 buck makes the single 5 V logic rail.
- **Management controller** (`firmware/controller`). Native pico-sdk
  firmware for the RP2350. Core 1 runs the port engine and owns the I2C bus;
  core 0 runs networking and the management surfaces. Development uses a
  Raspberry Pi Pico 2 W on the `hardware/pcie-breakout` carrier. The
  production controller (`hardware/controller`) is a custom RP2350 card for
  the same slot, with a WIZnet W6100 wired-Ethernet controller and RJ45, a
  Raspberry Pi RM2 radio for Wi-Fi and Bluetooth, and 16 MB of QSPI flash for
  the A/B firmware slots.

## Features

- Dynamic chassis power budget (360 W by default) with per-port priorities.
  Lower-priority ports are stepped down or shed before the input supply is
  oversubscribed, and stepped back up as headroom returns. Blades seated at
  boot power up one at a time in priority order.
- Per-port names, current limits, and power-up policy (on, off, or as last
  switched), settable from any surface.
- Charge-complete detection, with an off-when-charged switch and a sleep
  timer per port.
- Home Assistant through MQTT discovery: per-port power, voltage, current,
  energy, and state, enable switches, priorities and limits, charging and
  event entities, the chassis budget, fan control, an aggregate problem
  sensor, the boot reason, and a firmware update entity.
- Web UI with live per-port sparklines, the fault log, the console log, and
  a settings panel with export and import, backed by a JSON API with
  optional bearer-token auth and a Prometheus `/metrics` endpoint.
- First-time setup over Bluetooth using the Improv Wi-Fi standard, from the
  [hosted provisioner](https://mikesmitty.github.io/power-manifold/setup/wifi-provisioning/),
  the Home Assistant app, or any Improv client. A USB serial console covers
  everything else.
- DHCP or static addressing on WiFi or wired Ethernet, a DNS override, and
  console mirroring to a UDP syslog host.
- Status LEDs with a night window and idle dimming.
- A/B firmware slots with try-before-you-buy rollback. Updates arrive by HTTP
  push, URL pull from the console, or the Home Assistant update entity.
- Persistent fault log with the reason for every boot, fan auto-policy on
  chassis power and port current, and per-port energy counters.

## Repository layout

| Path | Contents |
| --- | --- |
| `hardware/backplane` | Backplane KiCad project |
| `hardware/charger-module` | Charger blade KiCad project |
| `hardware/controller` | Management controller card KiCad project |
| `hardware/pcie-breakout` | Pico 2 W development carrier for the management slot |
| `hardware/libraries` | Shared KiCad symbols and footprints |
| `hardware/CAD` | STEP exports of the boards |
| `firmware/controller` | Management controller firmware, host tests, and its README |
| `docs` | The documentation site (Astro + Starlight) |
| `cases` | V1 3D-printed cases. The V2 chassis is a metal enclosure and is not in the repo yet. |

CI exports KiCad fabrication outputs when a board changes and cuts
per-component releases with release-please. The earlier V1 design, with an
RP2040 and ESPHome on every charger module, lives in the git history and the
older release tags.

## Status

As of September 2026 the V2 backplane, charger module, and development
carrier are in their first fabrication run, and the controller card is
through its first pass of schematic and layout but has not been fabricated.
The firmware has been exercised on a Pico 2 W against simulated blades,
including Wi-Fi, BLE provisioning, the web UI, MQTT, OTA updates, and fault
injection, and the wired-Ethernet path has run on a WIZnet W6100-EVB-Pico2;
it has not yet driven real blades on a live backplane.
