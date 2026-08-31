# Changelog

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
