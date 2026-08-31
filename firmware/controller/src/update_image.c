#include "update_image.h"

#include <string.h>

static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

const char *update_uf2_parse(const uint8_t *block, uf2_frame_t *out) {
    if (rd32(block) != UPD_UF2_MAGIC_START0 ||
        rd32(block + 4) != UPD_UF2_MAGIC_START1 ||
        rd32(block + 508) != UPD_UF2_MAGIC_END)
        return "bad uf2 block magic";

    uint32_t flags = rd32(block + 8);
    out->target_addr = rd32(block + 12);
    out->payload_size = rd32(block + 16);
    out->block_no = rd32(block + 20);
    out->num_blocks = rd32(block + 24);
    out->data = block + 32;
    out->flash = !(flags & UPD_UF2_FLAG_NOT_MAIN_FLASH);
    if (!out->flash) return NULL; // comment/container block: caller skips it

    if (out->payload_size == 0 || out->payload_size > 476)
        return "bad uf2 payload size";
    if (!(flags & UPD_UF2_FLAG_FAMILY_ID_PRESENT))
        return "uf2 family is not rp2350-arm-s";
    uint32_t family = rd32(block + 28);
    if (family == UPD_UF2_FAMILY_ABSOLUTE &&
        out->target_addr == UPD_UF2_E10_MARKER_ADDR) {
        out->flash = false; // E10 erratum marker block, not image data
        return NULL;
    }
    if (family != UPD_UF2_FAMILY_RP2350_ARM_S)
        return "uf2 family is not rp2350-arm-s";
    return NULL;
}

// Item header word: [7:0] type; size in words at [15:8] (or [23:8] when type
// bit 7 marks a two-byte size); remaining top bits are item data. The LAST
// item's size field holds the word count of everything before it.
static const char *parse_block(const uint8_t *buf, uint32_t len, uint32_t off,
                               image_def_t *out) {
    memset(out, 0, sizeof(*out));
    uint32_t idx = off + 4; // past the start marker
    uint32_t words = 0;
    bool have_type = false;

    for (;;) {
        if (idx + 4 > len) return "image block truncated";
        uint32_t w = rd32(buf + idx);
        uint32_t type = w & 0xff;

        if (type == UPD_ITEM_LAST) {
            if (((w >> 8) & 0xffff) != words) return "image block length mismatch";
            if (idx + 12 > len) return "image block truncated";
            if (rd32(buf + idx + 8) != UPD_BLOCK_MARKER_END) // idx+4 is the loop link
                return "image block end marker missing";
            break;
        }

        uint32_t size = (type & 0x80) ? ((w >> 8) & 0xffff) : ((w >> 8) & 0xff);
        if (size == 0) return "zero-size image block item";

        switch (type) {
            case UPD_ITEM_IMAGE_TYPE: {
                uint16_t flags = (uint16_t)(w >> 16);
                if ((flags & UPD_IMAGE_TYPE_EXE_MASK) != UPD_IMAGE_TYPE_EXE)
                    return "not an executable image";
                out->image_type_word_off = idx;
                out->tbyb = (flags & UPD_IMAGE_TYPE_TBYB_BITS) != 0;
                have_type = true;
                break;
            }
            case UPD_ITEM_VERSION:
                if (size >= 2 && idx + 8 <= len) {
                    uint32_t v = rd32(buf + idx + 4);
                    out->ver_major = (uint16_t)(v >> 16);
                    out->ver_minor = (uint16_t)v;
                    out->has_version = true;
                }
                break;
            case UPD_ITEM_HASH_DEF:
            case UPD_ITEM_SIGNATURE:
                out->sealed = true;
                break;
            default:
                break;
        }

        words += size;
        idx += size * 4;
        if (words > UPD_BLOCK_MAX_WORDS) return "image block too large";
    }

    if (!have_type) return "image block has no image type";
    return NULL;
}

const char *update_image_scan(const uint8_t *buf, uint32_t len, image_def_t *out) {
    const char *err = "no image definition found";
    for (uint32_t off = 0; off + 16 <= len; off += 4) {
        if (rd32(buf + off) != UPD_BLOCK_MARKER_START) continue;
        const char *e = parse_block(buf, len, off, out);
        if (!e) return NULL;
        err = e; // a stray marker word parses as garbage; keep scanning
    }
    return err;
}

void update_image_set_tbyb(uint8_t *buf, const image_def_t *def) {
    // TBYB is bit 15 of the flags halfword at [31:16] of the item word
    buf[def->image_type_word_off + 3] |= (uint8_t)(UPD_IMAGE_TYPE_TBYB_BITS >> 8);
}
