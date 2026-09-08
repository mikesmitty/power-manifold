# Changelog

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
