#include <string.h>

#include "microtest.h"
#include "update_image.h"

// Buffers mirror the block pico_crt0 embeds in real builds (verified against
// controller.bin): marker, IMAGE_TYPE item, VERSION item, LAST, link, end
// marker. Image type flags 0x1021 = EXE | secure | RP2350.

#define REAL_EXE_FLAGS 0x1021u

static uint32_t wr32(uint8_t *b, uint32_t off, uint32_t v) {
    b[off] = (uint8_t)v;
    b[off + 1] = (uint8_t)(v >> 8);
    b[off + 2] = (uint8_t)(v >> 16);
    b[off + 3] = (uint8_t)(v >> 24);
    return off + 4;
}

static uint32_t rd32(const uint8_t *b, uint32_t off) {
    return (uint32_t)b[off] | ((uint32_t)b[off + 1] << 8) |
           ((uint32_t)b[off + 2] << 16) | ((uint32_t)b[off + 3] << 24);
}

static uint32_t emit_block(uint8_t *buf, uint32_t off, uint16_t type_flags,
                           bool with_version, uint32_t version, bool with_hash) {
    off = wr32(buf, off, UPD_BLOCK_MARKER_START);
    off = wr32(buf, off, UPD_ITEM_IMAGE_TYPE | (1u << 8) | ((uint32_t)type_flags << 16));
    uint32_t words = 1;
    if (with_version) {
        off = wr32(buf, off, UPD_ITEM_VERSION | (2u << 8));
        off = wr32(buf, off, version);
        words += 2;
    }
    if (with_hash) {
        off = wr32(buf, off, UPD_ITEM_HASH_DEF | (2u << 8));
        off = wr32(buf, off, 0xdeadbeef);
        words += 2;
    }
    off = wr32(buf, off, UPD_ITEM_LAST | (words << 8));
    off = wr32(buf, off, 0);                    // block loop link
    off = wr32(buf, off, UPD_BLOCK_MARKER_END);
    return off;
}

static void test_scan_finds_real_layout(void) {
    uint8_t buf[4096];
    memset(buf, 0xa5, sizeof(buf));
    // decoy: a bare marker word with garbage after it, before the real block
    wr32(buf, 0x40, UPD_BLOCK_MARKER_START);
    emit_block(buf, 0x134, REAL_EXE_FLAGS, true, 0x00000100u, false); // v0.1.0

    image_def_t def;
    MT_ASSERT(update_image_scan(buf, sizeof(buf), &def) == NULL);
    MT_ASSERT_EQ(def.image_type_word_off, 0x138);
    MT_ASSERT(!def.tbyb);
    MT_ASSERT(!def.sealed);
    MT_ASSERT(def.has_version);
    MT_ASSERT_EQ(def.ver_major, 0);
    MT_ASSERT_EQ(def.ver_minor, 256); // minor y*256+z: 0.1.0 -> 256
}

static void test_set_tbyb_flips_only_that_bit(void) {
    uint8_t buf[512];
    memset(buf, 0xff, sizeof(buf));
    emit_block(buf, 0, REAL_EXE_FLAGS, true, 0x00030201u, false);

    image_def_t def;
    MT_ASSERT(update_image_scan(buf, sizeof(buf), &def) == NULL);
    uint32_t before = rd32(buf, def.image_type_word_off);
    update_image_set_tbyb(buf, &def);
    uint32_t after = rd32(buf, def.image_type_word_off);
    MT_ASSERT_EQ(after, before | ((uint32_t)UPD_IMAGE_TYPE_TBYB_BITS << 16));

    image_def_t def2;
    MT_ASSERT(update_image_scan(buf, sizeof(buf), &def2) == NULL);
    MT_ASSERT(def2.tbyb);
    MT_ASSERT_EQ(def2.ver_major, 3);
    MT_ASSERT_EQ(def2.ver_minor, 0x0201);
}

static void test_scan_rejects_junk(void) {
    uint8_t buf[512];
    memset(buf, 0x5a, sizeof(buf));
    image_def_t def;
    MT_ASSERT(update_image_scan(buf, sizeof(buf), &def) != NULL);

    // marker so close to the end the block cannot fit
    memset(buf, 0, sizeof(buf));
    wr32(buf, sizeof(buf) - 8, UPD_BLOCK_MARKER_START);
    MT_ASSERT(update_image_scan(buf, sizeof(buf), &def) != NULL);
}

static void test_scan_rejects_non_exe(void) {
    uint8_t buf[512];
    memset(buf, 0, sizeof(buf));
    emit_block(buf, 0, 0x0002 /* IMAGE_TYPE_DATA */, false, 0, false);
    image_def_t def;
    MT_ASSERT(update_image_scan(buf, sizeof(buf), &def) != NULL);
}

static void test_scan_flags_sealed(void) {
    uint8_t buf[512];
    memset(buf, 0, sizeof(buf));
    emit_block(buf, 0, REAL_EXE_FLAGS, false, 0, true);
    image_def_t def;
    MT_ASSERT(update_image_scan(buf, sizeof(buf), &def) == NULL);
    MT_ASSERT(def.sealed);
    MT_ASSERT(!def.has_version);
}

