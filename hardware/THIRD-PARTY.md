# Third-party material in the hardware designs

The designs in this directory are published under CERN-OHL-P-2.0
([LICENSE-hardware](../LICENSE-hardware)). The parts listed here come from
others and keep their own licenses. Each entry gives the notices its license
asks for; the license texts are in [`licenses/`](licenses/).

## KiCad libraries

The board files use symbols, footprints and 3D models from the
[KiCad libraries](https://gitlab.com/kicad/libraries), and the board
exports in `CAD/` contain the KiCad 3D models. The libraries' exception
covers designs that use them and the files generated from those designs.

The following footprints in `libraries/Local Library.pretty/` are copies of
KiCad library footprints, some of them changed since; the history of each
file records how. As a redistributed part of the libraries they stay under
the KiCad libraries' license, CC BY-SA 4.0, and not under CERN-OHL-P-2.0:
`AMASS_XT30PW-M_1x02_P2.50mm_Horizontal`, `BUS_PCIexpress_x1`,
`C_0402_1005Metric`, `C_0603_1608Metric`, `C_0805_2012Metric`,
`C_1206_3216Metric`, `CP_Elec_6.3x5.4_Nichicon`, `CP_Elec_6.3x7.7`,
`D_SMA`, `D_SMB`, `D_SMC`, `D_SOD-323`,
`Diodes_UDFN-10_1.0x2.5mm_P0.5mm`, `FanPinHeader_1x03_P2.54mm_Vertical`,
`Fuse_1206_3216Metric`, `FuseHolder_Blade_ATO_Littelfuse_FLR_178.6165`,
`Fuseholder_Blade_Mini_Keystone_3568`,
`JST_XH_S3B-XH-A-1_1x03_P2.50mm_Horizontal`, `L_Sunlord_MWSA0603S`,
`L_Sunlord_MWSA0804S`, `LED_0603_1608Metric`,
`LED_WS2812B-2020_PLCC4_2.0x2.0mm`,
`Molex_Micro-Fit_3.0_43045-0600_2x03_P3.00mm_Horizontal`,
`Molex_Micro-Fit_3.0_43045-0812_2x04_P3.00mm_Vertical`,
`MountingHole_4.3mm_M4_DIN965_Pad`, `MountingHole_4.3mm_M4_Pad_Via`,
`PinSocket_1x02_P2.54mm_Vertical`, `R_0402_1005Metric`,
`R_0603_1608Metric`, `R_2512_6332Metric`, `R_Array_Concave_4x0402`,
`SOT-23`, `SOT-23-6`, `TI_SO-PowerPAD-8_ThermalVias`, `TO-263-2`,
`TSSOP-10_3x3mm_P0.5mm`, `TSSOP-16_4.4x5mm_P0.65mm`,
`TSSOP-24_4.4x7.8mm_P0.65mm` and `USB_C_Receptacle_XKB_U262-16XN-4BVC11`.

- Source: the KiCad libraries, by the KiCad library contributors,
  <https://gitlab.com/kicad/libraries>
- License: CC BY-SA 4.0 with the KiCad libraries' exception,
  [`licenses/KiCad-libraries.md`](licenses/KiCad-libraries.md) and
  [`licenses/CC-BY-SA-4.0.txt`](licenses/CC-BY-SA-4.0.txt)
- The libraries come without warranty of any kind, express or implied.

## Raspberry Pi minimal RP2350A design

The controller's RP2350A circuit is based on Raspberry Pi's minimal design
example:

- the symbols from the `RPI_DB` library in `controller/controller.kicad_sch`:
  the RP2350A, the flash, the crystal, the inductor, a connector and passive
  parts;
- the `C_0402_Wide` footprint in `libraries/Local Library.pretty/`;
- the RP2350A 3D model `rpi_rp2350a.step`, embedded in
  `controller/controller.kicad_pcb` and contained in `CAD/controller.step`.

Source: "RP2350A QFN-60 Minimal Design Example",
<https://datasheets.raspberrypi.com/rp2350/Minimal-KiCAD.zip>.
Copyright (c) 2026 Raspberry Pi Ltd. MIT License,
[`licenses/RaspberryPi-MIT.txt`](licenses/RaspberryPi-MIT.txt).

## Raspberry Pi RM2 module model

The RM2 radio module 3D model `rpi_rmc20452t.step`, embedded in
`controller/controller.kicad_pcb` and contained in `CAD/controller.step`.

Source: Raspberry Pi design files RP-008965-DP-1,
<https://pip.raspberrypi.com/categories/1223-design-files>, used under
Raspberry Pi's terms for its design files,
<https://www.raspberrypi.com/licensing/>: "Permission to use, copy, modify,
and/or distribute these designs for any purpose with or without fee is
hereby granted."

## marbastlib WS2812 2020 model

The WS2812 2020 LED 3D model `LED_WS2812_2020.step`, embedded in
`libraries/Local Library.pretty/LED_WS2812B-2020_PLCC4_2.0x2.0mm.kicad_mod`
and `status-strip/status-strip.kicad_pcb`, and contained in
`CAD/status-strip.step`. Unmodified.

Source: marbastlib, by ebastler, <https://github.com/ebastler/marbastlib>,
file `3dmodels/LED_WS2812_2020.step` as of marbastlib v7. License:
CERN-OHL-P-2.0, the same license as these designs
([LICENSE-hardware](../LICENSE-hardware)).

## Ubuntu font

Ubuntu Regular and Ubuntu Bold, version 0.83, embedded in
`backplane/backplane.kicad_pcb`, `controller/controller.kicad_pcb` and
`status-strip/status-strip.kicad_pcb` for the silkscreen text.

Copyright 2011 Canonical Ltd. Licensed under the Ubuntu Font Licence 1.0,
[`licenses/UFL-1.0.txt`](licenses/UFL-1.0.txt). Ubuntu and Canonical are
registered trademarks of Canonical Ltd.
