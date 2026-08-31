#include "flash_map.h"

#include <string.h>

#include "pico/bootrom.h"

#include "boot/picobin.h"
#include "hardware/regs/addressmap.h"

#define MAX_PARTITIONS 16

typedef struct {
    uint32_t offset;
    uint32_t size;
    uint64_t id;
    bool     has_id;
} partition_t;

static partition_t parts[MAX_PARTITIONS];
static uint8_t     part_count;
static bool        have_pt;
static int8_t      boot_partition = -1;
static bool        buy_pending;

// Work area sized for rom_explicit_buy (4KB); rom_load_partition_table needs
// less (3.25KB). Both run one-shot on core 0, so sharing is safe.
static uint8_t __attribute__((aligned(4))) workarea[4096];

static void parse_partition_table(void) {
    // 1 flags-echo word + 3 PT_INFO words + per partition: location+flags (2)
    // and id (2, present only when the partition has one)
    static uint32_t buf[4 + MAX_PARTITIONS * 4];
    const uint32_t flags = PT_INFO_PT_INFO | PT_INFO_PARTITION_LOCATION_AND_FLAGS |
                           PT_INFO_PARTITION_ID;

    int words = rom_get_partition_table_info(buf, count_of(buf), flags);
    if (words < 0) {
        // Not resident (e.g. watchdog reboot): ask the bootrom to reload it.
        if (rom_load_partition_table(workarea, sizeof(workarea), false) != BOOTROM_OK)
            return;
        words = rom_get_partition_table_info(buf, count_of(buf), flags);
    }
    if (words < 4 || (buf[0] & flags) != flags) return;

    uint32_t count = buf[1] & 0xff;
    if (count > MAX_PARTITIONS) count = MAX_PARTITIONS;
    uint32_t idx = 4; // skip flags echo, count, unpartitioned loc + flags

    for (uint32_t p = 0; p < count; p++) {
        if (idx + 2 > (uint32_t)words) return;
        uint32_t loc = buf[idx++];
        uint32_t pflags = buf[idx++];
        uint32_t first = (loc & PICOBIN_PARTITION_LOCATION_FIRST_SECTOR_BITS) >>
                         PICOBIN_PARTITION_LOCATION_FIRST_SECTOR_LSB;
        uint32_t last = (loc & PICOBIN_PARTITION_LOCATION_LAST_SECTOR_BITS) >>
                        PICOBIN_PARTITION_LOCATION_LAST_SECTOR_LSB;
        partition_t *part = &parts[p];
        part->offset = first * 4096;
        part->size = (last - first + 1) * 4096;
        if (pflags & PICOBIN_PARTITION_FLAGS_HAS_ID_BITS) {
            if (idx + 2 > (uint32_t)words) return;
            part->id = buf[idx] | ((uint64_t)buf[idx + 1] << 32);
            part->has_id = true;
            idx += 2;
        }
        part_count = (uint8_t)(p + 1);
    }
    have_pt = part_count > 0;
}

void flash_map_init(void) {
    boot_info_t info;
    if (rom_get_boot_info(&info)) {
        boot_partition = info.partition;
        buy_pending = (info.tbyb_and_update_info & BOOT_TBYB_AND_UPDATE_FLAG_BUY_PENDING) != 0;
    }
    parse_partition_table();
}

bool flash_map_has_partition_table(void) {
    return have_pt;
}

bool flash_map_find(uint64_t id, uint32_t *offset, uint32_t *size) {
    for (uint8_t i = 0; i < part_count; i++) {
        if (parts[i].has_id && parts[i].id == id) {
            if (offset) *offset = parts[i].offset;
            if (size) *size = parts[i].size;
            return true;
        }
    }
    return false;
}

flash_slot_t flash_map_boot_slot(void) {
    if (!have_pt || boot_partition < 0) return FLASH_SLOT_RAW;
    if (boot_partition == 0) return FLASH_SLOT_A;
    if (boot_partition == 1) return FLASH_SLOT_B;
    return FLASH_SLOT_OTHER;
}

const char *flash_map_slot_name(void) {
    switch (flash_map_boot_slot()) {
        case FLASH_SLOT_A: return "A";
        case FLASH_SLOT_B: return "B";
        case FLASH_SLOT_OTHER: return "other";
        default: return "raw";
    }
}

bool flash_map_update_pending(void) {
    return buy_pending;
}

bool flash_map_commit_update(void) {
    if (!buy_pending) return true;
    if (rom_explicit_buy(workarea, sizeof(workarea)) != BOOTROM_OK) return false;
    buy_pending = false;
    return true;
}

bool flash_map_update_target(uint32_t *offset, uint32_t *size, flash_slot_t *slot) {
    int a = -1, b = -1;
    for (uint8_t i = 0; i < part_count; i++) {
        if (!parts[i].has_id) continue;
        if (parts[i].id == FLASH_MAP_ID_APP_A) a = i;
        else if (parts[i].id == FLASH_MAP_ID_APP_B) b = i;
    }
    if (a < 0 || b < 0) return false;

    int picked = rom_pick_ab_partition_during_update((uint32_t *)workarea,
                                                     sizeof(workarea), (uint)a);
    if (picked < 0) return false;

    int target = picked == a ? b : a;
    *offset = parts[target].offset;
    *size = parts[target].size;
    *slot = target == a ? FLASH_SLOT_A : FLASH_SLOT_B;
    return true;
}

const void *flash_map_xip_ptr(uint32_t storage_offset) {
    return (const void *)(XIP_NOCACHE_NOALLOC_NOTRANSLATE_BASE + storage_offset);
}
