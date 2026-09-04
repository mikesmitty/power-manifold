#include "w6100.h"

#include <string.h>

#include "w6100_hw.h"

// ---- register map ----------------------------------------------------------

// control byte: block select [7:3], R/W [2], OM[1:0] = 00 variable length
#define BLK_COMMON      0x00
#define BLK_SREG(n)     ((uint8_t)((1 + 4 * (n)) << 3))
#define BLK_TXBUF(n)    ((uint8_t)((2 + 4 * (n)) << 3))
#define BLK_RXBUF(n)    ((uint8_t)((3 + 4 * (n)) << 3))
#define CTRL_WRITE      0x04

// common registers
#define CIDR            0x0000 // 0x6100
#define VER             0x0002
#define SYSR            0x2000
#define SYCR0           0x2004
#define SYCR1           0x2005
#define IMR             0x2104
#define SIMR            0x2114
#define PHYSR           0x3000
#define SHAR            0x4120
#define CHPLCKR         0x41F4
#define NETLCKR         0x41F5
#define PHYLCKR         0x41F6

#define SYSR_CHPL       0x80 // set = chip registers locked
#define SYSR_NETL       0x40
#define SYSR_PHYL       0x20
#define SYCR0_RST       0x00 // writing 0 resets the chip
#define SYCR1_IEN       0x80
#define CHPLCKR_UNLOCK  0xCE
#define NETLCKR_UNLOCK  0x3A
#define PHYLCKR_UNLOCK  0x53
#define PHYSR_LNK       0x01

// socket registers (offsets within a socket's block)
#define Sn_MR           0x0000
#define Sn_CR           0x0010
#define Sn_IR           0x0020
#define Sn_IMR          0x0024
#define Sn_IRCLR        0x0028
#define Sn_SR           0x0030
#define Sn_TX_BSR       0x0200
#define Sn_TX_FSR       0x0204
#define Sn_TX_WR        0x020C
#define Sn_RX_BSR       0x0220
#define Sn_RX_RSR       0x0224
#define Sn_RX_RD        0x0228

#define Sn_MR_MACRAW    0x07
#define Sn_MR_MF        0x80 // MAC filter: ours, broadcast, multicast only
#define Sn_CR_OPEN      0x01
#define Sn_CR_SEND      0x20
#define Sn_CR_RECV      0x40
#define Sn_IR_SENDOK    0x10
#define Sn_IR_TIMEOUT   0x08
#define Sn_IR_RECV      0x04
#define SOCK_MACRAW     0x42

#define SOCK            0    // MACRAW is socket 0 only
#define BUF_KB          16
#define BUF_SIZE        (BUF_KB * 1024u)

#define LOCK_TRIES      50
#define TX_WAIT_MS      20
#define SEND_WAIT_MS    50

// ---- bus helpers -----------------------------------------------------------

static void frame(uint8_t block, uint16_t addr, bool write) {
    uint8_t h[3] = {(uint8_t)(addr >> 8), (uint8_t)addr,
                    (uint8_t)(block | (write ? CTRL_WRITE : 0))};
    w6100_hw_write(h, 3);
}

static void rd_buf(uint8_t block, uint16_t addr, uint8_t *dst, size_t n) {
    w6100_hw_select(true);
    frame(block, addr, false);
    w6100_hw_read(dst, n);
    w6100_hw_select(false);
}

static void wr_buf(uint8_t block, uint16_t addr, const uint8_t *src, size_t n) {
    w6100_hw_select(true);
    frame(block, addr, true);
    w6100_hw_write(src, n);
    w6100_hw_select(false);
}

static uint8_t rd8(uint8_t block, uint16_t addr) {
    uint8_t v;
    rd_buf(block, addr, &v, 1);
    return v;
}

static uint16_t rd16(uint8_t block, uint16_t addr) {
    uint8_t v[2];
    rd_buf(block, addr, v, 2);
    return (uint16_t)((v[0] << 8) | v[1]);
}

static void wr8(uint8_t block, uint16_t addr, uint8_t v) {
    wr_buf(block, addr, &v, 1);
}

