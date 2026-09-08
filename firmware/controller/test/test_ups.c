#include <stdio.h>
#include <string.h>

#include "lad_hw.h"
#include "lad_proto.h"
#include "microtest.h"
#include "ups.h"

// The LAD protocol against the manual's worked examples, the frame parser
// against a noisy line, and the poller against an emulated LAD-360DU that
// answers (or not) through the lad_hw seam.

// ---- emulated supply --------------------------------------------------------

static uint32_t now;            // the poller's clock, advanced by step()
static uint8_t rxq[512];        // bytes the supply has "sent", released at rxq_ready
static size_t rxq_n;
static uint32_t rxq_ready;
static uint32_t reply_delay_ms = 5;
static bool silent;             // supply unplugged / powered down
static bool corrupt_next;       // next reply carries a bad CRC
static int garbage_before;      // noise bytes ahead of the next reply
static int requests, bad_requests, writes;
static uint16_t last_addr, last_write_addr;
static uint8_t last_write[8], last_write_len;
static uint32_t last_request_ms, min_gap_ms;

static uint16_t reg_status_h, reg_status_l, reg_mains, reg_load, reg_batt, reg_cells[4], reg_uvp;

static void emu_reset(void) {
    rxq_n = 0;
    silent = false;
    corrupt_next = false;
    garbage_before = 0;
    requests = bad_requests = writes = 0;
    last_addr = last_write_addr = 0;
    last_request_ms = 0;
    min_gap_ms = 0xFFFFFFFFu;
    reg_status_h = 0;
    reg_status_l = LAD_ST_AC_OK | LAD_ST_CHARGING;
    reg_mains = 2302;                                   // 230.2 V
    reg_load = 657;                                     // 6.57 A
    reg_batt = 4790;                                    // 47.90 V
    reg_cells[0] = 1264; reg_cells[1] = 1266; reg_cells[2] = 1292; reg_cells[3] = 1296;
    reg_uvp = 3816;                                     // 38.16 V
}

static void put16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

void lad_hw_init(void) {}

void lad_hw_write(const uint8_t *src, size_t n) {
    requests++;
    if (last_request_ms && now - last_request_ms < min_gap_ms) min_gap_ms = now - last_request_ms;
    last_request_ms = now;

    lad_parser_t p;
    lad_parser_reset(&p);
    lad_frame_t f;
    bool ok = false;
    for (size_t i = 0; i < n; i++)
        if (lad_parser_feed(&p, src[i], &f)) ok = true;
    if (!ok) { bad_requests++; return; }

    if (f.rw == LAD_RW_WRITE) {
        writes++;
        last_write_addr = f.addr;
        last_write_len = f.len;
        memcpy(last_write, f.data, f.len);
        return; // the manual shows no reply to a write
    }
    last_addr = f.addr;
    if (silent) return;

    uint8_t data[8];
    size_t len = 2;
    switch (f.addr) {
    case LAD_ADDR_STATUS: put16(data, reg_status_h); put16(data + 2, reg_status_l); len = 4; break;
    case LAD_ADDR_MAINS_V: put16(data, reg_mains); break;
    case LAD_ADDR_LOAD_A: put16(data, reg_load); break;
    case LAD_ADDR_BATT_V: put16(data, reg_batt); break;
    case LAD_ADDR_CELLS: for (int i = 0; i < 4; i++) put16(data + 2 * i, reg_cells[i]); len = 8; break;
    case LAD_ADDR_UVP: put16(data, reg_uvp); break;
    default: return; // unknown address: no answer
    }
    uint8_t fr[LAD_FRAME_MAX];
    fr[0] = LAD_RW_READ;
    fr[1] = (uint8_t)(2 + len + 1);
    put16(fr + 2, f.addr);
    memcpy(fr + 4, data, len);
    fr[4 + len] = lad_crc8(fr, 4 + len);
    if (corrupt_next) { fr[4 + len] ^= 0x5A; corrupt_next = false; }

    while (garbage_before-- > 0 && rxq_n < sizeof(rxq)) rxq[rxq_n++] = (uint8_t)(0x55 ^ garbage_before);
    garbage_before = 0;
    memcpy(rxq + rxq_n, fr, 5 + len);
    rxq_n += 5 + len;
    rxq_ready = now + reply_delay_ms;
}

size_t lad_hw_read(uint8_t *dst, size_t cap) {
    if (!rxq_n || (int32_t)(now - rxq_ready) < 0) return 0;
    size_t n = rxq_n < cap ? rxq_n : cap;
    memcpy(dst, rxq, n);
    memmove(rxq, rxq + n, rxq_n - n);
    rxq_n -= n;
    return n;
}

static void step(uint32_t ms) {
    while (ms--) {
        now++;
        ups_poll(now);
    }
}

static void fresh(void) {
    emu_reset();
    now = 1000; // boot happened a second ago
    ups_init();
}

// ---- protocol ---------------------------------------------------------------

