#include "ups.h"

#include <stdio.h>
#include <string.h>

#include "lad_hw.h"
#include "lad_proto.h"

#define REQUEST_GAP_MS   25    // the LAD wants >= 20 ms between requests
#define REPLY_TIMEOUT_MS 100   // spec: 5 ms; generous for a 2 ms main-loop tick
#define FAST_PERIOD_MS   1000  // status, mains, load, battery
#define SLOW_PERIOD_MS   10000 // cells, UVP set-point
#define PROBE_PERIOD_MS  5000  // while nothing answers: one status read
#define LOST_AFTER_MS    5000  // no valid reply this long: the supply is gone
#define LOST_AFTER_FAILS 5     // ...or this many timeouts in a row

typedef struct {
    uint16_t addr;
    uint8_t  len;       // expected reply data bytes
    uint32_t period_ms;
    uint32_t due_ms;
} slot_t;

static slot_t slots[] = {
    {LAD_ADDR_STATUS,  4, FAST_PERIOD_MS, 0},
    {LAD_ADDR_MAINS_V, 2, FAST_PERIOD_MS, 0},
    {LAD_ADDR_LOAD_A,  2, FAST_PERIOD_MS, 0},
    {LAD_ADDR_BATT_V,  2, FAST_PERIOD_MS, 0},
    {LAD_ADDR_CELLS,   8, SLOW_PERIOD_MS, 0},
    {LAD_ADDR_UVP,     2, SLOW_PERIOD_MS, 0},
};
#define N_SLOTS (sizeof(slots) / sizeof(slots[0]))

static ups_state_t st;
static lad_parser_t parser;
static bool waiting;          // a read request is out
static int  waiting_slot;
static uint32_t sent_ms;      // when the last request (read or write) left
static uint32_t next_probe_ms;
static unsigned fails;        // consecutive timeouts
static bool write_pending;
static uint8_t write_frame[LAD_FRAME_MAX];
static size_t write_len;
static uint16_t seen_status_l; // for transition logging
static char status_buf[96];

void ups_init(void) {
    memset(&st, 0, sizeof(st));
    for (int i = 0; i < 4; i++) st.cell_cv[i] = 0xFFFF;
    lad_parser_reset(&parser);
    parser.bad = 0;
    for (size_t i = 0; i < N_SLOTS; i++) slots[i].due_ms = 0;
    waiting = false;
    sent_ms = 0;
    fails = 0;
    write_pending = false;
    next_probe_ms = 0;
    seen_status_l = 0;
    lad_hw_init();
}

static void send_read(int slot, uint32_t now_ms) {
    uint8_t buf[LAD_FRAME_MAX];
    size_t n = lad_build_read(buf, slots[slot].addr);
    lad_parser_reset(&parser);
    lad_hw_write(buf, n);
    waiting = true;
    waiting_slot = slot;
    sent_ms = now_ms;
}

static void log_status_change(uint16_t was, uint16_t now) {
    uint16_t diff = was ^ now;
    if (diff & LAD_ST_ON_BATTERY)
        printf(now & LAD_ST_ON_BATTERY ? "ups: AC input lost, load on battery\n"
                                       : "ups: back on mains\n");
    else if (diff & LAD_ST_AC_OK)
        printf(now & LAD_ST_AC_OK ? "ups: AC input normal\n" : "ups: AC input abnormal\n");
    if (diff & LAD_ST_CHG_FULL && (now & LAD_ST_CHG_FULL)) printf("ups: battery full\n");
    if ((diff & LAD_ST_FAULT_MASK) && (now & LAD_ST_FAULT_MASK)) {
        char text[96];
        ups_fault_text(text, sizeof(text));
        printf("ups: battery fault: %s\n", text);
    } else if ((diff & LAD_ST_FAULT_MASK) && !(now & LAD_ST_FAULT_MASK)) {
        printf("ups: battery fault cleared\n");
    }
}

static void accept(const lad_frame_t *f, uint32_t now_ms) {
    switch (f->addr) {
    case LAD_ADDR_STATUS:
        st.status_h = lad_u16(f->data);
        st.status_l = lad_u16(f->data + 2);
        if (st.present && st.status_l != seen_status_l) log_status_change(seen_status_l, st.status_l);
        seen_status_l = st.status_l;
        break;
    case LAD_ADDR_MAINS_V: st.mains_dv = lad_u16(f->data); break;
    case LAD_ADDR_LOAD_A:  st.load_ca = lad_u16(f->data); break;
    case LAD_ADDR_BATT_V:  st.batt_cv = lad_u16(f->data); break;
    case LAD_ADDR_CELLS:
        for (int i = 0; i < 4; i++) st.cell_cv[i] = lad_u16(f->data + 2 * i);
        break;
    case LAD_ADDR_UVP:     st.uvp_cv = lad_u16(f->data); break;
    default: return;
    }
    st.replies++;
    st.last_ok_ms = now_ms;
    fails = 0;
    if (!st.present) {
        st.present = true;
        seen_status_l = st.status_l;
        // a fresh start: everything is due now, in table order
        for (size_t i = 0; i < N_SLOTS; i++) slots[i].due_ms = now_ms;
        printf("ups: Mean Well LAD answering on the UPS header\n");
    }
}

