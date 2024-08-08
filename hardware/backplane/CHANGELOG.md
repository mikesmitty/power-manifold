# Changelog

## [0.7.5](https://github.com/mikesmitty/power-manifold/compare/backplane-v0.7.4...backplane-v0.7.5) (2024-08-08)


### Bug Fixes

* add reverse polarity protection mosfet ([2afe8da](https://github.com/mikesmitty/power-manifold/commit/2afe8dad995c7af24ff7d0db02ad513eba687658))

## [0.7.4](https://github.com/mikesmitty/power-manifold/compare/backplane-v0.7.3...backplane-v0.7.4) (2024-08-02)


### Bug Fixes

* make sure the input has a gnd ([9e89fb6](https://github.com/mikesmitty/power-manifold/commit/9e89fb69b0984d57c87f4afeee9d4b00b62a6423))

## [0.7.3](https://github.com/mikesmitty/power-manifold/compare/backplane-v0.7.2...backplane-v0.7.3) (2024-08-01)


### Bug Fixes

* disconnect switch pins from i2c mux ([43d8c17](https://github.com/mikesmitty/power-manifold/commit/43d8c177ba27d518fe33105f22653d975bff59d6))

## [0.7.2](https://github.com/mikesmitty/power-manifold/compare/backplane-v0.7.1...backplane-v0.7.2) (2024-07-23)


### Bug Fixes

* fix fan header pinouts ([1386e08](https://github.com/mikesmitty/power-manifold/commit/1386e0885fd502d65d987aebbf47d9a931889ee8))

## [0.7.1](https://github.com/mikesmitty/power-manifold/compare/backplane-v0.7.0...backplane-v0.7.1) (2024-07-10)


### Bug Fixes

* clean up traces ([181868f](https://github.com/mikesmitty/power-manifold/commit/181868fa257600ebbe6f0f09d60b0f0e6955dadc))

## [0.7.0](https://github.com/mikesmitty/pdusb/compare/backplane-v0.6.0...backplane-v0.7.0) (2024-07-09)


### Features

* give vin 8 pins ([deeca54](https://github.com/mikesmitty/pdusb/commit/deeca54b5ecd7f126030e2663d6139264055c541))
* shrink charger module outline ([051fecc](https://github.com/mikesmitty/pdusb/commit/051fecc27901af27a6ce28e297dd4c0b121d335f))


### Bug Fixes

* add decoupling caps to i2c and swd switches ([98123b7](https://github.com/mikesmitty/pdusb/commit/98123b795a20ac01e529f1a2962bae2bb5664719))
* add pullup resistor to i2c switch reset pin ([98123b7](https://github.com/mikesmitty/pdusb/commit/98123b795a20ac01e529f1a2962bae2bb5664719))
* correct i2c pin mismatch ([deeca54](https://github.com/mikesmitty/pdusb/commit/deeca54b5ecd7f126030e2663d6139264055c541))

## [0.6.0](https://github.com/mikesmitty/pdusb/compare/backplane-v0.5.2...backplane-v0.6.0) (2024-07-07)


### Features

* add 5v fan headers to backplane ([#31](https://github.com/mikesmitty/pdusb/issues/31)) ([44fe18b](https://github.com/mikesmitty/pdusb/commit/44fe18beaf30929cd54f240da6189cc2349ec306))

## [0.5.2](https://github.com/mikesmitty/pdusb/compare/backplane-v0.5.1...backplane-v0.5.2) (2024-07-04)


### Bug Fixes

* add rotation corrections ([#23](https://github.com/mikesmitty/pdusb/issues/23)) ([d28bfbb](https://github.com/mikesmitty/pdusb/commit/d28bfbb0f5f295293d51a52142758b1be5bae77d))

## [0.5.1](https://github.com/mikesmitty/pdusb/compare/backplane-v0.5.0...backplane-v0.5.1) (2024-07-03)


### Bug Fixes

* can't forget the pull-ups ([38f5258](https://github.com/mikesmitty/pdusb/commit/38f52583257ef937b0b6ace3cdfb33ebec99fc47))

## [0.5.0](https://github.com/mikesmitty/pdusb/compare/backplane-v0.4.0...backplane-v0.5.0) (2024-07-01)


### Features

* add swd circuits for firmware updates ([3fe7ef4](https://github.com/mikesmitty/pdusb/commit/3fe7ef4b14e35cb9b6d7b5f8920c50c7c5d31d5d))
* add switch to access onboard rp2040 through charge plug ([3fe7ef4](https://github.com/mikesmitty/pdusb/commit/3fe7ef4b14e35cb9b6d7b5f8920c50c7c5d31d5d))

## [0.4.0](https://github.com/mikesmitty/pdusb/compare/backplane-v0.3.0...backplane-v0.4.0) (2024-06-29)


### Features

* add uart and alert line to backplane ([36bab3b](https://github.com/mikesmitty/pdusb/commit/36bab3bd54e157625549632fdef713c6cf7a9887))

## [0.3.0](https://github.com/mikesmitty/pdusb/compare/backplane-v0.2.0...backplane-v0.3.0) (2024-06-28)


### Features

* add initial config for backplane ([c16be59](https://github.com/mikesmitty/pdusb/commit/c16be598a50d608be1e3e5ce62dde4558369b812))
