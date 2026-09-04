#include <stdio.h>
#include <string.h>

#include "microtest.h"
#include "w6100.h"
#include "w6100_hw.h"

// The W6100 driver against an emulated chip: the SPI frame decoder, the
// register side effects the driver relies on (locks, reset, socket
// commands, interrupt clears) and the socket 0 ring buffers with their
// 16-bit wrapping pointers. Only what the MACRAW path touches is modelled.

#define BUFSZ 16384u
#define CREG_SIZE 0x4200
#define SREG_SIZE 0x240

static uint8_t creg[CREG_SIZE];
static uint8_t sreg[8][SREG_SIZE];
static uint8_t txbuf[BUFSZ], rxbuf[BUFSZ];
static uint16_t tx_rd, tx_wr, rx_rd, rx_wr; // chip-side socket 0 pointers
static uint8_t s0_ir;

static bool selected;
static uint8_t hdr[3];
static int hdr_n;
static uint16_t cur_addr;
static uint8_t cur_block;
static bool cur_write;
static int bad_direction; // data phase direction disagrees with the control byte

static uint32_t fake_ms;
static int hw_resets, sw_resets;
static bool reset_line;
static uint16_t cidr = 0x6100;
static bool send_times_out;
static uint8_t sent[2048];
static uint16_t sent_len;
static int sends;

static void sync_ptr_regs(void) {
    sreg[0][0x208] = (uint8_t)(tx_rd >> 8); sreg[0][0x209] = (uint8_t)tx_rd;
    sreg[0][0x20C] = (uint8_t)(tx_wr >> 8); sreg[0][0x20D] = (uint8_t)tx_wr;
    sreg[0][0x228] = (uint8_t)(rx_rd >> 8); sreg[0][0x229] = (uint8_t)rx_rd;
    sreg[0][0x22C] = (uint8_t)(rx_wr >> 8); sreg[0][0x22D] = (uint8_t)rx_wr;
}

static void chip_reset(void) {
    memset(creg, 0, sizeof(creg));
    memset(sreg, 0, sizeof(sreg));
    creg[0x0000] = (uint8_t)(cidr >> 8);
    creg[0x0001] = (uint8_t)cidr;
    creg[0x0002] = 0x46;
    creg[0x0003] = 0x61;
    creg[0x2000] = 0xE0; // SYSR: chip, net and PHY blocks locked
    for (int s = 0; s < 8; s++) {
        sreg[s][0x200] = 2; // 2 KB per socket, each way, as shipped
        sreg[s][0x220] = 2;
    }
    tx_rd = tx_wr = rx_rd = rx_wr = 0;
    s0_ir = 0;
    sync_ptr_regs();
}

static bool unlocked(uint8_t bit) {
    return (creg[0x2000] & bit) == 0;
}

static void common_write(uint16_t a, uint8_t v) {
    switch (a) {
    case 0x41F4: if (v == 0xCE) creg[0x2000] &= 0x7F; else creg[0x2000] |= 0x80; return;
    case 0x41F5: if (v == 0x3A) creg[0x2000] &= 0xBF; else creg[0x2000] |= 0x40; return;
    case 0x41F6: if (v == 0x53) creg[0x2000] &= 0xDF; else creg[0x2000] |= 0x20; return;
    case 0x2004:
        if (unlocked(0x80) && v == 0x00) { sw_resets++; chip_reset(); }
        return;
    default: break;
    }
    if (a >= 0x4120 && a < 0x4126 && !unlocked(0x40)) return; // SHAR needs the net unlock
    if (a >= 0x3000 && a < 0x3100 && !unlocked(0x20)) return; // PHY block
    creg[a] = v;
}

static void s0_send(void) {
    uint16_t len = (uint16_t)(tx_wr - tx_rd);
    sent_len = len;
    for (uint16_t i = 0; i < len; i++) sent[i] = txbuf[(uint16_t)(tx_rd + i) % BUFSZ];
    tx_rd = tx_wr;
    sync_ptr_regs();
    sends++;
    s0_ir |= send_times_out ? 0x08 : 0x10;
}

