#include "update.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "hardware/flash.h"
#include "hardware/watchdog.h"
#include "pico/bootrom.h"

#include "boot_reason_hw.h"
#include "pico/flash.h"
#include "pico/time.h"

#include "boot/picobin.h"
#include "boot/picoboot_constants.h"
#include "boot/uf2.h"
#include "hardware/regs/addressmap.h"

#include "flash_map.h"
#include "update_image.h"

// update_image.h duplicates SDK constants so the parser stays host-testable;
// fail the build if they ever drift.
static_assert(UPD_UF2_MAGIC_START0 == UF2_MAGIC_START0, "uf2 constants drifted");
static_assert(UPD_UF2_MAGIC_START1 == UF2_MAGIC_START1, "uf2 constants drifted");
static_assert(UPD_UF2_MAGIC_END == UF2_MAGIC_END, "uf2 constants drifted");
static_assert(UPD_UF2_FLAG_NOT_MAIN_FLASH == UF2_FLAG_NOT_MAIN_FLASH, "uf2 constants drifted");
static_assert(UPD_UF2_FLAG_FAMILY_ID_PRESENT == UF2_FLAG_FAMILY_ID_PRESENT, "uf2 constants drifted");
static_assert(UPD_UF2_FAMILY_RP2350_ARM_S == RP2350_ARM_S_FAMILY_ID, "uf2 constants drifted");
static_assert(UPD_UF2_FAMILY_ABSOLUTE == ABSOLUTE_FAMILY_ID, "uf2 constants drifted");
static_assert(UPD_BLOCK_MARKER_START == PICOBIN_BLOCK_MARKER_START, "picobin constants drifted");
static_assert(UPD_BLOCK_MARKER_END == PICOBIN_BLOCK_MARKER_END, "picobin constants drifted");
static_assert(UPD_BLOCK_MAX_WORDS == PICOBIN_MAX_BLOCK_SIZE / 4, "picobin constants drifted");
static_assert(UPD_ITEM_IMAGE_TYPE == PICOBIN_BLOCK_ITEM_1BS_IMAGE_TYPE, "picobin constants drifted");
static_assert(UPD_ITEM_HASH_DEF == PICOBIN_BLOCK_ITEM_1BS_HASH_DEF, "picobin constants drifted");
static_assert(UPD_ITEM_VERSION == PICOBIN_BLOCK_ITEM_1BS_VERSION, "picobin constants drifted");
static_assert(UPD_ITEM_SIGNATURE == PICOBIN_BLOCK_ITEM_SIGNATURE, "picobin constants drifted");
static_assert(UPD_ITEM_LAST == PICOBIN_BLOCK_ITEM_2BS_LAST, "picobin constants drifted");
static_assert(UPD_IMAGE_TYPE_EXE == PICOBIN_IMAGE_TYPE_IMAGE_TYPE_EXE, "picobin constants drifted");
static_assert(UPD_IMAGE_TYPE_TBYB_BITS == PICOBIN_IMAGE_TYPE_EXE_TBYB_BITS, "picobin constants drifted");

// A wedged client can stop mid-stream without ever closing; after this much
// silence a new update request may take the session over.
#define UPDATE_STALL_MS 30000

typedef enum { FMT_UNKNOWN = 0, FMT_UF2, FMT_BIN } stream_fmt_t;

static struct {
    bool         active;
    stream_fmt_t fmt;
    uint32_t     target_off, target_size;
    flash_slot_t target_slot;
    uint32_t     cursor;    // next logical image byte (streams are contiguous)
    uint32_t     crc_tail;  // running CRC of image bytes past the first sector
    uint8_t      first[FLASH_SECTOR_SIZE];  // held back, written last
    uint8_t      acc[FLASH_SECTOR_SIZE];    // sector being assembled
    uint8_t      frame[UPD_UF2_BLOCK_SIZE]; // uf2 reframing across TCP chunks
    uint32_t     frame_len;
    uint32_t     last_ms;
    uint32_t     reboot_at_ms; // 0 = no reboot scheduled
    char         ver[16];
} up;

static uint32_t now_ms(void) {
    return to_ms_since_boot(get_absolute_time());
}

static bool eout(char *err, size_t errlen, const char *msg) {
    if (err && errlen) snprintf(err, errlen, "%s", msg);
    return false;
}

static uint32_t crc32_feed(uint32_t crc, const uint8_t *data, size_t len) {
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return crc;
}

typedef struct {
    uint32_t offset;
    const uint8_t *data; // NULL: erase only
} flash_op_t;

