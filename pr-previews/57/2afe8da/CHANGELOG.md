# Changelog

## [0.11.4](https://github.com/mikesmitty/power-manifold/compare/charger-module-v0.11.3...charger-module-v0.11.4) (2024-08-01)


### Bug Fixes

* correct rp2 usb pin order ([d259270](https://github.com/mikesmitty/power-manifold/commit/d2592703c32c2d982217669368ba1caed00b513d))

## [0.11.3](https://github.com/mikesmitty/power-manifold/compare/charger-module-v0.11.2...charger-module-v0.11.3) (2024-07-23)


### Bug Fixes

* increase fuse to 8A to match input ([34cf64f](https://github.com/mikesmitty/power-manifold/commit/34cf64f5ec75cb4e9e9030e484891ce500b91830))

## [0.11.2](https://github.com/mikesmitty/power-manifold/compare/charger-module-v0.11.1...charger-module-v0.11.2) (2024-07-21)


### Bug Fixes

* open up path from usb gnd pins to plane ([735a073](https://github.com/mikesmitty/power-manifold/commit/735a073ddeeeeb4ab86238208ebdab3d418ca550))

## [0.11.1](https://github.com/mikesmitty/power-manifold/compare/charger-module-v0.11.0...charger-module-v0.11.1) (2024-07-11)


### Bug Fixes

* swap usb tvs diode to more available model ([c8b18fa](https://github.com/mikesmitty/power-manifold/commit/c8b18fa9c991078d95106d4458649c13adc903e4))

## [0.11.0](https://github.com/mikesmitty/power-manifold/compare/charger-module-v0.10.2...charger-module-v0.11.0) (2024-07-10)


### Features

* add gpio header to charger module ([4cbd2f3](https://github.com/mikesmitty/power-manifold/commit/4cbd2f3b8f6b9d3d9eb857e1141b53decd266e08))

## [0.10.2](https://github.com/mikesmitty/power-manifold/compare/charger-module-v0.10.1...charger-module-v0.10.2) (2024-07-10)


### Bug Fixes

* clean up traces ([181868f](https://github.com/mikesmitty/power-manifold/commit/181868fa257600ebbe6f0f09d60b0f0e6955dadc))

## [0.10.1](https://github.com/mikesmitty/pdusb/compare/charger-module-v0.10.0...charger-module-v0.10.1) (2024-07-10)


### Bug Fixes

* add via stitching to improve emc ([1808a28](https://github.com/mikesmitty/pdusb/commit/1808a28883a0908c73fe8574c44388933ec490f3))
* remove over-complicated USB switch ([b711b1c](https://github.com/mikesmitty/pdusb/commit/b711b1c423c8949ec734186f52588050cb6238bb))

## [0.10.0](https://github.com/mikesmitty/pdusb/compare/charger-module-v0.9.0...charger-module-v0.10.0) (2024-07-09)


### Features

* give vin 8 pins ([deeca54](https://github.com/mikesmitty/pdusb/commit/deeca54b5ecd7f126030e2663d6139264055c541))


### Bug Fixes

* correct i2c pin mismatch ([deeca54](https://github.com/mikesmitty/pdusb/commit/deeca54b5ecd7f126030e2663d6139264055c541))

## [0.9.0](https://github.com/mikesmitty/pdusb/compare/charger-module-v0.8.3...charger-module-v0.9.0) (2024-07-08)


### Features

* shrink charger module outline ([051fecc](https://github.com/mikesmitty/pdusb/commit/051fecc27901af27a6ce28e297dd4c0b121d335f))

## [0.8.3](https://github.com/mikesmitty/pdusb/compare/charger-module-v0.8.2...charger-module-v0.8.3) (2024-07-07)


### Bug Fixes

* fix opamp feedback resistor value ([51daa8c](https://github.com/mikesmitty/pdusb/commit/51daa8c1bc127483105343635c577d885270d42c))
* remove mosfet ([#30](https://github.com/mikesmitty/pdusb/issues/30)) ([f38021d](https://github.com/mikesmitty/pdusb/commit/f38021d279a5d4fb5c75b13646da387f04ae2dde))

## [0.8.2](https://github.com/mikesmitty/pdusb/compare/charger-module-v0.8.1...charger-module-v0.8.2) (2024-07-04)


### Bug Fixes

* fix incorrect part numbers ([7d5589f](https://github.com/mikesmitty/pdusb/commit/7d5589fe6c76b37504af899d7f41072b2987b070))

## [0.8.1](https://github.com/mikesmitty/pdusb/compare/charger-module-v0.8.0...charger-module-v0.8.1) (2024-07-04)


### Bug Fixes

* add rotation corrections ([#23](https://github.com/mikesmitty/pdusb/issues/23)) ([d28bfbb](https://github.com/mikesmitty/pdusb/commit/d28bfbb0f5f295293d51a52142758b1be5bae77d))

## [0.8.0](https://github.com/mikesmitty/pdusb/compare/charger-module-v0.7.1...charger-module-v0.8.0) (2024-07-04)


### Features

* add amplifier to current signal ([cd6c652](https://github.com/mikesmitty/pdusb/commit/cd6c6524c9f327b2ad43f052ae8103018f5e6368))

## [0.7.1](https://github.com/mikesmitty/pdusb/compare/charger-module-v0.7.0...charger-module-v0.7.1) (2024-07-03)


### Bug Fixes

* can't forget the pull-ups ([38f5258](https://github.com/mikesmitty/pdusb/commit/38f52583257ef937b0b6ace3cdfb33ebec99fc47))

## [0.7.0](https://github.com/mikesmitty/pdusb/compare/charger-module-v0.6.0...charger-module-v0.7.0) (2024-07-01)


### Features

* add swd circuits for firmware updates ([3fe7ef4](https://github.com/mikesmitty/pdusb/commit/3fe7ef4b14e35cb9b6d7b5f8920c50c7c5d31d5d))
* add swd lines to connector ([0cbfca7](https://github.com/mikesmitty/pdusb/commit/0cbfca78ad04f3e7deddfba9d1997bf1806fe2ab))
* add switch to access onboard rp2040 through charge plug ([3fe7ef4](https://github.com/mikesmitty/pdusb/commit/3fe7ef4b14e35cb9b6d7b5f8920c50c7c5d31d5d))
* add usb switch for rp2040 usb port ([0cbfca7](https://github.com/mikesmitty/pdusb/commit/0cbfca78ad04f3e7deddfba9d1997bf1806fe2ab))

## [0.6.0](https://github.com/mikesmitty/pdusb/compare/charger-module-v0.5.0...charger-module-v0.6.0) (2024-06-29)


### Features

* misc features ([#8](https://github.com/mikesmitty/pdusb/issues/8)) ([c4a45ae](https://github.com/mikesmitty/pdusb/commit/c4a45aea12a5c59c5742778317859835ea6f5bef))

## [0.5.0](https://github.com/mikesmitty/pdusb/compare/charger-module-v0.4.0...charger-module-v0.5.0) (2024-06-28)


### Features

* add charger module ([e0088c0](https://github.com/mikesmitty/pdusb/commit/e0088c0316c7c99f1039f19b1d478e8ac7130546))