static void socket_write(int s, uint16_t a, uint8_t v) {
    if (s != 0) { sreg[s][a] = v; return; }
    switch (a) {
    case 0x0010: // Sn_CR
        if (v == 0x01) sreg[0][0x30] = ((sreg[0][0] & 0x0F) == 0x07) ? 0x42 : 0x13;
        else if (v == 0x10) sreg[0][0x30] = 0x00;
        else if (v == 0x20 && sreg[0][0x30] == 0x42) s0_send();
        sreg[0][0x10] = 0; // auto-clear
        return;
    case 0x0028: s0_ir &= (uint8_t)~v; return; // Sn_IRCLR
    case 0x0020: return;                        // Sn_IR is read-only on the W6100
    default: break;
    }
    sreg[0][a] = v;
    if (a == 0x20C || a == 0x20D) tx_wr = (uint16_t)((sreg[0][0x20C] << 8) | sreg[0][0x20D]);
    if (a == 0x228 || a == 0x229) rx_rd = (uint16_t)((sreg[0][0x228] << 8) | sreg[0][0x229]);
}

static uint8_t socket_read(int s, uint16_t a) {
    if (s != 0) return sreg[s][a];
    uint16_t used_tx = (uint16_t)(tx_wr - tx_rd);
    uint16_t fsr = (uint16_t)(BUFSZ - used_tx);
    uint16_t rsr = (uint16_t)(rx_wr - rx_rd);
    switch (a) {
    case 0x0010: return 0;
    case 0x0020: return s0_ir;
    case 0x0204: return (uint8_t)(fsr >> 8);
    case 0x0205: return (uint8_t)fsr;
    case 0x0224: return (uint8_t)(rsr >> 8);
    case 0x0225: return (uint8_t)rsr;
    default: return sreg[0][a];
    }
}

static void chip_write(uint8_t block, uint16_t a, uint8_t v) {
    if (block == 0) { common_write(a, v); return; }
    int s = (block - 1) / 4, kind = (block - 1) % 4;
    if (kind == 0) socket_write(s, a, v);
    else if (kind == 1 && s == 0) txbuf[a % BUFSZ] = v;
}

static uint8_t chip_read(uint8_t block, uint16_t a) {
    if (block == 0) return creg[a];
    int s = (block - 1) / 4, kind = (block - 1) % 4;
    if (kind == 0) return socket_read(s, a);
    if (kind == 2 && s == 0) return rxbuf[a % BUFSZ];
    return 0;
}

// ---- w6100_hw seam ----------------------------------------------------------

void w6100_hw_init(void) {}

void w6100_hw_reset(bool asserted) {
    if (reset_line && !asserted) { hw_resets++; chip_reset(); }
    reset_line = asserted;
}

void w6100_hw_select(bool s) {
    selected = s;
    hdr_n = 0;
}

void w6100_hw_write(const uint8_t *src, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (hdr_n < 3) {
            hdr[hdr_n++] = src[i];
            if (hdr_n == 3) {
                cur_addr = (uint16_t)((hdr[0] << 8) | hdr[1]);
                cur_block = hdr[2] >> 3;
                cur_write = (hdr[2] & 0x04) != 0;
            }
            continue;
        }
        if (!cur_write) bad_direction++;
        chip_write(cur_block, cur_addr++, src[i]);
    }
}

void w6100_hw_read(uint8_t *dst, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (hdr_n < 3 || cur_write) bad_direction++;
        dst[i] = chip_read(cur_block, cur_addr++);
    }
}

void w6100_hw_delay_ms(uint32_t ms) {
    fake_ms += ms;
}

uint32_t w6100_hw_ms(void) {
    return fake_ms++; // time moves whenever someone looks
}

// ---- fixture ---------------------------------------------------------------

static const uint8_t MAC[6] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};

