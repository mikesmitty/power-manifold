# Changelog

## [0.3.0](https://github.com/mikesmitty/power-manifold/compare/charger-module-firmware-v0.2.0...charger-module-firmware-v0.3.0) (2026-10-05)


### Features

* **charger-module:** warn the sink before an over-temperature trip ([5422a0c](https://github.com/mikesmitty/power-manifold/commit/5422a0c8a16bb47b572f93d634629f14ef0e0f2d))

## [0.2.0](https://github.com/mikesmitty/power-manifold/compare/charger-module-firmware-v0.1.0...charger-module-firmware-v0.2.0) (2026-10-01)


### Features

* **charger-module-firmware:** declare register map version 1 ([1eb111e](https://github.com/mikesmitty/power-manifold/commit/1eb111e6ab9cdf9fb9246b9d0b3d941c196a732b))
* **charger-module-firmware:** reset into the ROM bootloader on request, on silence, and by option ([caac111](https://github.com/mikesmitty/power-manifold/commit/caac111e6694836d9445bfc8548a0c861c55654b))


### Bug Fixes

* **charger-module-firmware:** hold a hot reading for 50 ms before tripping ([d359fab](https://github.com/mikesmitty/power-manifold/commit/d359fab741582481ed11e9370d814db3d8eb17ff))
* **charger-module-firmware:** reset on the watch only with EN low, guard the reset commands ([b46f999](https://github.com/mikesmitty/power-manifold/commit/b46f9999985126b75c875c07b55e78d59988d1d4))

## 0.1.0 (2026-09-30)


### Features

* add adc reading cache for instant reads ([6d5c1f7](https://github.com/mikesmitty/power-manifold/commit/6d5c1f70831743ac66498cc9b01453e50adf55c9))
* add i2c registers and led functions ([0b01e47](https://github.com/mikesmitty/power-manifold/commit/0b01e478f39c8fec470629f8161a12b9826dd197))
* **charger-module-firmware:** add firmware for the STM32G071 charger blade ([a7667aa](https://github.com/mikesmitty/power-manifold/commit/a7667aa05b304783d402e94b22ebc2012f1ad259))
* **controller:** scaffold native RP2350 controller firmware ([8041d8e](https://github.com/mikesmitty/power-manifold/commit/8041d8e629d8cfa35920143ea43562d2a1f5bbac))
* initial charger firmware ([18ba888](https://github.com/mikesmitty/power-manifold/commit/18ba8886614f9ae0a21056d95de54454faf95615))
* update charger firmware for direct control ([3a91bbd](https://github.com/mikesmitty/power-manifold/commit/3a91bbd409cc3db423d2de4696927b5bc40629b4))


### Bug Fixes

* reduce logging verbosity ([68178a5](https://github.com/mikesmitty/power-manifold/commit/68178a5f561cc292b9639494e3d139653bb51584))
* turn off led when inactive ([81dfc89](https://github.com/mikesmitty/power-manifold/commit/81dfc895c7fd111b6f7a23a21a772b41114c7d26))
