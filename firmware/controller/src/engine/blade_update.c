#include "blade_update.h"

#include <string.h>

#include "blade_bundle.h"
#include "stboot.h"

#define TRIES     3   // a command that fails this often ends the trip
#define WAIT_MAX  300 // BUSY polls (ticks) per flash operation before giving up

enum {
    S_VERSION, S_ID, S_INSPECT,
    S_ERASE, S_ERASE_WAIT,
    S_WRITE, S_WRITE_WAIT,
    S_CRC, S_CRC_WAIT, S_READBACK,
    S_GO,
};

typedef struct {
    uint8_t  step, tries, fail;
    uint16_t waits;
    bool     rewrite, no_stretch, has_crc, wrote, rewritten;
    uint32_t chunk;   // position in the write (or read-back) order
    uint32_t length;  // bytes of image on the blade once done
    uint32_t version; // UPDATE_VERSION() of it
    blade_image_header_t have; // what the blade's flash holds
    bool     have_valid;
} update_t;

static update_t u[NUM_PORTS];

uint32_t blade_image_crc(const uint8_t *data, uint32_t size, uint32_t len) {
    uint32_t crc = 0xFFFFFFFFu;
    for (uint32_t i = 0; i + 4 <= len; i += 4) {
        uint32_t w = 0;
        for (int b = 3; b >= 0; b--) w = w << 8 | (i + b < size ? data[i + b] : 0xFFu);
        crc ^= w;
        for (int b = 0; b < 32; b++)
            crc = (crc & 0x80000000u) ? (crc << 1) ^ 0x04C11DB7u : crc << 1;
    }
    return crc;
}

static uint32_t chunks(const update_t *p) {
    return (p->length + STBOOT_CHUNK - 1) / STBOOT_CHUNK;
}

// The write order: every page but the first from the bottom up, then the
// first page from its top chunk down, so the header's chunk goes last.
static uint32_t write_offset(const update_t *p, uint32_t k) {
    uint32_t total = chunks(p);
    uint32_t page0 = BLADE_IMAGE_PAGE / STBOOT_CHUNK;
    if (page0 > total) page0 = total;
    uint32_t rest = total - page0;
    if (k < rest) return (page0 + k) * STBOOT_CHUNK;
    return (page0 - 1 - (k - rest)) * STBOOT_CHUNK;
}

// The image's bytes at off, the erased flash's 0xFF past the bundle
static uint32_t image_bytes(const update_t *p, uint32_t off, uint8_t *out) {
    uint32_t n = p->length - off;
    if (n > STBOOT_CHUNK) n = STBOOT_CHUNK;
    uint32_t size = blade_bundle_size();
    const uint8_t *data = blade_bundle_data();
    for (uint32_t k = 0; k < n; k++) out[k] = off + k < size ? data[off + k] : 0xFF;
    return n;
}

static void next(update_t *p, uint8_t step) {
    p->step = step;
    p->tries = 0;
    p->waits = 0;
}

static blade_update_status_t fail(update_t *p, uint8_t why) {
    p->fail = why;
    return BLADE_UPDATE_FAILED;
}

// The same step again next tick, until it has failed TRIES times
static blade_update_status_t retry(update_t *p, uint8_t why) {
    if (++p->tries >= TRIES) return fail(p, why);
    return BLADE_UPDATE_BUSY;
}

static blade_update_status_t wait_more(update_t *p, uint8_t why) {
    if (++p->waits >= WAIT_MAX) return fail(p, why);
    return BLADE_UPDATE_BUSY;
}

static void start_verify(update_t *p) {
    p->chunk = 0;
    next(p, p->has_crc ? S_CRC : S_READBACK);
}

// Not what was sent: once more from the erase, then give up
static blade_update_status_t verify_failed(update_t *p) {
    if (p->rewritten) return fail(p, UPDATE_FAIL_VERIFY);
    p->rewritten = true;
    next(p, S_ERASE);
    return BLADE_UPDATE_BUSY;
}

