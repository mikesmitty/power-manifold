# Changelog

## [0.5.2](https://github.com/mikesmitty/power-manifold/compare/charger-module-firmware-v0.5.1...charger-module-firmware-v0.5.2) (2026-10-21)


### Bug Fixes

* **charger-module:** let go of a backplane bus its master abandoned ([efe0c2f](https://github.com/mikesmitty/power-manifold/commit/efe0c2f39324b08265f90d1e46fa6e1d861861c7))
* **charger-module:** report a power-limited PPS contract at the port's limit ([a157e23](https://github.com/mikesmitty/power-manifold/commit/a157e2380d77d43554750cc1e80b041e3923b9e5))
* **charger-module:** scale the port current reading by its measured gain ([b007f2c](https://github.com/mikesmitty/power-manifold/commit/b007f2c94eedc55f51ff9678ca11fe719f651875))

## [0.5.1](https://github.com/mikesmitty/power-manifold/compare/charger-module-firmware-v0.5.0...charger-module-firmware-v0.5.1) (2026-10-20)


### Bug Fixes

* **charger-module:** flash the heartbeat for 5 ms every three seconds ([8002ebd](https://github.com/mikesmitty/power-manifold/commit/8002ebd7588cd4ce2fbd822fba3a45e12d8b38fa))
* **charger-module:** keep the OVP threshold over the converter's charge ([d95f71c](https://github.com/mikesmitty/power-manifold/commit/d95f71ca34ca720d2718db08410d948cc6a6e737))
* **charger-module:** power the cable's e-marker so a 5 A cable is found ([3fab45b](https://github.com/mikesmitty/power-manifold/commit/3fab45b3950c2d91b0c0c3575bb556e2dda32941))
* **charger-module:** read no port current while the VBUS switch is open ([26c71ea](https://github.com/mikesmitty/power-manifold/commit/26c71eac89d35ab4fb1a6d29aa8535793dde91f3))
* **controller:** check a blade's image by reading it back ([23fe726](https://github.com/mikesmitty/power-manifold/commit/23fe7269b5aa723ab156bc619187bd5b28f93e05))

## [0.5.0](https://github.com/mikesmitty/power-manifold/compare/charger-module-firmware-v0.4.1...charger-module-firmware-v0.5.0) (2026-10-15)


### Features

* **charger-module:** offer SPR AVS and take the port limit in watts ([3ad128a](https://github.com/mikesmitty/power-manifold/commit/3ad128ad53d8d045c69af8f2a13acd02e17125cd))


### Bug Fixes

* **charger-module:** keep a port that tripped hot off until it cools ([4d5b4f9](https://github.com/mikesmitty/power-manifold/commit/4d5b4f90ec300f485f6548654ecc51949c2976e8))

## [0.4.1](https://github.com/mikesmitty/power-manifold/compare/charger-module-firmware-v0.4.0...charger-module-firmware-v0.4.1) (2026-10-14)


### Bug Fixes

* **charger-module:** close the VBUS switch onto a converter already at 5 V ([06ac0ae](https://github.com/mikesmitty/power-manifold/commit/06ac0ae3df4c8cf7a0029b76228a4c27502134c0))
* **charger-module:** hold sinks without PD to the port's current limit ([8f0a2b0](https://github.com/mikesmitty/power-manifold/commit/8f0a2b0355da5794cbcf25870878144284d9fe87))

## [0.4.0](https://github.com/mikesmitty/power-manifold/compare/charger-module-firmware-v0.3.0...charger-module-firmware-v0.4.0) (2026-10-07)


### Features

* **charger-module:** count PD hard resets ([174cee5](https://github.com/mikesmitty/power-manifold/commit/174cee51109bf054efab80131634793ddfadf7ca))

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
