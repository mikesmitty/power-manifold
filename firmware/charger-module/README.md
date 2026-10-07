# Power Manifold Charger Module Firmware

Firmware for the STM32G071 on the gen-3 charger blade (`hardware/charger-module`
from release 0.15.0 on). It runs the blade's USB-C port with ST's USB-PD
stack, a TPS55288 converter and a TCPP02-M18 port protector, and serves a
register file to the controller over the backplane.

## Quick start

```sh
cmake -S . -B build -G Ninja
ninja -C build                      # build/charger-module.elf, .bin, .hex
```

```sh
cmake -S test -B test/build && cmake --build test/build
ctest --test-dir test/build --output-on-failure
```

The controller's build bundles `build/charger-module.bin` and programs blades
with it over the backplane.

## Documentation

Developer documentation lives in [`docs/`](docs/) and is published on the
[documentation site](https://docs.powermanifold.io/) under
*For developers*:

- [Overview](docs/overview.md): what the blade is, its protection, how the
  port runs, pin map, source layout, ST's stack and its license, building,
  flashing and the console.
- [Register map and updates](docs/register-map.md): the backplane register
  map and how the controller programs the blade through the ROM bootloader.
