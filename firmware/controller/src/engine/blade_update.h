#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "manifold.h"

// A gen-3 blade in its ROM bootloader, one trip through it per port: find
// out what it is and what its flash holds, program the bundled image when
// that is called for, check the result, and start it. One slice of work per
// engine tick (blade_update_step), with the port's mux channel selected by
// the caller, so five other ports keep being served meanwhile; a whole image
// takes a few seconds.
//
// What the trip does depends on what is found (blade_bundle.h):
//   - the flash holds the bundled version, byte for byte: start it;
//   - it holds anything else, or nothing, and there is a bundle: write the
//     bundle and start it (rewrite: also over a matching image);
//   - it holds a good image and nothing is bundled: start it, unchecked;
//   - it holds nothing and nothing is bundled: fail.
// The write erases every page of the image first and lands the first page
// last, its header chunk last of all, so a blade that lost power midway
// reads as blank next time and is simply written again.

// UPDATE_FAIL_*: the arg of an EVT_PROBE_FAIL whose code is PROBE_FAIL_UPDATE
#define UPDATE_FAIL_SILENT   1 // the bootloader stopped answering
#define UPDATE_FAIL_CHIP     2 // not the blade's STM32, or a bootloader protocol this driver does not speak
#define UPDATE_FAIL_NO_IMAGE 3 // nothing runnable on the blade and nothing bundled
#define UPDATE_FAIL_ERASE    4
#define UPDATE_FAIL_WRITE    5
#define UPDATE_FAIL_VERIFY   6 // written twice, still not what was sent
#define UPDATE_FAIL_LOOP     7 // the port engine's: the blade keeps coming back to the bootloader

typedef enum {
    BLADE_UPDATE_BUSY,    // more slices to go
    BLADE_UPDATE_STARTED, // the image is running (its Go command was accepted)
    BLADE_UPDATE_FAILED,  // blade_update_fail says why; the blade is still in its bootloader
} blade_update_status_t;

void blade_update_begin(uint8_t port, bool rewrite);
blade_update_status_t blade_update_step(uint8_t port);
uint8_t  blade_update_pct(uint8_t port);     // of the image written, 0-100
uint8_t  blade_update_fail(uint8_t port);    // UPDATE_FAIL_*, after FAILED
bool     blade_update_wrote(uint8_t port);   // after STARTED: the bundle was programmed (else what was found was started)
uint32_t blade_update_version(uint8_t port); // after STARTED: UPDATE_VERSION() of what runs

// The STM32 CRC unit's checksum of `len` bytes of an image, as the
// bootloader's checksum command computes it: CRC-32 (0x04C11DB7) from
// 0xFFFFFFFF over 32-bit words, no reflection, each word's bytes least
// significant first. Bytes past `size` are the erased flash's 0xFF.
uint32_t blade_image_crc(const uint8_t *data, uint32_t size, uint32_t len);