static void do_flash_op(void *param) {
    const flash_op_t *op = (const flash_op_t *)param;
    flash_range_erase(op->offset, FLASH_SECTOR_SIZE);
    if (op->data) flash_range_program(op->offset, op->data, FLASH_SECTOR_SIZE);
}

static bool flash_sector(uint32_t storage_off, const uint8_t *data) {
    // A sustained upload keeps core 0 busy in the lwIP callback context, so
    // the main loop may not get around to feeding the watchdog; each sector
    // written is progress enough to count.
    watchdog_update();
    flash_op_t op = {.offset = storage_off, .data = data};
    return flash_safe_execute(do_flash_op, &op, 500) == PICO_OK;
}

bool update_begin(uint32_t stream_len, char *err, size_t errlen) {
    if (up.active && update_age_ms() < UPDATE_STALL_MS)
        return eout(err, errlen, "another update is already in progress");
    if (up.reboot_at_ms)
        return eout(err, errlen, "an update is finished and about to reboot");
    if (flash_map_update_pending())
        return eout(err, errlen,
                    "running image is an uncommitted trial; wait for its commit or reboot");

    uint32_t off, size;
    flash_slot_t slot;
    if (!flash_map_update_target(&off, &size, &slot))
        return eout(err, errlen, "no A/B slot available (is the partition table flashed?)");
    if (stream_len < UPD_UF2_BLOCK_SIZE)
        return eout(err, errlen, "image too small");
    if (stream_len > 2 * size + 65536) // uf2 framing at most doubles the image
        return eout(err, errlen, "image too large for the slot");

    memset(&up, 0, sizeof(up));
    memset(up.first, 0xff, sizeof(up.first));
    memset(up.acc, 0xff, sizeof(up.acc));
    up.target_off = off;
    up.target_size = size;
    up.target_slot = slot;
    up.crc_tail = 0xFFFFFFFFu;
    strcpy(up.ver, "?");

    // Kill any stale IMAGE_DEF up front so an aborted transfer can never
    // leave the slot looking bootable.
    if (!flash_sector(up.target_off, NULL))
        return eout(err, errlen, "flash erase failed (engine not running?)");

    up.active = true;
    up.last_ms = now_ms();
    return true;
}

// Append bytes at the logical cursor; sector 0 is held back for finish, full
// sectors past it are erased+programmed as they complete.
static bool put_bytes(const uint8_t *data, uint32_t len, char *err, size_t errlen) {
    while (len) {
        if (up.cursor >= up.target_size) {
            update_abort();
            return eout(err, errlen, "image larger than the slot");
        }
        uint32_t sec_off = up.cursor & (FLASH_SECTOR_SIZE - 1);
        uint32_t room = FLASH_SECTOR_SIZE - sec_off;
        uint32_t chunk = len < room ? len : room;

        if (up.cursor < FLASH_SECTOR_SIZE) {
            memcpy(up.first + sec_off, data, chunk);
        } else {
            memcpy(up.acc + sec_off, data, chunk);
            up.crc_tail = crc32_feed(up.crc_tail, data, chunk);
            if (sec_off + chunk == FLASH_SECTOR_SIZE) {
                uint32_t base = up.cursor & ~(uint32_t)(FLASH_SECTOR_SIZE - 1);
                if (!flash_sector(up.target_off + base, up.acc)) {
                    update_abort();
                    return eout(err, errlen, "flash write failed");
                }
                memset(up.acc, 0xff, sizeof(up.acc));
            }
        }
        up.cursor += chunk;
        data += chunk;
        len -= chunk;
    }
    up.last_ms = now_ms();
    return true;
}