static void inject_rx(const uint8_t *f, uint16_t len) {
    uint16_t total = (uint16_t)(len + 2);
    rxbuf[rx_wr % BUFSZ] = (uint8_t)(total >> 8);
    rxbuf[(uint16_t)(rx_wr + 1) % BUFSZ] = (uint8_t)total;
    for (uint16_t i = 0; i < len; i++) rxbuf[(uint16_t)(rx_wr + 2 + i) % BUFSZ] = f[i];
    rx_wr = (uint16_t)(rx_wr + total);
    sync_ptr_regs();
    s0_ir |= 0x04;
}

static void pattern(uint8_t *b, uint16_t n, uint8_t seed) {
    for (uint16_t i = 0; i < n; i++) b[i] = (uint8_t)(seed + i * 7);
}

static bool setup(void) {
    send_times_out = false;
    hw_resets = sw_resets = sends = bad_direction = 0;
    fake_ms = 1000;
    reset_line = false;
    chip_reset();
    return w6100_init(MAC);
}

// place socket 0's pointers so the next frame straddles the buffer end
static void seat_pointers(uint16_t at) {
    tx_rd = tx_wr = at;
    rx_rd = rx_wr = at;
    sync_ptr_regs();
}

// ---- tests -----------------------------------------------------------------

static void test_init_sequence(void) {
    MT_ASSERT(setup());
    MT_ASSERT_EQ(hw_resets, 1);
    MT_ASSERT_EQ(sw_resets, 1);
    MT_ASSERT_EQ(bad_direction, 0);
    MT_ASSERT_EQ(creg[0x2000] & 0xE0, 0); // chip, net, PHY unlocked
    MT_ASSERT(memcmp(&creg[0x4120], MAC, 6) == 0);
    for (int s = 1; s < 8; s++) {
        MT_ASSERT_EQ(sreg[s][0x200], 0);
        MT_ASSERT_EQ(sreg[s][0x220], 0);
    }
    MT_ASSERT_EQ(sreg[0][0x200], 16);
    MT_ASSERT_EQ(sreg[0][0x220], 16);
    MT_ASSERT_EQ(sreg[0][0x00], 0x87); // MACRAW + MAC filter
    MT_ASSERT_EQ(sreg[0][0x30], 0x42); // SOCK_MACRAW
    MT_ASSERT_EQ(sreg[0][0x24], 0x04); // Sn_IMR: RECV only
    MT_ASSERT_EQ(creg[0x2114], 0x01);  // SIMR: socket 0
    MT_ASSERT_EQ(creg[0x2104], 0x00);  // IMR: nothing chip-level
    MT_ASSERT_EQ(creg[0x2005] & 0x80, 0x80); // SYCR1 IEN
    MT_ASSERT_EQ(w6100_version(), 0x4661);
}

static void test_init_rejects_wrong_chip(void) {
    cidr = 0x5500;
    MT_ASSERT(!setup());
    cidr = 0x6100;
}

static void test_rx_one_frame(void) {
    MT_ASSERT(setup());
    uint8_t f[64], got[64];
    pattern(f, sizeof(f), 1);
    MT_ASSERT_EQ(w6100_rx_begin(), 0); // nothing yet
    inject_rx(f, sizeof(f));
    MT_ASSERT(w6100_rx_pending());
    MT_ASSERT_EQ(w6100_rx_begin(), 64);
    w6100_rx_read(got, 64);
    w6100_rx_end();
    MT_ASSERT(memcmp(f, got, 64) == 0);
    MT_ASSERT_EQ(rx_rd, 66); // header + frame released
    MT_ASSERT(!w6100_rx_pending());
    MT_ASSERT_EQ(bad_direction, 0);
}

static void test_rx_reads_in_pieces(void) {
    MT_ASSERT(setup());
    uint8_t f[300], got[300];
    pattern(f, sizeof(f), 9);
    inject_rx(f, sizeof(f));
    MT_ASSERT_EQ(w6100_rx_begin(), 300);
    w6100_rx_read(got, 100);       // pbuf chain of unequal pieces
    w6100_rx_read(got + 100, 150);
    w6100_rx_read(got + 250, 50);
    w6100_rx_end();
    MT_ASSERT(memcmp(f, got, 300) == 0);
}

