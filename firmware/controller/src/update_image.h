#pragma once

#include <stdbool.h>
#include <stdint.h>

// Pure parsing/patching helpers for incoming OTA images: UF2 framing and the
// picobin IMAGE_DEF block embedded in an RP2350 executable. Deliberately free
// of pico-sdk dependencies so the host test suite can exercise them; the
// constants mirror boot/uf2.h and boot/picobin.h, and update.c static_asserts
// that they agree with the SDK.

#define UPD_UF2_BLOCK_SIZE   512u
#define UPD_UF2_MAGIC_START0 0x0A324655u
#define UPD_UF2_MAGIC_START1 0x9E5D5157u
#define UPD_UF2_MAGIC_END    0x0AB16F30u

#define UPD_UF2_FLAG_NOT_MAIN_FLASH    0x00000001u
#define UPD_UF2_FLAG_FAMILY_ID_PRESENT 0x00002000u
#define UPD_UF2_FAMILY_RP2350_ARM_S    0xe48bff59u
#define UPD_UF2_FAMILY_ABSOLUTE        0xe48bff57u

// picotool prepends every RP2350 UF2 with one absolute-family block at the
// last page of flash address space (the RP2350-E10 erratum workaround); the
// bootrom drops it and so do we.
#define UPD_UF2_E10_MARKER_ADDR 0x10ffff00u

typedef struct {
    uint32_t target_addr;  // absolute flash address (0x10000000-based)
    uint32_t payload_size;
    const uint8_t *data;   // points into the caller's block
    uint32_t block_no;
    uint32_t num_blocks;
    bool     flash;        // false: not-main-flash block, skip silently
} uf2_frame_t;

// Validates one 512-byte UF2 block; the family must be rp2350-arm-s.
// Returns NULL on success (out filled), else a static error string.
const char *update_uf2_parse(const uint8_t *block, uf2_frame_t *out);

#define UPD_BLOCK_MARKER_START 0xffffded3u
#define UPD_BLOCK_MARKER_END   0xab123579u
#define UPD_BLOCK_MAX_WORDS    (0x280u / 4u) // PICOBIN_MAX_BLOCK_SIZE

#define UPD_ITEM_IMAGE_TYPE 0x42u
#define UPD_ITEM_HASH_DEF   0x47u
#define UPD_ITEM_VERSION    0x48u
#define UPD_ITEM_SIGNATURE  0x09u
#define UPD_ITEM_LAST       0xffu

#define UPD_IMAGE_TYPE_EXE_MASK  0x000fu
#define UPD_IMAGE_TYPE_EXE       0x0001u
#define UPD_IMAGE_TYPE_TBYB_BITS 0x8000u

typedef struct {
    uint32_t image_type_word_off; // byte offset of the IMAGE_TYPE item word
    bool     tbyb;                // TBYB flag already set
    bool     sealed;              // hashed/signed: patching would invalidate it
    bool     has_version;
    uint16_t ver_major;
    uint16_t ver_minor;           // encodes y*256+z of x.y.z (see CMakeLists)
} image_def_t;

// Scans buf (an image's first flash sector) for the picobin IMAGE_DEF block
// the bootrom would find. Returns NULL on success, else an error string.
const char *update_image_scan(const uint8_t *buf, uint32_t len, image_def_t *out);

// Sets the TBYB flag on a block previously located by update_image_scan.
void update_image_set_tbyb(uint8_t *buf, const image_def_t *def);
