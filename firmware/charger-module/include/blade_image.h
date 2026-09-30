#pragma once

#include <stdint.h>

// The firmware image of the gen-3 charger blade, as the controller programs
// it through the STM32 ROM bootloader (AN4221 over the backplane I2C, at
// BLADE_LOADER_I2C_ADDR) and as the blade's own linker lays it out.
//
// Right after the vector table sits a header naming the image, so the
// controller can read the version and length out of a blade that is sitting
// in the bootloader and decide whether to start it or replace it. The image
// carries no checksum of its own: the controller holds the reference copy,
// and checks a blade's flash against it with the bootloader's checksum
// command (the STM32 CRC unit's defaults: polynomial 0x04C11DB7, initial
// value 0xFFFFFFFF, one 32-bit word at a time, least significant byte first).
//
// The controller writes the first flash page last, and the header's chunk of
// it last of all, so a header that reads back complete means the whole image
// before and after it was written.

#define BLADE_IMAGE_BASE          0x08000000u // where the image lives and runs
#define BLADE_IMAGE_MAX           0x10000u    // STM32G071C8: 64 KB
#define BLADE_IMAGE_PAGE          0x800u      // flash page: the erase unit
#define BLADE_IMAGE_HEADER_OFFSET 0xC0u       // past the 47-entry vector table
#define BLADE_IMAGE_MAGIC         0x4D57504Cu // "LPWM"

typedef struct {
    uint32_t magic;   // BLADE_IMAGE_MAGIC
    uint8_t  major;   // FW_VERSION
    uint8_t  minor;
    uint8_t  patch;
    uint8_t  proto;   // BLADE_PROTO_VERSION the image speaks
    uint32_t length;  // bytes from BLADE_IMAGE_BASE, a multiple of 8
    uint32_t reserved;
} blade_image_header_t;

// The STM32 device ID the bootloader reports, checked before anything is
// erased: STM32G07x / G08x.
#define BLADE_LOADER_DEVICE_ID    0x460