static void test_scan_rejects_bad_last_size(void) {
    uint8_t buf[512];
    memset(buf, 0, sizeof(buf));
    uint32_t off = wr32(buf, 0, UPD_BLOCK_MARKER_START);
    off = wr32(buf, off, UPD_ITEM_IMAGE_TYPE | (1u << 8) | (REAL_EXE_FLAGS << 16));
    off = wr32(buf, off, UPD_ITEM_LAST | (7u << 8)); // claims 7 words, saw 1
    off = wr32(buf, off, 0);
    wr32(buf, off, UPD_BLOCK_MARKER_END);
    image_def_t def;
    MT_ASSERT(update_image_scan(buf, sizeof(buf), &def) != NULL);
}

static uint32_t emit_uf2(uint8_t *b, uint32_t flags, uint32_t addr,
                         uint32_t payload, uint32_t family) {
    memset(b, 0, UPD_UF2_BLOCK_SIZE);
    wr32(b, 0, UPD_UF2_MAGIC_START0);
    wr32(b, 4, UPD_UF2_MAGIC_START1);
    wr32(b, 8, flags);
    wr32(b, 12, addr);
    wr32(b, 16, payload);
    wr32(b, 20, 3);  // blockNo
    wr32(b, 24, 12); // numBlocks
    wr32(b, 28, family);
    wr32(b, 508, UPD_UF2_MAGIC_END);
    return 0;
}

static void test_uf2_parse_good_block(void) {
    uint8_t b[UPD_UF2_BLOCK_SIZE];
    emit_uf2(b, UPD_UF2_FLAG_FAMILY_ID_PRESENT, 0x10000300u, 256,
             UPD_UF2_FAMILY_RP2350_ARM_S);
    uf2_frame_t f;
    MT_ASSERT(update_uf2_parse(b, &f) == NULL);
    MT_ASSERT(f.flash);
    MT_ASSERT_EQ(f.target_addr, 0x10000300u);
    MT_ASSERT_EQ(f.payload_size, 256);
    MT_ASSERT_EQ(f.block_no, 3);
    MT_ASSERT_EQ(f.num_blocks, 12);
    MT_ASSERT(f.data == b + 32);
}

static void test_uf2_parse_rejects(void) {
    uint8_t b[UPD_UF2_BLOCK_SIZE];
    uf2_frame_t f;

    // wrong family (absolute, i.e. the partition table image)
    emit_uf2(b, UPD_UF2_FLAG_FAMILY_ID_PRESENT, 0x10000000u, 256, 0xe48bff57u);
    MT_ASSERT(update_uf2_parse(b, &f) != NULL);

    // family flag missing entirely
    emit_uf2(b, 0, 0x10000000u, 256, UPD_UF2_FAMILY_RP2350_ARM_S);
    MT_ASSERT(update_uf2_parse(b, &f) != NULL);

    // corrupt end magic
    emit_uf2(b, UPD_UF2_FLAG_FAMILY_ID_PRESENT, 0x10000000u, 256,
             UPD_UF2_FAMILY_RP2350_ARM_S);
    wr32(b, 508, 0x12345678u);
    MT_ASSERT(update_uf2_parse(b, &f) != NULL);

    // oversized payload
    emit_uf2(b, UPD_UF2_FLAG_FAMILY_ID_PRESENT, 0x10000000u, 477,
             UPD_UF2_FAMILY_RP2350_ARM_S);
    MT_ASSERT(update_uf2_parse(b, &f) != NULL);
}

static void test_uf2_skips_not_main_flash(void) {
    uint8_t b[UPD_UF2_BLOCK_SIZE];
    // no family id at all: must still pass, flagged as skippable
    emit_uf2(b, UPD_UF2_FLAG_NOT_MAIN_FLASH, 0, 0, 0);
    uf2_frame_t f;
    MT_ASSERT(update_uf2_parse(b, &f) == NULL);
    MT_ASSERT(!f.flash);
}

static void test_uf2_skips_e10_marker(void) {
    uint8_t b[UPD_UF2_BLOCK_SIZE];
    // picotool's leading erratum block: absolute family at the marker address
    emit_uf2(b, UPD_UF2_FLAG_FAMILY_ID_PRESENT, UPD_UF2_E10_MARKER_ADDR, 256,
             UPD_UF2_FAMILY_ABSOLUTE);
    uf2_frame_t f;
    MT_ASSERT(update_uf2_parse(b, &f) == NULL);
    MT_ASSERT(!f.flash);

    // but an absolute-family block anywhere else stays rejected
    emit_uf2(b, UPD_UF2_FLAG_FAMILY_ID_PRESENT, 0x10000000u, 256,
             UPD_UF2_FAMILY_ABSOLUTE);
    MT_ASSERT(update_uf2_parse(b, &f) != NULL);
}

void run_update_image_tests(void) {
    mt_run("update_image: scan finds real crt0 layout", test_scan_finds_real_layout);
    mt_run("update_image: set_tbyb flips only that bit", test_set_tbyb_flips_only_that_bit);
    mt_run("update_image: scan rejects junk/truncation", test_scan_rejects_junk);
    mt_run("update_image: scan rejects non-exe images", test_scan_rejects_non_exe);
    mt_run("update_image: scan flags hashed images", test_scan_flags_sealed);
    mt_run("update_image: scan rejects bad LAST size", test_scan_rejects_bad_last_size);
    mt_run("update_image: uf2 good block", test_uf2_parse_good_block);
    mt_run("update_image: uf2 rejects wrong family/magic", test_uf2_parse_rejects);
    mt_run("update_image: uf2 skips not-main-flash", test_uf2_skips_not_main_flash);
    mt_run("update_image: uf2 skips the E10 marker block", test_uf2_skips_e10_marker);
}
