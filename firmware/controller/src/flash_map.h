#pragma once

#include <stdbool.h>
#include <stdint.h>

// Runtime view of the RP2350 partition table. The firmware never bakes in
// flash offsets: partitions/*.json defines the layout per board, and this
// module locates partitions by their 64-bit ID via the bootrom, so the same
// binary runs on the 4MB Pico 2 W and the 16MB production board.
//
// All offsets returned here are STORAGE addresses (byte offsets from the
// start of flash). hardware/flash erase/program take these directly, but
// memory-mapped reads must go through flash_map_xip_ptr(): when the bootrom
// boots an image from partition B it points the normal XIP window at B via
// QMI address translation, so a raw XIP_BASE + offset read would land in the
// wrong place.

// Partition IDs, matching partitions/*.json ("PWRM" tag + role).
// By convention image slot A is partition 0 and B is partition 1.
#define FLASH_MAP_ID_APP_A  0x5057524d00000001ull
#define FLASH_MAP_ID_APP_B  0x5057524d00000002ull
#define FLASH_MAP_ID_DATA   0x5057524d00000010ull
#define FLASH_MAP_ID_ASSETS 0x5057524d00000020ull

typedef enum {
    FLASH_SLOT_RAW = 0, // no partition table: image sits at the start of flash
    FLASH_SLOT_A,
    FLASH_SLOT_B,
    FLASH_SLOT_OTHER,   // booted from a partition that is not slot A/B
} flash_slot_t;

// Core 0, once, before settings_load() and before core 1 launches.
void flash_map_init(void);

bool flash_map_has_partition_table(void);

// Storage offset/size of the partition carrying `id`; false if absent.
bool flash_map_find(uint64_t id, uint32_t *offset, uint32_t *size);

flash_slot_t flash_map_boot_slot(void);
const char  *flash_map_slot_name(void); // "A", "B", "raw", "other"

// True when this image booted try-before-you-buy and has not been committed:
// a plain reboot (or watchdog reset) reverts to the previous image.
bool flash_map_update_pending(void);

// Commit a pending trial image (bootrom explicit buy). Pauses core 1 briefly
// via flash_safe_execute, so the engine must be running. True on success or
// when nothing was pending.
bool flash_map_commit_update(void);

// A/B slot an incoming OTA image should be written into: the pair partner of
// the slot the bootrom would pick right now (per
// rom_pick_ab_partition_during_update, which also verifies the picked image
// and leaves any in-flight update state intact). False when there is no A/B
// pair or the bootrom refuses.
bool flash_map_update_target(uint32_t *offset, uint32_t *size, flash_slot_t *slot);

// Translation-proof pointer for memory-mapped reads of a storage offset
// (uncached; fine for one-shot reads like settings load).
const void *flash_map_xip_ptr(uint32_t storage_offset);