static void lost(const char *why) {
    if (!st.present) return;
    st.present = false;
    printf("ups: %s; retrying every %u s\n", why, PROBE_PERIOD_MS / 1000);
}

void ups_poll(uint32_t now_ms) {
    // drain whatever arrived; only the answer we are waiting for is used
    uint8_t rx[32];
    size_t n;
    while ((n = lad_hw_read(rx, sizeof(rx))) != 0) {
        for (size_t i = 0; i < n; i++) {
            lad_frame_t f;
            if (!lad_parser_feed(&parser, rx[i], &f)) continue;
            if (waiting && f.rw == LAD_RW_READ && f.addr == slots[waiting_slot].addr &&
                f.len == slots[waiting_slot].len) {
                waiting = false;
                slots[waiting_slot].due_ms = now_ms + slots[waiting_slot].period_ms;
                accept(&f, now_ms);
            }
        }
    }
    st.bad_frames = parser.bad;

    if (waiting) {
        if (now_ms - sent_ms < REPLY_TIMEOUT_MS) return;
        waiting = false;
        st.timeouts++;
        if (st.present && ++fails >= LOST_AFTER_FAILS) lost("no reply");
        // a missed slot is tried again next round rather than immediately
        slots[waiting_slot].due_ms = now_ms + (st.present ? FAST_PERIOD_MS : PROBE_PERIOD_MS);
    }
    if (st.present && now_ms - st.last_ok_ms > LOST_AFTER_MS) lost("silent");
    if (now_ms - sent_ms < REQUEST_GAP_MS) return;

    if (write_pending) {
        lad_hw_write(write_frame, write_len);
        write_pending = false;
        sent_ms = now_ms;
        return;
    }

    if (!st.present) {
        // one status read per probe period: enough to notice a supply
        // appearing without filling the log while none is plugged in
        if ((int32_t)(now_ms - next_probe_ms) < 0) return;
        next_probe_ms = now_ms + PROBE_PERIOD_MS;
        send_read(0, now_ms);
        return;
    }
    for (size_t i = 0; i < N_SLOTS; i++) {
        if ((int32_t)(now_ms - slots[i].due_ms) < 0) continue;
        send_read((int)i, now_ms);
        return;
    }
}

bool ups_present(void) {
    return st.present;
}

bool ups_on_battery(void) {
    return st.present && (st.status_l & LAD_ST_ON_BATTERY);
}

bool ups_fault(void) {
    return st.present && (st.status_l & LAD_ST_FAULT_MASK);
}

const ups_state_t *ups_state(void) {
    return &st;
}

unsigned ups_fault_text(char *buf, unsigned cap) {
    static const struct { uint16_t bit; const char *text; } NAMES[] = {
        {LAD_ST_BAT_NC, "battery missing"},        {LAD_ST_BAT_REV, "battery reversed"},
        {LAD_ST_BAT_UVP, "undervoltage"},          {LAD_ST_BAT_OVP, "overvoltage"},
        {LAD_ST_UNBALANCED, "string unbalanced"},  {LAD_ST_DISCH_OLP, "discharge overload"},
        {LAD_ST_BAT_ERR1, "block 1"},              {LAD_ST_BAT_ERR2, "block 2"},
        {LAD_ST_BAT_ERR3, "block 3"},              {LAD_ST_BAT_ERR4, "block 4"},
    };
    unsigned n = 0, count = 0;
    if (!cap) return 0;
    buf[0] = '\0';
    for (size_t i = 0; i < sizeof(NAMES) / sizeof(NAMES[0]); i++) {
        if (!(st.status_l & NAMES[i].bit)) continue;
        int w = snprintf(buf + n, cap - n, "%s%s", count ? ", " : "", NAMES[i].text);
        if (w < 0 || (unsigned)w >= cap - n) break;
        n += (unsigned)w;
        count++;
    }
    return count;
}

const char *ups_status_str(void) {
    if (!st.present) return "absent";
    const char *batt = (st.status_l & LAD_ST_FAULT_MASK) ? "FAULT"
                     : (st.status_l & LAD_ST_CHG_FULL) ? "full"
                     : (st.status_l & LAD_ST_CHARGING) ? "charging" : "idle";
    snprintf(status_buf, sizeof(status_buf), "%s %u.%u V, battery %u.%02u V %s, load %u.%02u A",
             (st.status_l & LAD_ST_ON_BATTERY) ? "ON BATTERY, mains" : "on mains",
             st.mains_dv / 10, st.mains_dv % 10, st.batt_cv / 100, st.batt_cv % 100, batt,
             st.load_ca / 100, st.load_ca % 100);
    return status_buf;
}

bool ups_set_buzzer(bool on) {
    if (write_pending) return false;
    uint8_t v = on ? 0 : 1; // the register is "buzzer off"
    write_len = lad_build_write(write_frame, LAD_WADDR_BUZZER, &v, 1);
    write_pending = true;
    return true;
}