static void wr16(uint8_t block, uint16_t addr, uint16_t v) {
    uint8_t b[2] = {(uint8_t)(v >> 8), (uint8_t)v};
    wr_buf(block, addr, b, 2);
}

// Free-size / received-size counters can change between their two bytes;
// read until two reads agree (the datasheet's recommended access).
static uint16_t rd16_stable(uint8_t block, uint16_t addr) {
    uint16_t a = rd16(block, addr);
    for (;;) {
        uint16_t b = rd16(block, addr);
        if (a == b) return a;
        a = b;
    }
}

static void cmd(uint8_t c) {
    wr8(BLK_SREG(SOCK), Sn_CR, c);
    while (rd8(BLK_SREG(SOCK), Sn_CR)) {} // auto-clears once accepted
}

static bool wait_sysr(uint8_t mask, bool set) {
    for (int i = 0; i < LOCK_TRIES; i++) {
        bool is_set = (rd8(BLK_COMMON, SYSR) & mask) != 0;
        if (is_set == set) return true;
        w6100_hw_delay_ms(1);
    }
    return false;
}

// ---- init ------------------------------------------------------------------

static uint16_t version;
static w6100_fail_t fail;

static bool failed(const char *step) {
    fail.step = step;
    fail.cidr = rd16(BLK_COMMON, CIDR);
    fail.ver = rd16(BLK_COMMON, VER);
    fail.sysr = rd8(BLK_COMMON, SYSR);
    return false;
}

const w6100_fail_t *w6100_last_failure(void) {
    return &fail;
}

bool w6100_init(const uint8_t mac[6]) {
    w6100_hw_init();
    w6100_hw_select(false);
    w6100_hw_reset(true);
    w6100_hw_delay_ms(2);
    w6100_hw_reset(false);
    w6100_hw_delay_ms(100); // WIZnet's own examples give it this long

    // identity first: an absent chip fails here in one read
    if (rd16(BLK_COMMON, CIDR) != 0x6100) return failed("id");

    // software reset needs the chip block unlocked; the chip relocks itself
    wr8(BLK_COMMON, CHPLCKR, CHPLCKR_UNLOCK);
    if (!wait_sysr(SYSR_CHPL, false)) return failed("unlock");
    wr8(BLK_COMMON, SYCR0, SYCR0_RST);
    if (!wait_sysr(SYSR_CHPL, true)) return failed("soft reset");

    wr8(BLK_COMMON, CHPLCKR, CHPLCKR_UNLOCK);
    wr8(BLK_COMMON, NETLCKR, NETLCKR_UNLOCK);
    wr8(BLK_COMMON, PHYLCKR, PHYLCKR_UNLOCK);
    if (!wait_sysr(SYSR_CHPL | SYSR_NETL | SYSR_PHYL, false)) return failed("unlock all");

    if (rd16(BLK_COMMON, CIDR) != 0x6100) return failed("id after reset");
    version = rd16(BLK_COMMON, VER);

    // the whole buffer to socket 0: others must give theirs up first so the
    // total never exceeds the 16 KB per direction the chip has
    for (int s = 7; s >= 1; s--) {
        wr8(BLK_SREG(s), Sn_TX_BSR, 0);
        wr8(BLK_SREG(s), Sn_RX_BSR, 0);
    }
    wr8(BLK_SREG(SOCK), Sn_TX_BSR, BUF_KB);
    wr8(BLK_SREG(SOCK), Sn_RX_BSR, BUF_KB);

    wr_buf(BLK_COMMON, SHAR, mac, 6);

    // INTn: socket 0 RECV only
    wr8(BLK_SREG(SOCK), Sn_IRCLR, 0xFF);
    wr8(BLK_SREG(SOCK), Sn_IMR, Sn_IR_RECV);
    wr8(BLK_COMMON, SIMR, 1u << SOCK);
    wr8(BLK_COMMON, IMR, 0);
    wr8(BLK_COMMON, SYCR1, SYCR1_IEN);

    wr8(BLK_SREG(SOCK), Sn_MR, Sn_MR_MACRAW | Sn_MR_MF);
    cmd(Sn_CR_OPEN);
    if (rd8(BLK_SREG(SOCK), Sn_SR) != SOCK_MACRAW) return failed("open");
    return true;
}

