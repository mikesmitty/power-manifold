#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "manifold.h"

// Persistent fault history in the data partition, right after the settings
// ping-pong pair: a 64KB ring of 256-byte flash pages, one 32-byte record
// each, erased a sector at a time as the ring wraps (~256 records, oldest
// sector dropped first). Unavailable on boards without a partition table.
//
// Appends run on core 0 with the engine up (flash_safe_execute); reads go
// through the untranslated XIP alias and are safe any time after init.

typedef struct {
    uint32_t seq;         // monotonic; 0xFFFFFFFF marks an empty page
    uint32_t epoch;       // unix time at the event, 0 before SNTP sync
    uint32_t uptime_s;
    uint8_t  port;        // 0-based; 0xFF = chassis-level
    uint8_t  type;        // evt_type_t (EVT_FAULT / EVT_PROBE_FAIL)
    uint16_t code;
    uint32_t arg;
    uint32_t power_mw;    // port telemetry snapshotted at the event
    uint32_t contract_mw;
    uint8_t  reserved[4]; // 0xFF
} fault_rec_t;

void fault_log_init(void); // core 0, after flash_map_init(); scan only
bool fault_log_available(void);

// Filters for fault-class events and appends (rate-limited). Anything else
// is ignored, so the main loop can hand it every engine event.
void fault_log_event(const engine_evt_t *e);

int  fault_log_count(void);
bool fault_log_get(int n, fault_rec_t *out); // n = 0 is the newest record
bool fault_log_clear(void);