static blade_update_status_t crc_result(update_t *p) {
    uint32_t crc;
    if (stboot_checksum_result(&crc) != STBOOT_OK) return retry(p, UPDATE_FAIL_VERIFY);
    if (crc != blade_image_crc(blade_bundle_data(), blade_bundle_size(), p->length))
        return verify_failed(p);
    next(p, S_GO);
    return BLADE_UPDATE_BUSY;
}

void blade_update_begin(uint8_t port, bool rewrite) {
    update_t *p = &u[port];
    memset(p, 0, sizeof *p);
    p->rewrite = rewrite;
}

blade_update_status_t blade_update_step(uint8_t port) {
    update_t *p = &u[port];
    const blade_image_header_t *want = blade_bundle_header();
    stboot_result_t r;
    uint8_t buf[STBOOT_CHUNK];

    switch (p->step) {
    case S_VERSION: {
        uint8_t v;
        if (stboot_version(&v) != STBOOT_OK) return retry(p, UPDATE_FAIL_SILENT);
        if ((v >> 4) != 1) return fail(p, UPDATE_FAIL_CHIP); // this driver speaks protocol 1.x
        p->no_stretch = v >= STBOOT_PROTOCOL_MIN_NS;
        p->has_crc = v >= STBOOT_PROTOCOL_MIN_CRC;
        next(p, S_ID);
        return BLADE_UPDATE_BUSY;
    }
    case S_ID: {
        uint16_t pid;
        if (stboot_id(&pid) != STBOOT_OK) return retry(p, UPDATE_FAIL_SILENT);
        if (pid != BLADE_LOADER_DEVICE_ID) return fail(p, UPDATE_FAIL_CHIP);
        next(p, S_INSPECT);
        return BLADE_UPDATE_BUSY;
    }
    case S_INSPECT: {
        if (stboot_read(BLADE_IMAGE_BASE + BLADE_IMAGE_HEADER_OFFSET, buf, sizeof p->have) != STBOOT_OK)
            return retry(p, UPDATE_FAIL_SILENT);
        memcpy(&p->have, buf, sizeof p->have);
        p->have_valid = p->have.magic == BLADE_IMAGE_MAGIC && p->have.length <= BLADE_IMAGE_MAX &&
                        p->have.length > BLADE_IMAGE_HEADER_OFFSET + sizeof p->have;
        bool same = want && p->have_valid && p->have.length == want->length &&
                    p->have.major == want->major && p->have.minor == want->minor &&
                    p->have.patch == want->patch;
        if (want && (p->rewrite || !same)) {
            p->length = want->length;
            p->version = UPDATE_VERSION(want->major, want->minor, want->patch);
            next(p, S_ERASE);
        } else if (same) {
            p->length = want->length;
            p->version = UPDATE_VERSION(want->major, want->minor, want->patch);
            start_verify(p);
        } else if (p->have_valid) {
            p->length = p->have.length;
            p->version = UPDATE_VERSION(p->have.major, p->have.minor, p->have.patch);
            next(p, S_GO); // nothing to check it against
        } else {
            return fail(p, UPDATE_FAIL_NO_IMAGE);
        }
        return BLADE_UPDATE_BUSY;
    }
    case S_ERASE: {
        uint16_t pages = (uint16_t)((p->length + BLADE_IMAGE_PAGE - 1) / BLADE_IMAGE_PAGE);
        r = stboot_erase(0, pages, p->no_stretch);
        if (r == STBOOT_OK) { p->chunk = 0; next(p, S_WRITE); }
        else if (r == STBOOT_BUSY) next(p, S_ERASE_WAIT);
        else return retry(p, UPDATE_FAIL_ERASE);
        return BLADE_UPDATE_BUSY;
    }
    case S_ERASE_WAIT:
        r = stboot_poll();
        if (r == STBOOT_OK) { p->chunk = 0; next(p, S_WRITE); }
        else if (r == STBOOT_BUSY) return wait_more(p, UPDATE_FAIL_ERASE);
        else return fail(p, UPDATE_FAIL_ERASE);
        return BLADE_UPDATE_BUSY;
    case S_WRITE: {
        if (p->chunk >= chunks(p)) {
            p->wrote = true;
            start_verify(p);
            return BLADE_UPDATE_BUSY;
        }
        uint32_t off = write_offset(p, p->chunk);
        uint32_t n = image_bytes(p, off, buf);
        r = stboot_write(BLADE_IMAGE_BASE + off, buf, n, p->no_stretch);
        if (r == STBOOT_OK) { p->chunk++; p->tries = 0; }
        else if (r == STBOOT_BUSY) next(p, S_WRITE_WAIT);
        else if (r == STBOOT_NACK && p->tries) {
            // Flash programs erased words only. A chunk whose acknowledgement
            // was lost the first time is refused the second: read it back,
            // and count it done if it is all there.
            uint8_t back[STBOOT_CHUNK];
            if (stboot_read(BLADE_IMAGE_BASE + off, back, n) == STBOOT_OK && !memcmp(back, buf, n)) {
                p->chunk++;
                p->tries = 0;
            } else {
                return retry(p, UPDATE_FAIL_WRITE);
            }
        } else return retry(p, UPDATE_FAIL_WRITE);
        return BLADE_UPDATE_BUSY;
    }
    case S_WRITE_WAIT:
        r = stboot_poll();
        if (r == STBOOT_OK) { p->chunk++; next(p, S_WRITE); }
        else if (r == STBOOT_BUSY) return wait_more(p, UPDATE_FAIL_WRITE);
        else { p->step = S_WRITE; return retry(p, UPDATE_FAIL_WRITE); } // the same chunk again
        return BLADE_UPDATE_BUSY;
    case S_CRC:
        r = stboot_checksum(BLADE_IMAGE_BASE, p->length);
        if (r == STBOOT_OK) return crc_result(p);
        if (r == STBOOT_BUSY) { next(p, S_CRC_WAIT); return BLADE_UPDATE_BUSY; }
        return retry(p, UPDATE_FAIL_VERIFY);
    case S_CRC_WAIT:
        r = stboot_poll();
        if (r == STBOOT_OK) return crc_result(p);
        if (r == STBOOT_BUSY) return wait_more(p, UPDATE_FAIL_VERIFY);
        p->step = S_CRC;
        return retry(p, UPDATE_FAIL_VERIFY);
    case S_READBACK: { // no checksum command: compare the flash chunk by chunk
        uint8_t want_bytes[STBOOT_CHUNK];
        uint32_t off = p->chunk * STBOOT_CHUNK;
        if (off >= p->length) { next(p, S_GO); return BLADE_UPDATE_BUSY; }
        uint32_t n = image_bytes(p, off, want_bytes);
        if (stboot_read(BLADE_IMAGE_BASE + off, buf, n) != STBOOT_OK) return retry(p, UPDATE_FAIL_VERIFY);
        if (memcmp(buf, want_bytes, n) != 0) return verify_failed(p);
        p->chunk++;
        p->tries = 0;
        return BLADE_UPDATE_BUSY;
    }
    case S_GO:
        if (stboot_go(BLADE_IMAGE_BASE) != STBOOT_OK) return retry(p, UPDATE_FAIL_SILENT);
        return BLADE_UPDATE_STARTED;
    default:
        return fail(p, UPDATE_FAIL_SILENT);
    }
}

uint8_t blade_update_pct(uint8_t port) {
    const update_t *p = &u[port];
    switch (p->step) {
    case S_WRITE:
    case S_WRITE_WAIT: {
        uint32_t total = chunks(p);
        return (uint8_t)(total ? (p->chunk * 100) / total : 0);
    }
    case S_CRC:
    case S_CRC_WAIT:
    case S_READBACK:
    case S_GO:
        return 100;
    default:
        return 0;
    }
}

uint8_t blade_update_fail(uint8_t port) { return u[port].fail; }
bool blade_update_wrote(uint8_t port) { return u[port].wrote; }
uint32_t blade_update_version(uint8_t port) { return u[port].version; }