uint16_t w6100_version(void) {
    return version;
}

bool w6100_link_up(void) {
    return (rd8(BLK_COMMON, PHYSR) & PHYSR_LNK) != 0;
}

uint8_t w6100_phy_status(void) {
    return rd8(BLK_COMMON, PHYSR);
}

// ---- receive ---------------------------------------------------------------

// Each MACRAW frame sits behind a 2-byte big-endian header holding the
// frame length plus the header's own 2 bytes. The 16-bit buffer address
// wraps inside the socket buffer on the chip, so reads never split.
static uint16_t rx_rd;    // RX_RD at frame start
static uint16_t rx_pos;   // next byte to read
static uint16_t rx_total; // header + frame

uint16_t w6100_rx_begin(void) {
    uint16_t rsr = rd16_stable(BLK_SREG(SOCK), Sn_RX_RSR);
    if (rsr < 2) return 0;
    rx_rd = rd16(BLK_SREG(SOCK), Sn_RX_RD);
    uint8_t h[2];
    rd_buf(BLK_RXBUF(SOCK), rx_rd, h, 2);
    uint16_t total = (uint16_t)((h[0] << 8) | h[1]);
    if (total < 2 || total > rsr || total - 2 > W6100_MAX_FRAME) {
        // header nonsense: the pointers are out of step, drop everything
        // buffered and let the chip start clean
        wr16(BLK_SREG(SOCK), Sn_RX_RD, (uint16_t)(rx_rd + rsr));
        cmd(Sn_CR_RECV);
        return 0;
    }
    rx_pos = (uint16_t)(rx_rd + 2);
    rx_total = total;
    return (uint16_t)(total - 2);
}

void w6100_rx_read(uint8_t *dst, uint16_t n) {
    rd_buf(BLK_RXBUF(SOCK), rx_pos, dst, n);
    rx_pos = (uint16_t)(rx_pos + n);
}

void w6100_rx_end(void) {
    wr16(BLK_SREG(SOCK), Sn_RX_RD, (uint16_t)(rx_rd + rx_total));
    cmd(Sn_CR_RECV);
}

bool w6100_rx_pending(void) {
    return rd16_stable(BLK_SREG(SOCK), Sn_RX_RSR) != 0;
}

void w6100_irq_ack(void) {
    wr8(BLK_SREG(SOCK), Sn_IRCLR, Sn_IR_RECV);
}

// ---- transmit --------------------------------------------------------------

static uint16_t tx_pos;

bool w6100_tx_begin(uint16_t len) {
    if (len > W6100_MAX_FRAME) return false;
    uint32_t start = w6100_hw_ms();
    for (;;) {
        if (rd16_stable(BLK_SREG(SOCK), Sn_TX_FSR) >= len) break;
        if (rd8(BLK_SREG(SOCK), Sn_SR) != SOCK_MACRAW) return false;
        if (w6100_hw_ms() - start > TX_WAIT_MS) return false;
    }
    tx_pos = rd16(BLK_SREG(SOCK), Sn_TX_WR);
    return true;
}

void w6100_tx_write(const uint8_t *src, uint16_t n) {
    wr_buf(BLK_TXBUF(SOCK), tx_pos, src, n);
    tx_pos = (uint16_t)(tx_pos + n);
}

bool w6100_tx_end(void) {
    wr16(BLK_SREG(SOCK), Sn_TX_WR, tx_pos);
    cmd(Sn_CR_SEND);
    uint32_t start = w6100_hw_ms();
    for (;;) {
        uint8_t ir = rd8(BLK_SREG(SOCK), Sn_IR);
        if (ir & (Sn_IR_SENDOK | Sn_IR_TIMEOUT)) {
            wr8(BLK_SREG(SOCK), Sn_IRCLR, Sn_IR_SENDOK | Sn_IR_TIMEOUT);
            return (ir & Sn_IR_SENDOK) != 0;
        }
        if (w6100_hw_ms() - start > SEND_WAIT_MS) return false;
    }
}
