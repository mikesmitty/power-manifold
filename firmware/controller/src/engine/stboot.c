#include "stboot.h"

#include "hardware/i2c.h"

#include "blade_image.h"
#include "blade_regs.h"
#include "pins.h"

#define ADDR BLADE_LOADER_I2C_ADDR
#define ACK  0x79
#define NACK 0x1F
#define BUSY 0x76

#define FRAME_TIMEOUT_US   (10 * 1000)  // one frame of up to 258 bytes at 100 kHz
// A stretched erase or write holds the clock for the flash operation: page
// erase and a 256-byte program are each well under this on the G0.
#define STRETCH_TIMEOUT_US (200 * 1000)

static bool send(const uint8_t *buf, size_t n) {
    return i2c_write_timeout_us(I2C_BUS, ADDR, buf, n, false, FRAME_TIMEOUT_US) == (int)n;
}

static bool receive(uint8_t *buf, size_t n, uint32_t timeout_us) {
    return i2c_read_timeout_us(I2C_BUS, ADDR, buf, n, false, timeout_us) == (int)n;
}

static stboot_result_t ack(uint32_t timeout_us) {
    uint8_t a;
    if (!receive(&a, 1, timeout_us)) return STBOOT_SILENT;
    switch (a) {
    case ACK:  return STBOOT_OK;
    case BUSY: return STBOOT_BUSY;
    case NACK: return STBOOT_NACK;
    default:   return STBOOT_SILENT;
    }
}

static stboot_result_t command(uint8_t cmd) {
    uint8_t f[2] = {cmd, (uint8_t)~cmd};
    if (!send(f, 2)) return STBOOT_SILENT;
    return ack(FRAME_TIMEOUT_US);
}

// A 32-bit argument, most significant byte first, with its XOR
static stboot_result_t send_u32(uint32_t v) {
    uint8_t f[5] = {(uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v, 0};
    f[4] = (uint8_t)(f[0] ^ f[1] ^ f[2] ^ f[3]);
    if (!send(f, 5)) return STBOOT_SILENT;
    return ack(FRAME_TIMEOUT_US);
}

stboot_result_t stboot_version(uint8_t *version) {
    stboot_result_t r = command(0x01);
    if (r != STBOOT_OK) return r;
    if (!receive(version, 1, FRAME_TIMEOUT_US)) return STBOOT_SILENT;
    return ack(FRAME_TIMEOUT_US);
}

stboot_result_t stboot_id(uint16_t *pid) {
    stboot_result_t r = command(0x02);
    if (r != STBOOT_OK) return r;
    uint8_t d[3]; // N = 1, then the ID, most significant byte first
    if (!receive(d, 3, FRAME_TIMEOUT_US)) return STBOOT_SILENT;
    if (d[0] != 1) return STBOOT_SILENT;
    *pid = (uint16_t)(d[1] << 8 | d[2]);
    return ack(FRAME_TIMEOUT_US);
}

stboot_result_t stboot_read(uint32_t addr, uint8_t *buf, size_t n) {
    if (!n || n > STBOOT_CHUNK) return STBOOT_NACK;
    stboot_result_t r = command(0x11);
    if (r != STBOOT_OK) return r;
    if ((r = send_u32(addr)) != STBOOT_OK) return r;
    uint8_t f[2] = {(uint8_t)(n - 1), (uint8_t)~(n - 1)};
    if (!send(f, 2)) return STBOOT_SILENT;
    if ((r = ack(FRAME_TIMEOUT_US)) != STBOOT_OK) return r;
    return receive(buf, n, FRAME_TIMEOUT_US) ? STBOOT_OK : STBOOT_SILENT;
}

stboot_result_t stboot_erase(uint16_t first, uint16_t count, bool no_stretch) {
    if (!count || count > 32 || first + count > 32) return STBOOT_NACK; // the 64 KB part's pages
    stboot_result_t r = command(no_stretch ? 0x45 : 0x44);
    if (r != STBOOT_OK) return r;
    uint8_t n[3] = {(uint8_t)((count - 1) >> 8), (uint8_t)(count - 1), 0};
    n[2] = (uint8_t)(n[0] ^ n[1]);
    if (!send(n, 3)) return STBOOT_SILENT;
    if ((r = ack(FRAME_TIMEOUT_US)) != STBOOT_OK) return r;
    uint8_t pages[2 * 32 + 1];
    uint8_t x = 0;
    for (uint16_t k = 0; k < count; k++) {
        uint16_t p = (uint16_t)(first + k);
        pages[2 * k] = (uint8_t)(p >> 8);
        pages[2 * k + 1] = (uint8_t)p;
        x ^= pages[2 * k] ^ pages[2 * k + 1];
    }
    pages[2 * count] = x;
    if (!send(pages, 2u * count + 1)) return STBOOT_SILENT;
    return ack(no_stretch ? FRAME_TIMEOUT_US : STRETCH_TIMEOUT_US);
}

stboot_result_t stboot_write(uint32_t addr, const uint8_t *data, size_t n, bool no_stretch) {
    if (!n || n > STBOOT_CHUNK) return STBOOT_NACK;
    stboot_result_t r = command(no_stretch ? 0x32 : 0x31);
    if (r != STBOOT_OK) return r;
    if ((r = send_u32(addr)) != STBOOT_OK) return r;
    uint8_t f[STBOOT_CHUNK + 2];
    f[0] = (uint8_t)(n - 1);
    uint8_t x = f[0];
    for (size_t k = 0; k < n; k++) {
        f[1 + k] = data[k];
        x ^= data[k];
    }
    f[1 + n] = x;
    if (!send(f, n + 2)) return STBOOT_SILENT;
    return ack(no_stretch ? FRAME_TIMEOUT_US : STRETCH_TIMEOUT_US);
}

stboot_result_t stboot_checksum(uint32_t addr, uint32_t len) {
    stboot_result_t r = command(0xA1);
    if (r != STBOOT_OK) return r;
    if ((r = send_u32(addr)) != STBOOT_OK) return r;
    return send_u32(len); // BUSY until the CRC unit is done
}

stboot_result_t stboot_checksum_result(uint32_t *crc) {
    uint8_t d[5]; // most significant byte first, then the XOR of the four
    if (!receive(d, 5, FRAME_TIMEOUT_US)) return STBOOT_SILENT;
    if ((uint8_t)(d[0] ^ d[1] ^ d[2] ^ d[3]) != d[4]) return STBOOT_SILENT;
    *crc = (uint32_t)d[0] << 24 | (uint32_t)d[1] << 16 | (uint32_t)d[2] << 8 | d[3];
    return STBOOT_OK;
}

stboot_result_t stboot_go(uint32_t addr) {
    stboot_result_t r = command(0x21);
    if (r != STBOOT_OK) return r;
    return send_u32(addr);
}

stboot_result_t stboot_poll(void) {
    return ack(FRAME_TIMEOUT_US);
}
