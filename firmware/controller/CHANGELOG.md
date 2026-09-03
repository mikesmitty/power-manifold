# Changelog

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