static void test_crc_examples(void) {
    // request/reply pairs printed in the manual (the two it gets wrong are
    // left out; see the LAD protocol note)
    static const struct { const char *hex; uint8_t crc; } EX[] = {
        {"55030010", 0x7F}, {"55030020", 0xEF}, {"55030030", 0x9F}, {"55030040", 0xC8},
        {"55030050", 0xB8}, {"55030060", 0x28}, {"AA04001001", 0x26}, {"AA04003001", 0x88},
        {"AA04003000", 0x8F}, {"5507001000011781", 0x4C}, {"5505002008FE", 0x97},
        {"550500300291", 0xBD}, {"5511005004F004F2050C0510", 0xAA}, {"550500600EE8", 0x0D},
    };
    for (size_t i = 0; i < sizeof(EX) / sizeof(EX[0]); i++) {
        uint8_t buf[16];
        size_t n = 0;
        for (const char *h = EX[i].hex; h[0]; h++) {
            if (*h == ' ') continue;
            unsigned v;
            sscanf(h, "%2x", &v);
            buf[n++] = (uint8_t)v;
            h++;
        }
        MT_ASSERT_EQ(lad_crc8(buf, n), EX[i].crc);
    }
}

static void test_build(void) {
    uint8_t buf[LAD_FRAME_MAX];
    MT_ASSERT_EQ(lad_build_read(buf, LAD_ADDR_STATUS), 5);
    MT_ASSERT(!memcmp(buf, "\x55\x03\x00\x10\x7F", 5));
    uint8_t off = 1;
    MT_ASSERT_EQ(lad_build_write(buf, LAD_WADDR_BUZZER, &off, 1), 6);
    MT_ASSERT(!memcmp(buf, "\xAA\x04\x00\x30\x01\x88", 6));
    uint8_t uvp[2] = {0x0E, 0xE8};
    MT_ASSERT_EQ(lad_build_write(buf, LAD_WADDR_UVP, uvp, 2), 7);
    MT_ASSERT(!memcmp(buf, "\xAA\x05\x00\x20\x0E\xE8\x24", 7)); // the manual prints 0x0D here
}

static bool feed_all(lad_parser_t *p, const uint8_t *b, size_t n, lad_frame_t *f) {
    bool got = false;
    for (size_t i = 0; i < n; i++)
        if (lad_parser_feed(p, b[i], f)) got = true;
    return got;
}

static void test_parser(void) {
    lad_parser_t p;
    lad_parser_reset(&p);
    lad_frame_t f;
    static const uint8_t status[] = {0x55, 0x07, 0x00, 0x10, 0x00, 0x01, 0x17, 0x81, 0x4C};

    // clean frame, byte at a time
    MT_ASSERT(feed_all(&p, status, sizeof(status), &f));
    MT_ASSERT_EQ(f.rw, 0x55);
    MT_ASSERT_EQ(f.addr, 0x0010);
    MT_ASSERT_EQ(f.len, 4);
    MT_ASSERT_EQ(lad_u16(f.data), 0x0001);
    MT_ASSERT_EQ(lad_u16(f.data + 2), 0x1781);

    // noise, including a stray 0x55 with a plausible length, ahead of it
    static const uint8_t noisy[] = {0x00, 0xFF, 0x55, 0x05, 0x12, 0x34,
                                    0x55, 0x07, 0x00, 0x10, 0x00, 0x01, 0x17, 0x81, 0x4C};
    lad_parser_reset(&p);
    MT_ASSERT(feed_all(&p, noisy, sizeof(noisy), &f));
    MT_ASSERT_EQ(f.addr, 0x0010);
    MT_ASSERT_EQ(lad_u16(f.data + 2), 0x1781);
    MT_ASSERT(p.bad >= 1);

    // bad CRC is dropped, the frame after it still parses
    uint8_t bad[sizeof(status)];
    memcpy(bad, status, sizeof(bad));
    bad[8] ^= 0x01;
    lad_parser_reset(&p);
    MT_ASSERT(!feed_all(&p, bad, sizeof(bad), &f));
    MT_ASSERT(feed_all(&p, status, sizeof(status), &f));
    MT_ASSERT_EQ(f.addr, 0x0010);

    // an impossible length byte cannot wedge it
    static const uint8_t huge[] = {0x55, 0x40, 0x55, 0x03, 0x00, 0x20, 0xEF};
    lad_parser_reset(&p);
    MT_ASSERT(feed_all(&p, huge, sizeof(huge), &f));
    MT_ASSERT_EQ(f.addr, 0x0020);
    MT_ASSERT_EQ(f.len, 0);
}

// ---- poller -----------------------------------------------------------------

static void test_absent(void) {
    fresh();
    silent = true;
    step(12000);
    MT_ASSERT(!ups_present());
    MT_ASSERT(!strcmp(ups_status_str(), "absent"));
    // one status probe every 5 s, nothing else
    MT_ASSERT(requests >= 2 && requests <= 3);
    MT_ASSERT_EQ(last_addr, LAD_ADDR_STATUS);
    MT_ASSERT_EQ(bad_requests, 0);
    MT_ASSERT_EQ(ups_state()->timeouts, (uint32_t)requests);
}