static void test_rx_wraps_buffer_end(void) {
    MT_ASSERT(setup());
    seat_pointers((uint16_t)(BUFSZ - 40)); // header + 38 bytes fit, the rest wraps
    uint8_t f[200], got[200];
    pattern(f, sizeof(f), 33);
    inject_rx(f, sizeof(f));
    MT_ASSERT_EQ(w6100_rx_begin(), 200);
    w6100_rx_read(got, 200);
    w6100_rx_end();
    MT_ASSERT(memcmp(f, got, 200) == 0);
    MT_ASSERT_EQ(rx_rd, (uint16_t)(BUFSZ - 40 + 202));
}

static void test_rx_wraps_16bit_pointer(void) {
    MT_ASSERT(setup());
    seat_pointers(0xFFF0); // pointers pass 0xFFFF, buffer index wraps at 16 KB
    uint8_t f[100], got[100];
    pattern(f, sizeof(f), 77);
    inject_rx(f, sizeof(f));
    MT_ASSERT_EQ(w6100_rx_begin(), 100);
    w6100_rx_read(got, 100);
    w6100_rx_end();
    MT_ASSERT(memcmp(f, got, 100) == 0);
    MT_ASSERT_EQ(rx_rd, (uint16_t)(0xFFF0 + 102));
}

static void test_rx_two_frames_back_to_back(void) {
    MT_ASSERT(setup());
    uint8_t a[60], b[1514], got[1514];
    pattern(a, sizeof(a), 2);
    pattern(b, sizeof(b), 3);
    inject_rx(a, sizeof(a));
    inject_rx(b, sizeof(b));
    MT_ASSERT_EQ(w6100_rx_begin(), 60);
    w6100_rx_read(got, 60);
    w6100_rx_end();
    MT_ASSERT(memcmp(a, got, 60) == 0);
    MT_ASSERT_EQ(w6100_rx_begin(), 1514);
    w6100_rx_read(got, 1514);
    w6100_rx_end();
    MT_ASSERT(memcmp(b, got, 1514) == 0);
    MT_ASSERT_EQ(w6100_rx_begin(), 0);
}

static void test_rx_skip_without_reading(void) {
    MT_ASSERT(setup());
    uint8_t a[80], b[40], got[40];
    pattern(a, sizeof(a), 4);
    pattern(b, sizeof(b), 5);
    inject_rx(a, sizeof(a));
    inject_rx(b, sizeof(b));
    MT_ASSERT_EQ(w6100_rx_begin(), 80);
    w6100_rx_end(); // no buffer for it: dropped, next one intact
    MT_ASSERT_EQ(w6100_rx_begin(), 40);
    w6100_rx_read(got, 40);
    w6100_rx_end();
    MT_ASSERT(memcmp(b, got, 40) == 0);
}

static void test_rx_bad_header_resyncs(void) {
    MT_ASSERT(setup());
    uint8_t f[50];
    pattern(f, sizeof(f), 6);
    inject_rx(f, sizeof(f));
    rxbuf[0] = 0x7F; // length claims 32 KB: more than the chip holds
    rxbuf[1] = 0xFF;
    MT_ASSERT_EQ(w6100_rx_begin(), 0);
    MT_ASSERT(!w6100_rx_pending()); // everything buffered was discarded
    MT_ASSERT_EQ(rx_rd, rx_wr);
}

static void test_irq_ack_clears_recv(void) {
    MT_ASSERT(setup());
    uint8_t f[60];
    pattern(f, sizeof(f), 8);
    inject_rx(f, sizeof(f));
    MT_ASSERT_EQ(s0_ir & 0x04, 0x04);
    w6100_irq_ack();
    MT_ASSERT_EQ(s0_ir & 0x04, 0);
    MT_ASSERT(w6100_rx_pending()); // the data is still there to drain
}

