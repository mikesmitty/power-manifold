#pragma once

#include <stdbool.h>
#include <stdint.h>

// Flash seam under the settings store (settings.c): settings_hw_pico.c is
// the RP2350's flash behind flash_map (the data partition, or the legacy
// top-of-flash sectors of an unpartitioned board); the host tests keep the
// sectors in RAM.

#define SETTINGS_SECTOR_SIZE 4096u

// Storage offset of the ping-pong pair, and of the legacy pre-partition
// location (the same offset on a board without a partition table).
uint32_t settings_hw_home(void);
uint32_t settings_hw_legacy(void);

// Memory-mapped view of the sector at a storage offset (reads only).
const void *settings_hw_sector(uint32_t off);

// Erase the sector at `off`, then program `data` (one full sector) into it;
// NULL erases only. Core 0 only on the Pico, with the engine running: it is
// parked while XIP is unavailable.
bool settings_hw_write(uint32_t off, const uint8_t *data);

// This image booted as an uncommitted trial (flash_map_update_pending).
bool settings_hw_trial(void);

// Milliseconds since boot, for the save debounce.
uint32_t settings_hw_now_ms(void);
