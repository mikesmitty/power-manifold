# Building the controller firmware

How to build the controller firmware for each board, run it without a backplane, and run the host-side tests. The source is in [`firmware/controller`](../).

## Building

Requires the [pico-sdk](https://github.com/raspberrypi/pico-sdk) (2.x) with
its `lib/btstack` (Wi-Fi setup) and `lib/mbedtls` (MQTT TLS) submodules
checked out, an `arm-none-eabi` toolchain, and Python 3 (BTstack's
`compile_gatt.py` turns `src/net/improv_profile.gatt` into a header at build
time).

```sh
export PICO_SDK_PATH=~/.pico-sdk/sdk/2.2.0   # or wherever the SDK lives
cmake -B build -G Ninja
ninja -C build
```

A new board takes `build/controller-factory.bin` over SWD, or
`partition_table.uf2` and `controller.uf2` over BOOTSEL: see
[First programming](flash-and-updates.md#first-programming). After that, updates go into the
inactive image slot.

The image carries the gen-3 blade firmware it programs blades with (see
[Blade firmware updates](flash-and-updates.md#blade-firmware-updates)): `-DBLADE_IMAGE=<path>`
names the `charger-module.bin` to bundle, and without it the build takes a
sibling `../charger-module/build/charger-module.bin` when one is there
(build `firmware/charger-module` first). A build with neither still starts
blades that hold a good image, but cannot program a blank one, and says so
at configure time.

## Boards

| `PICO_BOARD` | Board | Flash | Partition layout |
| --- | --- | --- | --- |
| `pwrman_controller_card` (default) | the production controller card, `hardware/controller` | 16 MB | `prod-16mb.json` |
| `pico2_w` | Pico 2 W on the pcie-breakout: a development stand-in until the card exists | 4 MB | `pico2w-4mb.json` |
| `wiznet_w6100_evb_pico2` | WIZnet W6100-EVB-Pico2 in the breakout's socket, no radio | 2 MB | none (runs unpartitioned) |

The pico-sdk knows the Pico 2 W; `boards/` carries the other two. The EVB's
header mostly exists to declare its 2 MB flash so the settings and
fault-log sectors land inside the chip. The card's header declares its
16 MB W25Q128, the RM2 radio on the Pico 2 W's CYW43 pins, and defines
`PWRMAN_CONTROLLER_CARD`, which switches `src/pins.h` to the card's
[GPIO map](internals.md#gpio-map) and the inverted reset drivers; the build picks the
16 MB partition layout for it.

```sh
cmake -B build-pico2w -G Ninja -DPICO_BOARD=pico2_w
ninja -C build-pico2w
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
cmake -B build-pull -G Ninja -DPICO_BOARD=pico2_w -DCARRIER_INTERNAL_PULLUPS=ON
ninja -C build-pull
```

## Network options

| Option | Default | Effect |
| --- | --- | --- |
| `NET_WIFI` | ON | CYW43 WiFi + Improv Wi-Fi setup; needs a CYW43 board (`pico2_w`, the card) |
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
over SWD or BOOTSEL. The W6100 needs time after reset: the driver waits
100 ms before the first register read, because the chip does not answer at
10 ms.

## Fake-blade mode (no backplane needed)

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
cmake -B build-fake -G Ninja -DPICO_BOARD=pico2_w -DFAKE_BLADES=ON
ninja -C build-fake
```

The console's `sim` command drives the simulated backplane by hand, so the
fault paths — the fault log, the *Problem* sensor, events, auto-recovery,
charge-complete — can be exercised on a bare board instead of only in the
host tests. Run `sim pause` first: the demo script would otherwise
overwrite injected state within seconds. `sim` alone prints the list; on a
real-blade build the command says so and does nothing.

| Command | Effect |
| --- | --- |
| `sim pause` / `sim run` | stop the demo script; restart it from its baseline |
| `sim seat <n>` / `sim unseat <n>` | seat or pull a blade |
| `sim gen <n> 2\|3` | which blade generation the slot holds, set before seating it |
| `sim attach <n> <mV> <mA>` / `sim detach <n>` | attach a sink with that contract, or detach it |
| `sim load <n> <pct>` | measured draw as a percentage of the contract current; `sim load 3 1` with `charged 20000 1` shows a charge-complete within a minute |
| `sim fault <n> ocp` | an over-current trip: the INA226 on a gen-2 blade, the VBUS switch on a gen-3 one |
| `sim fault <n> otw1\|ntc1\|cc\|...\|clear` | MPQ4242 fault bits, sticky until cleared |
| `sim fault <n> ovp\|conv-ocp\|plug-hot\|pd\|...\|clear` | a gen-3 blade's faults, held as a condition that re-latches until cleared |
| `sim restart <n>` | a gen-3 blade's MCU restarts and loses its configuration |
| `sim temp <n> <conv> <plug>` / `sim temp <n> auto` | pin a gen-3 blade's thermometers in °C, or hand them back to the model that warms them with the load |
| `sim probe <n> ina\|mpq\|blade\|ok` | the next probes find a silent chip |
| `sim mux fail\|ok` / `sim expander fail\|ok` | bus-level failures the engine recovers from by resetting the mux and expander |

## Host-side tests

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