static void test_tx_one_frame_in_pieces(void) {
    MT_ASSERT(setup());
    uint8_t f[700];
    pattern(f, sizeof(f), 11);
    MT_ASSERT(w6100_tx_begin(700));
    w6100_tx_write(f, 14);        // ethernet header pbuf
    w6100_tx_write(f + 14, 686);  // payload pbuf
    MT_ASSERT(w6100_tx_end());
    MT_ASSERT_EQ(sends, 1);
    MT_ASSERT_EQ(sent_len, 700);
    MT_ASSERT(memcmp(sent, f, 700) == 0);
    MT_ASSERT_EQ(s0_ir & 0x18, 0); // SENDOK acknowledged
    MT_ASSERT_EQ(tx_wr, 700);
    MT_ASSERT_EQ(bad_direction, 0);
}

static void test_tx_wraps_buffer_end(void) {
    MT_ASSERT(setup());
    seat_pointers((uint16_t)(BUFSZ - 100));
    uint8_t f[1000];
    pattern(f, sizeof(f), 12);
    MT_ASSERT(w6100_tx_begin(1000));
    w6100_tx_write(f, 1000);
    MT_ASSERT(w6100_tx_end());
    MT_ASSERT_EQ(sent_len, 1000);
    MT_ASSERT(memcmp(sent, f, 1000) == 0);
}

static void test_tx_waits_for_space_then_gives_up(void) {
    MT_ASSERT(setup());
    tx_rd = 0;
    tx_wr = (uint16_t)(BUFSZ - 100); // 100 bytes free, chip never drains
    sync_ptr_regs();
    MT_ASSERT(w6100_tx_begin(100));
    MT_ASSERT(!w6100_tx_begin(101)); // times out rather than hanging lwIP
    MT_ASSERT(!w6100_tx_begin(W6100_MAX_FRAME + 1));
}

static void test_tx_timeout_reported(void) {
    MT_ASSERT(setup());
    send_times_out = true;
    uint8_t f[64];
    pattern(f, sizeof(f), 13);
    MT_ASSERT(w6100_tx_begin(64));
    w6100_tx_write(f, 64);
    MT_ASSERT(!w6100_tx_end());
    MT_ASSERT_EQ(s0_ir & 0x18, 0); // TIMEOUT cleared so the next send starts clean
    send_times_out = false;
    MT_ASSERT(w6100_tx_begin(64));
    w6100_tx_write(f, 64);
    MT_ASSERT(w6100_tx_end());
}

static void test_link_follows_physr(void) {
    MT_ASSERT(setup());
    creg[0x3000] = 0x00;
    MT_ASSERT(!w6100_link_up());
    creg[0x3000] = 0x01; // link up; SPD/DPX clear = 100M full
    MT_ASSERT(w6100_link_up());
    MT_ASSERT_EQ(w6100_phy_status(), 0x01);
}

void run_w6100_tests(void) {
    printf("w6100 driver\n");
    mt_run("init sequence: resets, unlocks, buffers, MACRAW open", test_init_sequence);
    mt_run("init rejects a chip with the wrong CIDR", test_init_rejects_wrong_chip);
    mt_run("rx one frame", test_rx_one_frame);
    mt_run("rx reads in pieces", test_rx_reads_in_pieces);
    mt_run("rx wraps at the buffer end", test_rx_wraps_buffer_end);
    mt_run("rx wraps the 16-bit pointer", test_rx_wraps_16bit_pointer);
    mt_run("rx two frames back to back", test_rx_two_frames_back_to_back);
    mt_run("rx skip without reading", test_rx_skip_without_reading);
    mt_run("rx bad header resyncs", test_rx_bad_header_resyncs);
    mt_run("irq ack clears RECV", test_irq_ack_clears_recv);
    mt_run("tx one frame in pieces", test_tx_one_frame_in_pieces);
    mt_run("tx wraps at the buffer end", test_tx_wraps_buffer_end);
    mt_run("tx waits for space then gives up", test_tx_waits_for_space_then_gives_up);
    mt_run("tx timeout reported and cleared", test_tx_timeout_reported);
    mt_run("link follows PHYSR", test_link_follows_physr);
}
