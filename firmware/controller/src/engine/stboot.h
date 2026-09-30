#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// The STM32 ROM bootloader's I2C protocol (AN4221), as far as programming a
// gen-3 charger blade needs it: a client for one blade at a time, at
// BLADE_LOADER_I2C_ADDR on the mux channel the caller has selected.
//
// Every command is a few I2C frames back to back — the command and its
// complement, an ACK, the arguments and their XOR, an ACK — and the ones
// that make the flash work (erase, write, checksum) then take their time.
// Their "no-stretch" variants answer BUSY to each further ACK request
// instead of holding the clock, so the caller comes back later and polls
// (stboot_poll) while the rest of the bus stays free; that is the shape the
// engine's tick wants. The bootloader also resets itself when the host goes
// quiet for too long in the middle of a command, so the frames of one
// command are never split across ticks.

typedef enum {
    STBOOT_OK,     // ACK
    STBOOT_BUSY,   // the bootloader is still at it: poll again
    STBOOT_NACK,   // the bootloader refused the command or its arguments
    STBOOT_SILENT, // nothing answered at the address, or the transfer broke
} stboot_result_t;

#define STBOOT_PROTOCOL_MIN_NS 0x11 // no-stretch commands from protocol V1.1
#define STBOOT_PROTOCOL_MIN_CRC 0x12 // the checksum command from V1.2
#define STBOOT_CHUNK 256 // bytes per read or write command

// Get Version: the protocol version (0x1X), which also says which commands exist.
stboot_result_t stboot_version(uint8_t *version);
// Get ID: the STM32 product ID (BLADE_LOADER_DEVICE_ID for the blade's part).
stboot_result_t stboot_id(uint16_t *pid);
// Read Memory: 1..256 bytes.
stboot_result_t stboot_read(uint32_t addr, uint8_t *buf, size_t n);
// Erase `count` consecutive pages from `first`. no_stretch: the V1.1 form,
// which answers BUSY until done (poll); else the command holds the clock and
// returns once done.
stboot_result_t stboot_erase(uint16_t first, uint16_t count, bool no_stretch);
// Write Memory: 1..256 bytes at addr. no_stretch as for erase.
stboot_result_t stboot_write(uint32_t addr, const uint8_t *data, size_t n, bool no_stretch);
// Get Checksum (V1.2): start the CRC of `len` bytes (a multiple of 4) at
// addr; BUSY until it is done, then stboot_checksum_result fetches it.
stboot_result_t stboot_checksum(uint32_t addr, uint32_t len);
stboot_result_t stboot_checksum_result(uint32_t *crc);
// Go: run the image whose vector table is at addr. The bootloader is gone
// on OK.
stboot_result_t stboot_go(uint32_t addr);
// One more ACK/NACK/BUSY read, for a command that answered BUSY.
stboot_result_t stboot_poll(void);
