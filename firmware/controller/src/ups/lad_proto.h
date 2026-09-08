#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Mean Well LAD-xxxU UPS power supply: the UART protocol from the LAD series
// user manual (ch. 5.4). Not Modbus — a 9600 8N1 point-to-point link with
// its own frame:
//
//   [RW][LEN][ADDR_H][ADDR_L][DATA...][CRC8]
//
// RW is 0x55 for a read and 0xAA for a write (replies carry 0x55); LEN counts
// every byte after it (2 address + data + 1 CRC); values are big-endian; the
// CRC-8 (poly 0x07, init 0, no reflection) covers RW through the last data
// byte — verified against the manual's worked examples in the host tests.
// The supply answers within 5 ms and wants requests at least 20 ms apart.
// Hardware-free: the poller in ups.c drives it through lad_hw.h.

#define LAD_RW_READ   0x55
#define LAD_RW_WRITE  0xAA

// Read addresses and the unit of what comes back
#define LAD_ADDR_STATUS   0x0010 // 4 bytes: STATUS_H then STATUS_L
#define LAD_ADDR_MAINS_V  0x0020 // u16, 0.1 V: AC input voltage
#define LAD_ADDR_LOAD_A   0x0030 // u16, 0.01 A: load current
#define LAD_ADDR_BATT_V   0x0040 // u16, 0.01 V: battery string voltage
#define LAD_ADDR_CELLS    0x0050 // 4 x u16, 0.01 V: per-block voltages (0xFFFF = tap not wired)
#define LAD_ADDR_UVP      0x0060 // u16, 0.01 V: battery undervoltage protection point

// Write addresses (volatile: the supply forgets them when it powers down)
#define LAD_WADDR_BACKUP_OFF 0x0010 // u8: 1 = drop the battery backup function
#define LAD_WADDR_UVP        0x0020 // u16, 0.01 V
#define LAD_WADDR_BUZZER     0x0030 // u8: 1 = buzzer off, 0 = on
#define LAD_WADDR_STANDBY_EN 0x0040 // u8: re-enable the backup function

// STATUS_L bits (16-bit, manual's "high byte" in bits 15..8)
#define LAD_ST_AC_OK       (1u << 0)  // AC input normal
#define LAD_ST_BAT_NC      (1u << 1)  // abnormal battery access (missing / not connected)
#define LAD_ST_DISCH_OLP   (1u << 2)  // main discharge overload
#define LAD_ST_BAT_UVP     (1u << 3)  // battery undervoltage protection tripped
#define LAD_ST_ON_BATTERY  (1u << 4)  // load carried by the battery (0 = mains)
#define LAD_ST_LINK_CTRL   (1u << 5)  // remote-UPS pins 3/4 shorted
#define LAD_ST_BAT_OVP     (1u << 6)  // battery overvoltage protection
#define LAD_ST_UNBALANCED  (1u << 7)  // battery string unbalanced
#define LAD_ST_BAT_ERR1    (1u << 8)  // per-block abnormal, blocks 1..4
#define LAD_ST_BAT_ERR2    (1u << 9)
#define LAD_ST_BAT_ERR3    (1u << 10)
#define LAD_ST_BAT_ERR4    (1u << 11)
#define LAD_ST_CHG_FULL    (1u << 12) // battery full
#define LAD_ST_CHARGING    (1u << 13)
#define LAD_ST_BAT_REV     (1u << 14) // battery reversed
#define LAD_ST_FORCED      (1u << 15) // force-start pins 1/2 shorted
// STATUS_H
#define LAD_STH_BAT_SW_OFF (1u << 0)  // the front battery switch is off

// Anything in here means the battery side needs attention
#define LAD_ST_FAULT_MASK \
    (LAD_ST_BAT_NC | LAD_ST_DISCH_OLP | LAD_ST_BAT_UVP | LAD_ST_BAT_OVP | LAD_ST_UNBALANCED | \
     LAD_ST_BAT_ERR1 | LAD_ST_BAT_ERR2 | LAD_ST_BAT_ERR3 | LAD_ST_BAT_ERR4 | LAD_ST_BAT_REV)

#define LAD_DATA_MAX  8                   // the cell-voltage reply
#define LAD_FRAME_MAX (4 + LAD_DATA_MAX + 1)

uint8_t lad_crc8(const uint8_t *p, size_t n);

// Request builders; return the frame length (fits LAD_FRAME_MAX).
size_t lad_build_read(uint8_t *out, uint16_t addr);
size_t lad_build_write(uint8_t *out, uint16_t addr, const uint8_t *data, size_t n);

typedef struct {
    uint8_t rw;
    uint16_t addr;
    uint8_t len;                 // data bytes
    uint8_t data[LAD_DATA_MAX];
} lad_frame_t;

typedef struct {
    uint8_t buf[LAD_FRAME_MAX];
    uint8_t n;
    uint32_t bad;                // frames dropped for a bad CRC or length
} lad_parser_t;

void lad_parser_reset(lad_parser_t *p);

// Feed one received byte. True when it completed a CRC-valid frame, copied
// to *f. Garbage and truncated frames are skipped by resynchronising on the
// next plausible RW byte, so a noisy or floating line cannot wedge it.
bool lad_parser_feed(lad_parser_t *p, uint8_t b, lad_frame_t *f);

static inline uint16_t lad_u16(const uint8_t *p) {
    return (uint16_t)((p[0] << 8) | p[1]);
}
