# Power Manifold

![Power Manifold chassis from the front right](docs/public/hero.webp)

A six-port USB-C power supply: up to 100 W per port from one 24 V supply,
with per-port control, priority-based power sharing and Home Assistant
support. This repository holds the hardware (KiCad), the firmware and the
documentation site.

**Documentation:** <https://docs.powermanifold.io/>

- [First-time setup](https://docs.powermanifold.io/setup/first-time-setup/)
- [User guide](https://docs.powermanifold.io/guide/front-panel/)
- [Integrations](https://docs.powermanifold.io/integrations/mqtt/): MQTT,
  Home Assistant and the HTTP API
- [Hardware architecture](https://docs.powermanifold.io/developers/architecture/)
- [Building the controller firmware](https://docs.powermanifold.io/developers/controller/building/)
  and the [charger blade firmware](https://docs.powermanifold.io/developers/charger-module/overview/)

## Repository layout

| Path | Contents |
| --- | --- |
| `hardware/backplane` | Backplane KiCad project |
| `hardware/backplane-backer` | Insulating FR4 backer plate between the backplane and the rear wall |
| `hardware/charger-module` | Charger blade KiCad project |
| `hardware/controller` | Management controller card KiCad project |
| `hardware/left-side-plate` | Left chassis side plate, fabricated as a bare PCB |
| `hardware/pcie-breakout` | Pico 2 W development carrier for the management slot |
| `hardware/status-strip` | Front-panel status LED strip |
| `hardware/libraries` | Shared KiCad symbols and footprints |
| `hardware/CAD` | STEP exports of the boards |
| `firmware/controller` | Management controller firmware (RP2350) and its host tests |
| `firmware/charger-module` | Charger blade firmware (STM32G071) and its host tests |
| `firmware/update-proxy` | Cloudflare Worker that serves controller update images |
| `docs` | The documentation site (Astro and Starlight) |
| `cases` | Earlier 3D-printed cases |

Each firmware directory has a short README with build commands. Its full
developer documentation is in its `docs/` directory and is published on the
documentation site.

CI exports KiCad fabrication outputs when a board changes, and
release-please cuts a release for each component.

## Status

The firmware has run on development boards against simulated blades, but
not yet on real blades in a live backplane.
