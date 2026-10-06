# Changelog

## [0.14.0](https://github.com/mikesmitty/power-manifold/compare/controller-firmware-v0.13.0...controller-firmware-v0.14.0) (2026-10-06)


### Features

* **controller:** open Settings at the token field when a change needs it ([bf3633a](https://github.com/mikesmitty/power-manifold/commit/bf3633a7f9704bada51e678d8992fc35bd0de9ba))
* **controller:** restyle the web page like the front of the chassis ([2884df2](https://github.com/mikesmitty/power-manifold/commit/2884df2440a09d7eeb64fc55194fb8b55b60a449))


### Bug Fixes

* **controller:** name charge events on MQTT ([2b108b9](https://github.com/mikesmitty/power-manifold/commit/2b108b9632afb296ce416499f6a3b6d685837938))
* **controller:** stop web page labels overlapping or splitting ([70baa26](https://github.com/mikesmitty/power-manifold/commit/70baa26e945b5751f3b1fa152bcdd3541427b79d))

## [0.13.0](https://github.com/mikesmitty/power-manifold/compare/controller-firmware-v0.12.0...controller-firmware-v0.13.0) (2026-10-06)


### Features

* **controller:** breathe the chassis light red with no network ([53d8d74](https://github.com/mikesmitty/power-manifold/commit/53d8d74d1b1be2a962ad32b2f0c22657820c1154))
* **controller:** drive the chassis light at the head of the light bar ([cedbd06](https://github.com/mikesmitty/power-manifold/commit/cedbd06a386f39cc354731ec4e6da31c21b5d7d0))
* **controller:** first-time setup over Ethernet, no changes without a token ([e418972](https://github.com/mikesmitty/power-manifold/commit/e418972b2a028b1940f03acded0174b82b0bbcca))
* **controller:** redesign the web page ([16bff12](https://github.com/mikesmitty/power-manifold/commit/16bff1206038b9fba9c0d4fcc26c7b5347fd983d))
* **controller:** retire magenta from the status lights ([aee9fe1](https://github.com/mikesmitty/power-manifold/commit/aee9fe19f04ed03800dc4856e0a87405b4fcebda))
* **controller:** show 12 V and up in blue, 5 and 9 V in green ([5d539dc](https://github.com/mikesmitty/power-manifold/commit/5d539dc3b00742d5c86e76ab6ad94ad046079716))
* **controller:** show bus faults and an all-clear glow on the chassis light ([3dcfda6](https://github.com/mikesmitty/power-manifold/commit/3dcfda67b80647b376a92d8d561e178765993302))
* **controller:** swap the empty-slot and ready port colours ([32a6ce6](https://github.com/mikesmitty/power-manifold/commit/32a6ce6e7e0405aaf74c48abd9189a6e4e48acf8))
* **controller:** turn an active port green from 11 V, not 19 V ([54096a3](https://github.com/mikesmitty/power-manifold/commit/54096a395687a62017f980c3dace82a2e763a8ea))
* **controller:** turn ports on and off from the web page ([4aaef0d](https://github.com/mikesmitty/power-manifold/commit/4aaef0d2664b17a2f2751e60f0ebb6b4c726affc))


### Bug Fixes

* **controller:** draw an updating port dark on the web page ([6502714](https://github.com/mikesmitty/power-manifold/commit/6502714f0bac3786fb5fd185fdae372fbd749592))
* **controller:** give the links 15 s after power-up before flagging no network ([e8fce1d](https://github.com/mikesmitty/power-manifold/commit/e8fce1d6d0813768b30bd88abe3f2ec00a41f423))

## [0.12.0](https://github.com/mikesmitty/power-manifold/compare/controller-firmware-v0.11.0...controller-firmware-v0.12.0) (2026-10-05)


### ⚠ BREAKING CHANGES

* **controller:** a build without -DPICO_BOARD is now for the controller card, and a release's unsuffixed files (controller.uf2, controller.elf, controller.signed.bin, partition_table.uf2) are the card's. The Pico 2 W build is published as controller-pico2w.* and partition_table-pico2w.uf2; pass -DPICO_BOARD=pico2_w to build it.

### Features

* **controller:** add a PEM and DER codec for certificates ([48bfead](https://github.com/mikesmitty/power-manifold/commit/48bfead85a7f3cd7f83e5443b0d9e7aa084e5231))
* **controller:** build for the controller card by default ([68d33ae](https://github.com/mikesmitty/power-manifold/commit/68d33ae89d8687b93ae0ec7281221167c0ee9239))
* **controller:** cap ports at 3 A while the DC bus sags ([fff3f2d](https://github.com/mikesmitty/power-manifold/commit/fff3f2d301bb3d766a57f761ea39d6756d44d5ab))
* **controller:** factory image for first programming over SWD ([660d443](https://github.com/mikesmitty/power-manifold/commit/660d443b65859c80ee5f6db1f425584c153d1e48))
* **controller:** look for new releases and offer them ([e55e9a9](https://github.com/mikesmitty/power-manifold/commit/e55e9a941ff525c85c1ad35692481cc49ce3ce0e))
* **controller:** poll a shipped time server behind the one DHCP names ([304f1b6](https://github.com/mikesmitty/power-manifold/commit/304f1b6f00d47af4a33ff74f41d1d9d14cc94d7b))
* **controller:** report the model as PM6 to Home Assistant ([caa6d55](https://github.com/mikesmitty/power-manifold/commit/caa6d557f18f4256b92f99b4bbe33aa46701f90d))
* **controller:** run the fan from the gen-3 blade thermometers ([b18cbe1](https://github.com/mikesmitty/power-manifold/commit/b18cbe16500606244c3252fb7f5434d6fa92a4be))
* **controller:** TLS on the broker link with an optional certificate ([eb6c3b5](https://github.com/mikesmitty/power-manifold/commit/eb6c3b5f1f09f434a5243cb732108beefca174e4))
* **controller:** trust a backup update key held on a YubiKey ([d14e544](https://github.com/mikesmitty/power-manifold/commit/d14e5445708a9a472e9639af2babecc91e0ff9be))
* **controller:** trust the Let's Encrypt roots on the broker link ([e5f97e4](https://github.com/mikesmitty/power-manifold/commit/e5f97e4e47c71999f5699c436545408ea5d02f1c))


### Bug Fixes

* **controller:** let a trial image commit on a flapping WiFi link ([bfaa83c](https://github.com/mikesmitty/power-manifold/commit/bfaa83ca154b14b3c67d6dd8644ae010250d4129))

## [0.11.0](https://github.com/mikesmitty/power-manifold/compare/controller-firmware-v0.10.0...controller-firmware-v0.11.0) (2026-10-01)


### ⚠ BREAKING CHANGES

* **controller:** POST /api/v1/update and the Home Assistant install refuse unsigned images (controller.uf2, a plain controller.bin) and images older than the running firmware. Use the release's controller.signed.bin, or the console's `update --unsigned` / `--downgrade`.

### Features

* **controller:** drive gen-3 charger blades ([bfe9e21](https://github.com/mikesmitty/power-manifold/commit/bfe9e2197b359ca84e9c29a2a06b7411baff8b67))
* **controller:** install only signed images over the network ([c9f8365](https://github.com/mikesmitty/power-manifold/commit/c9f83656192faf857771c028838adeb7319cf174))
* **controller:** program gen-3 blade firmware over the backplane ([6264846](https://github.com/mikesmitty/power-manifold/commit/62648468bd28f223099555a4d67a6dd6dc49f644))
* **controller:** read the gen-3 blade's thermometers ([187d6d6](https://github.com/mikesmitty/power-manifold/commit/187d6d6c72eec33c72838535d7864d573b935859))


### Bug Fixes

* **controller:** build fake-blade images without a blade firmware bundle ([be6711b](https://github.com/mikesmitty/power-manifold/commit/be6711bfe97188892ec3802a259191b4230b08e4))
* **controller:** build the MPQ4242 driver with the port fault names ([2b32b32](https://github.com/mikesmitty/power-manifold/commit/2b32b32afe2e036b63ee18c3f35d555a106b53c9))
* **controller:** keep ports powered through updates, silent blades and bad reads ([77805dc](https://github.com/mikesmitty/power-manifold/commit/77805dcb48593ccc728670e56960e264620d9f7b))
* **controller:** keep settings readable across a reverted update ([2bf24c9](https://github.com/mikesmitty/power-manifold/commit/2bf24c98622522969718758021f0103dca3f9755))

## [0.10.0](https://github.com/mikesmitty/power-manifold/compare/controller-firmware-v0.9.0...controller-firmware-v0.10.0) (2026-09-24)


### Features

* **controller:** hold every PDO to 100 W, so the 21 V PPS range advertises 4.75 A ([b5da427](https://github.com/mikesmitty/power-manifold/commit/b5da427e0bd2c3ffb6749e1c158b043530f9e37a))


### Bug Fixes

* **controller:** release the reset lines to the backplane's 5 V pull-ups ([526a81f](https://github.com/mikesmitty/power-manifold/commit/526a81f77ee03e27a46303efaf813b8cbf7e74e5))

## [0.9.0](https://github.com/mikesmitty/power-manifold/compare/controller-firmware-v0.8.0...controller-firmware-v0.9.0) (2026-09-17)


### Features

* **controller:** declare the DC input range as 20-28 V and flag the bus above 29 V ([f309804](https://github.com/mikesmitty/power-manifold/commit/f3098041c300953af951b49f87ab5ca9fa27e106))

## [0.8.0](https://github.com/mikesmitty/power-manifold/compare/controller-firmware-v0.7.0...controller-firmware-v0.8.0) (2026-09-10)


### Features

* **controller-firmware:** DC bus voltage monitor on the controller card ([4b7601d](https://github.com/mikesmitty/power-manifold/commit/4b7601d357a858d0cc010e438ffd6a9b6d1f39c6))
* **controller-firmware:** front-panel button — wake the LEDs, open BLE, hold to factory-reset ([f7e6fb9](https://github.com/mikesmitty/power-manifold/commit/f7e6fb923512d4b1533e9b33c77032227f3f844c))
* **controller-firmware:** warm start keeps port power across a controller reboot, plus the card's board header ([9e1b223](https://github.com/mikesmitty/power-manifold/commit/9e1b22329d179107f5c9ca7f3ac1bacf3d290593))
* **controller:** move the bus-high warning to 33 V ([1a50e51](https://github.com/mikesmitty/power-manifold/commit/1a50e519b4c322c359694846979edd4e4cb51caa))


### Bug Fixes

* **controller-firmware:** clear the pad pull-down on SDA, SCL and ALERT# ([4fd1637](https://github.com/mikesmitty/power-manifold/commit/4fd16379f95fda0e70c7b3ee0766067bf3cd8122))

## [0.7.0](https://github.com/mikesmitty/power-manifold/compare/controller-firmware-v0.6.0...controller-firmware-v0.7.0) (2026-09-08)


### Features

* **controller:** Mean Well LAD UPS supply on the card's UPS header ([e934818](https://github.com/mikesmitty/power-manifold/commit/e934818837a817bb97dffdeba75236bea9e9e89c))

## [0.6.0](https://github.com/mikesmitty/power-manifold/compare/controller-firmware-v0.5.0...controller-firmware-v0.6.0) (2026-09-08)


### Features

* **controller:** charged sinks show solid magenta on the status LEDs ([7c44edb](https://github.com/mikesmitty/power-manifold/commit/7c44edb925ade4094516016d5ed96a17c8c9e107))
* **controller:** per-port voltage cap on the advertised PDO set ([f990bee](https://github.com/mikesmitty/power-manifold/commit/f990beef028354a75e2606992d394a8912ded923))


### Bug Fixes

* **controller:** enable the internal pull-up on EXP_INT# ([68f72e2](https://github.com/mikesmitty/power-manifold/commit/68f72e248bbe3ac9985378dc1e125118503c1dd3))
* **controller:** subscribe to MQTT command topics one per SUBACK ([a734ab7](https://github.com/mikesmitty/power-manifold/commit/a734ab735f21c4aacc47ab9b74d2720c307b8b15))

## [0.5.0](https://github.com/mikesmitty/power-manifold/compare/controller-firmware-v0.4.1...controller-firmware-v0.5.0) (2026-09-05)


### Features

* **controller:** aggregate problem indicator with a Home Assistant binary sensor ([e5ac774](https://github.com/mikesmitty/power-manifold/commit/e5ac774ccf1936ace05bb6b261f4c455a4beb1a8))
* **controller:** board definition for the WIZnet W6100-EVB-Pico2 ([cf5ddfa](https://github.com/mikesmitty/power-manifold/commit/cf5ddfa3c7501b502af9b6fd36c0daab0193a360))
* **controller:** charge-complete detection with off-when-charged and a sleep timer ([d8c2c71](https://github.com/mikesmitty/power-manifold/commit/d8c2c7130f3a9ae9465c48aa5f18da318c7b6f2d))
* **controller:** console fault injection for the simulated backplane ([b4306f0](https://github.com/mikesmitty/power-manifold/commit/b4306f0cea4df3788f7ccc9a026847e34da25a65))
* **controller:** console log ring with UDP syslog and GET /api/v1/log ([9aca382](https://github.com/mikesmitty/power-manifold/commit/9aca3822415c9b0be0ec3a6f792d99d305b37663))
* **controller:** fault log over the API and the page, last fault per port in HA ([27e7cfc](https://github.com/mikesmitty/power-manifold/commit/27e7cfc444144a61d08aee71d0b98021a7b8a9c4))
* **controller:** Home Assistant event entity per port from the MQTT event topic ([7ef93f0](https://github.com/mikesmitty/power-manifold/commit/7ef93f02910db513c65e0d1e250a852e86952aba))
* **controller:** LED night window and idle dimming ([5a05566](https://github.com/mikesmitty/power-manifold/commit/5a055668ddf2777ccd5e5559545d8e5218d02963))
* **controller:** per-port current limit on every surface, applied live ([bd96af8](https://github.com/mikesmitty/power-manifold/commit/bd96af81f75dfae222ce30fd4347550e2a846b1f))
* **controller:** per-port names on the console, API, page and Home Assistant ([2d8aa3e](https://github.com/mikesmitty/power-manifold/commit/2d8aa3e9ed3db1a3abbf1b1d8414ed49bea4a158))
* **controller:** per-port power-up state, on, off or last ([7b6098f](https://github.com/mikesmitty/power-manifold/commit/7b6098fe0394732a936cd79ab55f26e5df83e45e))
* **controller:** Prometheus text exposition on GET /metrics ([eabb3c2](https://github.com/mikesmitty/power-manifold/commit/eabb3c273f4dbb956338d221999fb0b889eadd38))
* **controller:** report why the controller booted and surface crashes ([de0f9a5](https://github.com/mikesmitty/power-manifold/commit/de0f9a582a25681f10a156139f33cb76a1b25d4d))
* **controller:** settings export and import, host-tested round trip ([1f97a9f](https://github.com/mikesmitty/power-manifold/commit/1f97a9f0b1a7e7e058d56053e1d36175e8190c8b))
* **controller:** stagger the blades seated at boot, by priority ([270cbf1](https://github.com/mikesmitty/power-manifold/commit/270cbf197ceb4782790fdc3e5f31a717bf5f64c9))
* **controller:** static IPv4 addressing and a DNS override ([f29ae9a](https://github.com/mikesmitty/power-manifold/commit/f29ae9a1819d6943ae9699a9faa391f2fa2857ed))
* **controller:** status LED scheme with chassis overlays and brightness controls ([2aa1458](https://github.com/mikesmitty/power-manifold/commit/2aa14588f2fe57afcc7388cb5f796d5513c72258))


### Bug Fixes

* **controller:** bench findings for addressing output and the syslog backlog ([f176b5c](https://github.com/mikesmitty/power-manifold/commit/f176b5c953265b4e9aac508968678aa8d963cf2d))
* **controller:** clamp TCP MSS under the broker's Cilium/WireGuard path; a stalled MQTT link no longer starves lwIP ([c483164](https://github.com/mikesmitty/power-manifold/commit/c483164a65cb4224efa4a378851fbd7d13885127))
* **controller:** stop lwIP refusing the tail of every MQTT burst ([b3e5c1b](https://github.com/mikesmitty/power-manifold/commit/b3e5c1b1ca05755eb13639245e52c2bf88f54c73))
* **controller:** stream HTTP bodies a segment at a time so the page loads beside MQTT ([d271739](https://github.com/mikesmitty/power-manifold/commit/d2717397db006770c6dd12ce50471f9ed2507202))
* **controller:** W6100 bring-up fixes from the first board ([06c3e8d](https://github.com/mikesmitty/power-manifold/commit/06c3e8daba00497343d1a958147a44e0f1689d05))

## [0.4.1](https://github.com/mikesmitty/power-manifold/compare/controller-firmware-v0.4.0...controller-firmware-v0.4.1) (2026-09-03)


### Bug Fixes

* **controller:** keep the settings sequence counter across `defaults` ([62376d6](https://github.com/mikesmitty/power-manifold/commit/62376d61701abdbd9222bb82cf88a806b33d2bf1))
* **controller:** notify the Improv result URL before the provisioned state ([8496781](https://github.com/mikesmitty/power-manifold/commit/84967819c1ce49105ec4818ad2b674b72e16bc59))
* **controller:** serve the root page when the request carries a query string ([7da4816](https://github.com/mikesmitty/power-manifold/commit/7da481699991dbecc7afc52554d20e881bfa269a))

## [0.4.0](https://github.com/mikesmitty/power-manifold/compare/controller-firmware-v0.3.0...controller-firmware-v0.4.0) (2026-09-02)


### Features

* **controller:** fan auto-on for contracts over 3A ([e6c0904](https://github.com/mikesmitty/power-manifold/commit/e6c09046d3a815baecf1f7d63a817d3e2bfcc98a))
* **controller:** Improv Wi-Fi BLE provisioning ([4398049](https://github.com/mikesmitty/power-manifold/commit/4398049910f927196d6a0950aa6107cd41b24d17))
* **controller:** make the chassis power budget settable remotely ([c595a2c](https://github.com/mikesmitty/power-manifold/commit/c595a2c6e5b23b354ad1ae42d2b0f5ff5c996f8e))
* **controller:** per-port history sparklines in the web UI ([f8a24fe](https://github.com/mikesmitty/power-manifold/commit/f8a24fe692447e0029dc59a0d6e5054c8e51d027))
* **controller:** program the blade PDO table with fixed 12 V and 11 V PPS ([c4cbc17](https://github.com/mikesmitty/power-manifold/commit/c4cbc176b7da49b83af443ab73990c046cf28eef))
* **controller:** show measured draw alongside budget reservation in the web UI ([5074caf](https://github.com/mikesmitty/power-manifold/commit/5074caf8c3083c259a56c27b774c8d003a46e461))
* **controller:** W6100 wired Ethernet as an lwIP netif ([1651d55](https://github.com/mikesmitty/power-manifold/commit/1651d55fcddc0f10d7c7b4a6c8fba529dec2657e))
* **controller:** web UI settings panel with Improv setup secret ([62df4f2](https://github.com/mikesmitty/power-manifold/commit/62df4f23e887e04f8da48a1be3c7834c19bf3bd1))


### Bug Fixes

* **controller:** cap the settable power budget at 600W ([747cade](https://github.com/mikesmitty/power-manifold/commit/747cade1f104d65222a9449d4193034fa28e7c2c))
* **controller:** core 0 stack overflow into the engine, plus bench diagnostics ([0bfd9b2](https://github.com/mikesmitty/power-manifold/commit/0bfd9b22a343fc512659a77cdc8c2a422b50c3fa))

## [0.3.0](https://github.com/mikesmitty/power-manifold/compare/controller-firmware-v0.2.0...controller-firmware-v0.3.0) (2026-08-31)


### Features

* **controller:** automatic fan policy with hysteresis ([d256d2c](https://github.com/mikesmitty/power-manifold/commit/d256d2cdbdde2efd187038a6407d25cb29abe15f))
* **controller:** HA buttons for hard reset/src-cap and port priority numbers ([5802dc6](https://github.com/mikesmitty/power-manifold/commit/5802dc6ffcb6bc40376f27674c1bb9b5c63717d3))
* **controller:** Home Assistant update entity with HTTP pull OTA ([c8b5fd8](https://github.com/mikesmitty/power-manifold/commit/c8b5fd83c2afda4ee73d0474b96182fdc7301388))
* **controller:** per-port and chassis energy sensors ([b102fb8](https://github.com/mikesmitty/power-manifold/commit/b102fb8f9eb3cb00b54d486c55c23f989a8bdcad))
* **controller:** persistent fault log in the data partition ([a3c83df](https://github.com/mikesmitty/power-manifold/commit/a3c83df7a8bef5f2484fc82cace32c63f59a150c))
* **controller:** recover throttled ports in partial steps ([bf6f2ed](https://github.com/mikesmitty/power-manifold/commit/bf6f2edbfe7e802796e2604e80eee0aa04909f7f))

## [0.2.0](https://github.com/mikesmitty/power-manifold/compare/controller-firmware-v0.1.0...controller-firmware-v0.2.0) (2026-08-31)


### Features

* **controller:** add OTA update endpoint writing the inactive A/B slot ([231ed1e](https://github.com/mikesmitty/power-manifold/commit/231ed1ede804ea6432b141c07baeeedbaceb8f84))
* **controller:** host-side engine tests and fake-blade mode ([4461f2b](https://github.com/mikesmitty/power-manifold/commit/4461f2bee788238798c0773611121a35eb54db29))
* **controller:** partition-table flash map with A/B slots and TBYB commit ([5477425](https://github.com/mikesmitty/power-manifold/commit/5477425efcc60b5fca2c1e9b68d7a4f910a3847b))
* **controller:** priority-based throttle victim selection ([35dd734](https://github.com/mikesmitty/power-manifold/commit/35dd734ab810ae2498e332097257ac0c1ec5ae31))
* **controller:** tune blade over-current protection to MPQ4242 behavior ([5d90338](https://github.com/mikesmitty/power-manifold/commit/5d903384696dc165cf1a04558c565f2434d8131e))


### Bug Fixes

* **controller:** keep a taken-over OTA connection from aborting its successor ([1b276e0](https://github.com/mikesmitty/power-manifold/commit/1b276e0aceb860a5e4bad5daaa3e946994826095))

## 0.1.0 (2026-08-30)


### Features

* **controller:** scaffold native RP2350 controller firmware ([8041d8e](https://github.com/mikesmitty/power-manifold/commit/8041d8e629d8cfa35920143ea43562d2a1f5bbac))

## Changelog