bool update_write(const uint8_t *data, size_t len, char *err, size_t errlen) {
    if (!up.active) return eout(err, errlen, "no update in progress");
    if (up.fmt == FMT_BIN) return put_bytes(data, (uint32_t)len, err, errlen);

    // UF2 until proven otherwise: reassemble 512-byte frames across TCP
    // chunk boundaries, sniffing the format off the first four bytes.
    while (len) {
        uint32_t want = UPD_UF2_BLOCK_SIZE - up.frame_len;
        uint32_t chunk = len < want ? (uint32_t)len : want;
        memcpy(up.frame + up.frame_len, data, chunk);
        up.frame_len += chunk;
        data += chunk;
        len -= chunk;

        if (up.fmt == FMT_UNKNOWN && up.frame_len >= 4) {
            uint32_t magic;
            memcpy(&magic, up.frame, 4);
            if (magic == UPD_UF2_MAGIC_START0) {
                up.fmt = FMT_UF2;
            } else { // raw binary (a flash image starts with a stack pointer)
                up.fmt = FMT_BIN;
                uint32_t buffered = up.frame_len;
                up.frame_len = 0;
                if (!put_bytes(up.frame, buffered, err, errlen)) return false;
                return len ? put_bytes(data, (uint32_t)len, err, errlen) : true;
            }
        }

        if (up.frame_len == UPD_UF2_BLOCK_SIZE) {
            up.frame_len = 0;
            uf2_frame_t f;
            const char *e = update_uf2_parse(up.frame, &f);
            if (e) {
                update_abort();
                return eout(err, errlen, e);
            }
            if (!f.flash) continue;
            // picotool-built UF2s are dense and ascending; anything else is
            // outside what this streaming writer supports
            if (f.target_addr != XIP_BASE + up.cursor) {
                update_abort();
                return eout(err, errlen, "non-contiguous uf2 image");
            }
            if (!put_bytes(f.data, f.payload_size, err, errlen)) return false;
        }
    }
    up.last_ms = now_ms();
    return true;
}

bool update_finish(char *err, size_t errlen) {
    if (!up.active) return eout(err, errlen, "no update in progress");
    if (up.fmt == FMT_UF2 && up.frame_len) {
        update_abort();
        return eout(err, errlen, "truncated uf2 stream");
    }
    if (up.cursor <= FLASH_SECTOR_SIZE) {
        update_abort();
        return eout(err, errlen, "image too small");
    }

    // flush the trailing partial sector (0xff-padded)
    if (up.cursor & (FLASH_SECTOR_SIZE - 1)) {
        uint32_t base = up.cursor & ~(uint32_t)(FLASH_SECTOR_SIZE - 1);
        if (!flash_sector(up.target_off + base, up.acc)) {
            update_abort();
            return eout(err, errlen, "flash write failed");
        }
    }

    image_def_t def;
    const char *e = update_image_scan(up.first, FLASH_SECTOR_SIZE, &def);
    if (e) {
        update_abort();
        return eout(err, errlen, e);
    }
    if (def.sealed && !def.tbyb) {
        update_abort();
        return eout(err, errlen, "image is hashed/signed; cannot set the trial flag");
    }
    update_image_set_tbyb(up.first, &def);
    if (def.has_version)
        snprintf(up.ver, sizeof(up.ver), "%u.%u.%u", def.ver_major,
                 def.ver_minor >> 8, def.ver_minor & 0xff);

    if (!flash_sector(up.target_off, up.first)) {
        update_abort();
        return eout(err, errlen, "flash write failed");
    }

    // Read back through the untranslated alias and verify: the first sector
    // against the patched buffer, the rest against the stream CRC.
    const uint8_t *fp = flash_map_xip_ptr(up.target_off);
    if (memcmp(fp, up.first, FLASH_SECTOR_SIZE) != 0 ||
        crc32_feed(0xFFFFFFFFu, fp + FLASH_SECTOR_SIZE,
                   up.cursor - FLASH_SECTOR_SIZE) != up.crc_tail) {
        update_abort();
        return eout(err, errlen, "flash verify failed");
    }

    up.active = false;
    return true;
}

void update_abort(void) {
    up.active = false;
    up.frame_len = 0;
}

bool update_active(void) {
    return up.active;
}

uint32_t update_age_ms(void) {
    return now_ms() - up.last_ms;
}

uint32_t update_bytes(void) {
    return up.cursor;
}

const char *update_slot_name(void) {
    return up.target_slot == FLASH_SLOT_A ? "A" : "B";
}

const char *update_version_str(void) {
    return up.ver;
}

void update_schedule_reboot(uint32_t delay_ms) {
    uint32_t t = now_ms() + delay_ms;
    up.reboot_at_ms = t ? t : 1;
}

bool update_reboot_due(void) {
    return up.reboot_at_ms && (int32_t)(now_ms() - up.reboot_at_ms) >= 0;
}

void update_reboot_now(void) {
    // Flash-update boot of the freshly written slot: the only boot path that
    // will run a TBYB-flagged image (and what arms the buy-pending flag the
    // health gate in main() later commits).
    boot_reason_mark(BOOT_UPDATE, 0, 0, 0, 0);
    rom_reboot(REBOOT2_FLAG_REBOOT_TYPE_FLASH_UPDATE | REBOOT2_FLAG_NO_RETURN_ON_SUCCESS,
               10, XIP_BASE + up.target_off, 0);
    for (;;) tight_loop_contents(); // unreachable unless the ROM call failed
}
