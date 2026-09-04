#pragma once

#include <stdbool.h>
#include <stdint.h>

// WIZnet W6100 in MACRAW mode: socket 0 carries raw Ethernet frames and
// lwIP does everything above the MAC, so the chip's own TCP/IP offload
// (and its IPv6 stack) goes unused; MQTT/HTTP/mDNS stay transport-agnostic.
// All 32 KB of socket memory is given to socket 0 (16 KB each way).
//
// Register map per WIZnet's W6100 datasheet / ioLibrary w6100.h. The SPI
// frame is the W5500 one: 16-bit address, control byte (block select,
// R/W, variable-length mode), data.

#define W6100_MAX_FRAME 1514 // MACRAW frame limit, no FCS

// Hardware reset, software reset, identity check, buffer split, MAC,
// interrupt routing (socket 0 RECV on INTn) and MACRAW open. False when no
// chip answers (CIDR wrong) or the socket refuses MACRAW.
bool w6100_init(const uint8_t mac[6]);
// Why the last w6100_init() failed, with the raw reads taken at that point
// (boot-log material: an absent chip reads as all-ones or all-zeros).
typedef struct { const char *step; uint16_t cidr, ver; uint8_t sysr; } w6100_fail_t;
const w6100_fail_t *w6100_last_failure(void);
uint16_t w6100_version(void); // VER register, valid after init

bool w6100_link_up(void);
uint8_t w6100_phy_status(void); // raw PHYSR: LNK bit0 (1 = up), SPD bit1 (1 = 10M), DPX bit2 (1 = half)

// Receive: begin returns the next frame's length (0 = nothing waiting) and
// leaves the frame ready for sequential reads; end releases it to the
// chip. A frame the caller can't take can be ended without reading.
uint16_t w6100_rx_begin(void);
void     w6100_rx_read(uint8_t *dst, uint16_t n);
void     w6100_rx_end(void);
bool     w6100_rx_pending(void); // bytes waiting in the receive buffer
void     w6100_irq_ack(void);    // clear socket 0's RECV interrupt (INTn)

// Transmit: begin waits (briefly) for len bytes of free buffer, write
// appends pieces, end issues SEND and waits for completion.
bool w6100_tx_begin(uint16_t len);
void w6100_tx_write(const uint8_t *src, uint16_t n);
bool w6100_tx_end(void);
