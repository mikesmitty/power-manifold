#include "fault_log.h"

#include <assert.h>
#include <string.h>

#include "hardware/flash.h"
#include "hardware/watchdog.h"
#include "pico/flash.h"
#include "pico/time.h"

#include "flash_map.h"
#include "ipc.h"
#include "net/net.h"

// Region layout inside the data partition: the settings ping-pong pair owns
// the first two sectors (see settings.c), the fault ring the next sixteen.
#define FL_OFFSET      (2 * FLASH_SECTOR_SIZE)
#define FL_SECTORS     16
#define FL_PAGE        256u
#define FL_PPS         (FLASH_SECTOR_SIZE / FL_PAGE) // pages per sector
#define FL_PAGES       (FL_SECTORS * FL_PPS)
#define FL_EMPTY       0xFFFFFFFFu

// consecutive identical-ish faults add nothing; bound the append rate
#define FL_MIN_GAP_MS  250

static_assert(sizeof(fault_rec_t) == 32, "fault record must stay 32 bytes");

static bool     available;
static uint32_t region_off;         // storage offset of the ring
static uint32_t page_seq[FL_PAGES]; // RAM mirror of every page's seq word
static uint32_t seq_max;
static int      next_page;
static int      valid_count;
static uint32_t last_append_ms;
static fault_rec_t last[NUM_PORTS]; // newest fault/probe per port
static bool        have_last[NUM_PORTS];

static const fault_rec_t *page_ptr(int page) {
    return (const fault_rec_t *)flash_map_xip_ptr(region_off +
                                                  (uint32_t)page * FL_PAGE);
}

typedef struct {
    uint32_t offset;
    const uint8_t *data; // NULL: erase the sector at offset instead
} flop_t;

static void do_flop(void *param) {
    const flop_t *op = (const flop_t *)param;
    if (op->data) flash_range_program(op->offset, op->data, FL_PAGE);
    else flash_range_erase(op->offset, FLASH_SECTOR_SIZE);
}

static bool run_flop(uint32_t offset, const uint8_t *data) {
    flop_t op = {.offset = offset, .data = data};
    return flash_safe_execute(do_flop, &op, 500) == PICO_OK;
}

void fault_log_init(void) {
    uint32_t off, size;
    if (!flash_map_find(FLASH_MAP_ID_DATA, &off, &size) ||
        size < FL_OFFSET + FL_SECTORS * FLASH_SECTOR_SIZE)
        return;
    region_off = off + FL_OFFSET;
    available = true;

    int max_page = -1;
    for (int p = 0; p < FL_PAGES; p++) {
        page_seq[p] = page_ptr(p)->seq;
        if (page_seq[p] == FL_EMPTY) continue;
        valid_count++;
        if (page_seq[p] > seq_max) {
            seq_max = page_seq[p];
            max_page = p;
        }
    }
    next_page = max_page < 0 ? 0 : (max_page + 1) % FL_PAGES;

    // seed the per-port "last fault" from the newest records
    for (int n = 0; n < valid_count; n++) {
        fault_rec_t r;
        if (!fault_log_get(n, &r)) break;
        if (r.port >= NUM_PORTS || have_last[r.port]) continue;
        if (r.type != EVT_FAULT && r.type != EVT_PROBE_FAIL) continue;
        last[r.port] = r;
        have_last[r.port] = true;
    }
}

bool fault_log_available(void) {
    return available;
}

