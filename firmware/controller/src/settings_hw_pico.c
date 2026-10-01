#include "settings_hw.h"

#include "hardware/flash.h"
#include "pico/flash.h"
#include "pico/time.h"

#include "flash_map.h"

_Static_assert(SETTINGS_SECTOR_SIZE == FLASH_SECTOR_SIZE, "settings sector size");

#define SETTINGS_SLOTS 2

// The pair lives in the first two sectors of the "data" partition; boards
// without a partition table fall back to the legacy pre-partition location,
// the last two sectors of flash (which, on a freshly partitioned board, is
// where settings written by older firmware are found and migrated from).
uint32_t settings_hw_legacy(void) {
    return PICO_FLASH_SIZE_BYTES - SETTINGS_SLOTS * FLASH_SECTOR_SIZE;
}

uint32_t settings_hw_home(void) {
    uint32_t off, size;
    if (flash_map_find(FLASH_MAP_ID_DATA, &off, &size) &&
        size >= SETTINGS_SLOTS * FLASH_SECTOR_SIZE)
        return off;
    return settings_hw_legacy();
}

// Reads go through the untranslated XIP alias: storage offsets stay valid no
// matter which A/B slot the bootrom mapped at XIP_BASE.
const void *settings_hw_sector(uint32_t off) {
    return flash_map_xip_ptr(off);
}

typedef struct {
    uint32_t offset;
    const uint8_t *data; // NULL: erase only
} flash_op_t;

static void do_flash_write(void *param) {
    const flash_op_t *op = (const flash_op_t *)param;
    flash_range_erase(op->offset, FLASH_SECTOR_SIZE);
    if (op->data) flash_range_program(op->offset, op->data, FLASH_SECTOR_SIZE);
}

bool settings_hw_write(uint32_t off, const uint8_t *data) {
    flash_op_t op = {.offset = off, .data = data};
    // flash_safe_execute parks core 1 (the engine calls
    // flash_safe_execute_core_init at startup) while XIP is unavailable
    return flash_safe_execute(do_flash_write, &op, 500) == PICO_OK;
}

bool settings_hw_trial(void) {
    return flash_map_update_pending();
}

uint32_t settings_hw_now_ms(void) {
    return to_ms_since_boot(get_absolute_time());
}
