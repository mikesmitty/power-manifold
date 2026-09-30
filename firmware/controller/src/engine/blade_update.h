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
// What the trip does depends on what is found (blade_bundle.h) and on the
// mode it was begun in:
//   - the flash holds the bundled version, byte for byte: start it;
//   - it holds another good image: write the bundle over it and start that
//     (MATCH), or start what is there, unchecked (KEEP, or nothing bundled);
//   - it holds nothing good: write the bundle and start it;
//   - it holds nothing and nothing is bundled: fail.
// REWRITE writes the bundle whatever is found. The write erases the image's
// pages first, the first page before the rest, and lands the first page
// last, its header chunk last of all, so a blade that lost power midway
// reads as blank next time and is simply written again.
//
// The pages are erased one command each with a pause after it: bootloader
// V11.3 on this part acknowledges an erase before the flash has finished
// it, and ST's way around that is one page at a time and 40 ms before the
// next operation (AN2606, table 108). V11.4 does not need it and does not
// mind.

// UPDATE_FAIL_*: the arg of an EVT_PROBE_FAIL whose code is PROBE_FAIL_UPDATE
#define UPDATE_FAIL_SILENT   1 // the bootloader stopped answering
#define UPDATE_FAIL_CHIP     2 // not the blade's STM32, or a bootloader protocol this driver does not speak
#define UPDATE_FAIL_NO_IMAGE 3 // nothing runnable on the blade and nothing bundled
#define UPDATE_FAIL_ERASE    4
#define UPDATE_FAIL_WRITE    5
#define UPDATE_FAIL_VERIFY   6 // written twice, still not what was sent
#define UPDATE_FAIL_LOOP     7 // the port engine's: the blade keeps coming back to the bootloader
#define UPDATE_FAIL_STUCK    8 // the port engine's: the blade runs, and the trips have not changed what they were for; left in service

typedef enum {
    BLADE_UPDATE_BUSY,    // more slices to go
    BLADE_UPDATE_STARTED, // the image is running (its Go command was accepted)
    BLADE_UPDATE_FAILED,  // blade_update_fail says why; the blade is still in its bootloader
} blade_update_status_t;

typedef enum {
    BLADE_UPDATE_KEEP,    // a good image on the blade is started as it is; only a blade without one is written
    BLADE_UPDATE_MATCH,   // the blade leaves on the bundle: started if that is what it holds, written if not
    BLADE_UPDATE_REWRITE, // the bundle is written whatever the blade holds
} blade_update_mode_t;

void blade_update_begin(uint8_t port, blade_update_mode_t mode);
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
