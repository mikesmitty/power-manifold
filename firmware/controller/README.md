# Power Manifold Controller Firmware

Native pico-sdk firmware for the management controller: an RP2350 that
supervises up to six charger blades through the backplane's I2C mux and GPIO
expander, shares the chassis power budget between them, drives the port
lights, and serves the web page, the HTTP API, MQTT with Home Assistant
discovery, and a serial console.

The production target is the `hardware/controller` card in the backplane's
management socket: an RP2350A with a WIZnet W6100 for wired Ethernet and a
Raspberry Pi RM2 for Wi-Fi and Bluetooth.

## Quick start

```sh
export PICO_SDK_PATH=~/.pico-sdk/sdk/2.2.0   # with lib/btstack and lib/mbedtls checked out
cmake -B build -G Ninja
ninja -C build
```

```sh
cmake -S test -B test/build && cmake --build test/build
ctest --test-dir test/build --output-on-failure
```

## Documentation

Developer documentation lives in [`docs/`](docs/) and is published on the
[documentation site](https://mikesmitty.github.io/power-manifold/) under
*For developers*:

- [Building](docs/building.md): toolchain, board targets, network options,
  the fake-blade simulator, host tests.
- [Internals](docs/internals.md): the two cores, GPIO map, blade
  generations, warm starts and keeping ports powered, bus voltage, wired
  Ethernet, the UPS link, the fan and the logs.
- [Flash layout and updates](docs/flash-and-updates.md): partitions, first
  programming, OTA, signed images, try-before-you-buy, blade firmware
  updates, and the not-yet-run-on-hardware checklist.
- [Serial console](docs/console.md): every console command.

What an owner sees (setup, the web page, port settings, Home Assistant, the
MQTT topics and the HTTP API) is in the
[user guide](https://mikesmitty.github.io/power-manifold/).

Signing keys are covered in [`keys/README.md`](keys/README.md).