static bool append(const fault_rec_t *rec) {
    // a non-blank target mid-sector (torn wrap) skips ahead to the next
    // sector boundary; entering a sector with old records erases it
    if (next_page % FL_PPS != 0 && page_seq[next_page] != FL_EMPTY)
        next_page = (next_page / FL_PPS + 1) * FL_PPS % FL_PAGES;
    if (next_page % FL_PPS == 0) {
        bool dirty = false;
        for (int p = next_page; p < next_page + (int)FL_PPS; p++)
            dirty |= page_seq[p] != FL_EMPTY;
        if (dirty) {
            if (!run_flop(region_off + (uint32_t)next_page * FL_PAGE, NULL))
                return false;
            for (int p = next_page; p < next_page + (int)FL_PPS; p++) {
                if (page_seq[p] != FL_EMPTY) valid_count--;
                page_seq[p] = FL_EMPTY;
            }
        }
    }

    static uint8_t page[FL_PAGE]; // static: off the tick-loop stack
    memset(page, 0xFF, sizeof(page));
    memcpy(page, rec, sizeof(*rec));
    if (!run_flop(region_off + (uint32_t)next_page * FL_PAGE, page))
        return false;
    page_seq[next_page] = rec->seq;
    valid_count++;
    next_page = (next_page + 1) % FL_PAGES;
    return true;
}

static void record_init(fault_rec_t *rec, uint8_t port, uint8_t type, uint16_t code,
                        uint32_t arg) {
    memset(rec, 0xFF, sizeof(*rec));
    rec->seq = 0; // assigned when committed to flash
    rec->epoch = net_epoch();
    rec->uptime_s = to_ms_since_boot(get_absolute_time()) / 1000;
    rec->port = port;
    rec->type = type;
    rec->code = code;
    rec->arg = arg;
    rec->power_mw = 0;
    rec->contract_mw = 0;
}

static bool commit(fault_rec_t *rec) {
    if (!available) return false;
    rec->seq = ++seq_max;
    return append(rec);
}

void fault_log_event(const engine_evt_t *e) {
    if (e->type != EVT_FAULT && e->type != EVT_PROBE_FAIL) return;

    fault_rec_t rec;
    record_init(&rec, e->port, e->type, e->code, e->arg);
    if (e->port < NUM_PORTS) {
        telemetry_t t;
        ipc_snapshot_read(&t);
        rec.power_mw = t.port[e->port].power_mw;
        rec.contract_mw = t.port[e->port].contract_mw;
        last[e->port] = rec; // every event, even the ones the ring rate-limits away
        have_last[e->port] = true;
    }
    if (!available) return;

    uint32_t now = to_ms_since_boot(get_absolute_time());
    if (last_append_ms && now - last_append_ms < FL_MIN_GAP_MS) return;
    last_append_ms = now ? now : 1;
    commit(&rec);
}

bool fault_log_boot(const boot_cause_t *b) {
    fault_rec_t rec;
    record_init(&rec, 0xFF, EVT_BOOT, (uint16_t)(b->reason | ((uint16_t)b->core << 8)), b->pc);
    rec.power_mw = b->lr;
    rec.contract_mw = b->cfsr;
    return commit(&rec);
}

bool fault_log_last(unsigned port, fault_rec_t *out) {
    if (port >= NUM_PORTS || !have_last[port]) return false;
    *out = last[port];
    return true;
}

int fault_log_count(void) {
    return valid_count;
}

bool fault_log_get(int n, fault_rec_t *out) {
    if (!available || n < 0 || n >= valid_count) return false;
    // seq values form one contiguous window (append is monotonic, the wrap
    // erases oldest-first), so the nth-newest is findable by value. The one
    // exception is a wrap interrupted by power loss, which can leave a gap:
    // a missing value then just ends a listing early until the ring heals.
    uint32_t want = seq_max - (uint32_t)n;
    for (int p = 0; p < FL_PAGES; p++) {
        if (page_seq[p] == want) {
            memcpy(out, page_ptr(p), sizeof(*out));
            return true;
        }
    }
    return false;
}

bool fault_log_clear(void) {
    if (!available) return false;
    for (int s = 0; s < FL_SECTORS; s++) {
        watchdog_update();
        if (!run_flop(region_off + (uint32_t)s * FLASH_SECTOR_SIZE, NULL))
            return false;
    }
    memset(page_seq, 0xFF, sizeof(page_seq));
    valid_count = 0;
    next_page = 0;
    seq_max = 0;
    return true;
}