static void test_detect_and_read(void) {
    fresh();
    step(50);
    MT_ASSERT(ups_present()); // the first probe answered within 5 ms
    step(1500);               // a full round of every register
    const ups_state_t *s = ups_state();
    MT_ASSERT_EQ(s->status_l, LAD_ST_AC_OK | LAD_ST_CHARGING);
    MT_ASSERT_EQ(s->mains_dv, 2302);
    MT_ASSERT_EQ(s->load_ca, 657);
    MT_ASSERT_EQ(s->batt_cv, 4790);
    MT_ASSERT_EQ(s->cell_cv[0], 1264);
    MT_ASSERT_EQ(s->cell_cv[3], 1296);
    MT_ASSERT_EQ(s->uvp_cv, 3816);
    MT_ASSERT_EQ(s->timeouts, 0);
    MT_ASSERT_EQ(bad_requests, 0);
    MT_ASSERT(min_gap_ms >= 20); // the LAD's minimum request spacing
    MT_ASSERT(!ups_on_battery());
    MT_ASSERT(!ups_fault());
    MT_ASSERT(!strcmp(ups_status_str(),
                      "on mains 230.2 V, battery 47.90 V charging, load 6.57 A"));

    // cadence: the four fast registers once a second, the slow two every 10 s
    int before = requests;
    step(10000);
    int per_10s = requests - before;
    MT_ASSERT(per_10s >= 40 && per_10s <= 44);
}

static void test_status_transitions(void) {
    fresh();
    step(1500);
    reg_status_l = LAD_ST_ON_BATTERY;                   // mains gone
    step(1100);
    MT_ASSERT(ups_on_battery());
    MT_ASSERT(!strncmp(ups_status_str(), "ON BATTERY", 10));
    reg_status_l = LAD_ST_AC_OK | LAD_ST_BAT_NC | LAD_ST_BAT_ERR2;
    step(1100);
    MT_ASSERT(ups_fault());
    char text[96];
    MT_ASSERT_EQ(ups_fault_text(text, sizeof(text)), 2);
    MT_ASSERT(!strcmp(text, "battery missing, block 2"));
    MT_ASSERT(strstr(ups_status_str(), "FAULT") != NULL);
}

static void test_lost_and_back(void) {
    fresh();
    step(1500);
    MT_ASSERT(ups_present());
    silent = true;
    step(6000);
    MT_ASSERT(!ups_present());
    MT_ASSERT(ups_state()->timeouts >= 5);
    // back to the slow probe while it is gone
    int before = requests;
    step(10000);
    MT_ASSERT(requests - before <= 3);
    silent = false;
    step(5100);
    MT_ASSERT(ups_present());
}

static void test_noise_and_corruption(void) {
    fresh();
    step(1500);
    uint32_t replies = ups_state()->replies;
    corrupt_next = true;   // one bad reply: that register is retried next round
    garbage_before = 3;    // and line noise ahead of the one after
    step(2100);
    MT_ASSERT(ups_present());
    MT_ASSERT(ups_state()->replies > replies + 4);
    MT_ASSERT(ups_state()->bad_frames >= 1);
    MT_ASSERT(ups_state()->timeouts <= 1);
}

static void test_slow_reply(void) {
    fresh();
    reply_delay_ms = 60; // well past the spec's 5 ms, inside our timeout
    step(2000);
    MT_ASSERT(ups_present());
    MT_ASSERT_EQ(ups_state()->timeouts, 0);
    reply_delay_ms = 5;
}

static void test_buzzer_write(void) {
    fresh();
    step(1500);
    MT_ASSERT(ups_set_buzzer(false));
    MT_ASSERT(!ups_set_buzzer(true)); // one at a time
    step(100);
    MT_ASSERT_EQ(writes, 1);
    MT_ASSERT_EQ(last_write_addr, LAD_WADDR_BUZZER);
    MT_ASSERT_EQ(last_write_len, 1);
    MT_ASSERT_EQ(last_write[0], 1);
    MT_ASSERT(ups_set_buzzer(true));
    step(100);
    MT_ASSERT_EQ(writes, 2);
    MT_ASSERT_EQ(last_write[0], 0);
    MT_ASSERT(ups_present()); // polling carried on around the writes
    MT_ASSERT(min_gap_ms >= 20);
}

void run_ups_tests(void) {
    printf("ups:\n");
    mt_run("crc examples", test_crc_examples);
    mt_run("build", test_build);
    mt_run("parser", test_parser);
    mt_run("absent", test_absent);
    mt_run("detect and read", test_detect_and_read);
    mt_run("status transitions", test_status_transitions);
    mt_run("lost and back", test_lost_and_back);
    mt_run("noise and corruption", test_noise_and_corruption);
    mt_run("slow reply", test_slow_reply);
    mt_run("buzzer write", test_buzzer_write);
}
