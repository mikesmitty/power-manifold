#include "fake_bus.h"

#include <string.h>

#include "board.h"
#include "bus.h"
#include "tcpp02.h"

typedef struct {
    bool    present;
    uint8_t reg[256];
} dev_t;

static dev_t tps, tcpp;
static bool ack_stuck;
static void (*observer)(void);
static fake_bus_write_t log_[FAKE_BUS_LOG];
static unsigned log_n;

static dev_t *dev(uint8_t addr) {
    if (addr == ADDR_TPS55288) return &tps;
    if (addr == ADDR_TCPP02) return &tcpp;
    return NULL;
}

void fake_bus_reset(void) {
    memset(&tps, 0, sizeof tps);
    memset(&tcpp, 0, sizeof tcpp);
    tps.present = tcpp.present = true;
    // reset values, SLVSF01B table 7-12
    static const uint8_t tps_defaults[8] = {0xD2, 0x00, 0xE4, 0x01, 0x03, 0xE0, 0x20, 0x03};
    memcpy(tps.reg, tps_defaults, sizeof tps_defaults);
    tcpp.reg[TCPP02_REG_FLAGS] = TCPP02_FLAG_IS_TCPP02;
    ack_stuck = false;
    observer = NULL;
    log_n = 0;
}

void fake_bus_set_present(uint8_t addr, bool present) { dev(addr)->present = present; }
void fake_bus_poke(uint8_t addr, uint8_t reg, uint8_t val) { dev(addr)->reg[reg] = val; }
uint8_t fake_bus_peek(uint8_t addr, uint8_t reg) { return dev(addr)->reg[reg]; }
void fake_bus_stick_tcpp02_ack(bool stuck) { ack_stuck = stuck; }
void fake_bus_on_write(void (*cb)(void)) { observer = cb; }
unsigned fake_bus_write_count(void) { return log_n; }
const fake_bus_write_t *fake_bus_write_at(unsigned i) { return &log_[i]; }

bool bus_probe(uint8_t addr) {
    dev_t *d = dev(addr);
    return d && d->present;
}

bool bus_write(uint8_t addr, const uint8_t *buf, size_t len) {
    dev_t *d = dev(addr);
    if (!d || !d->present || !len) return false;
    if (log_n < FAKE_BUS_LOG) {
        fake_bus_write_t *w = &log_[log_n++];
        w->addr = addr;
        w->len = (uint8_t)len;
        memcpy(w->data, buf, len < sizeof w->data ? len : sizeof w->data);
    }
    uint8_t reg = buf[0];
    for (size_t i = 1; i < len; i++, reg++) {
        d->reg[reg] = buf[i];
        if (d == &tcpp && reg == TCPP02_REG_CTRL && !ack_stuck)
            d->reg[TCPP02_REG_ACK] = buf[i];
    }
    if (observer) observer();
    return true;
}

bool bus_read_reg(uint8_t addr, uint8_t reg, uint8_t *buf, size_t len) {
    dev_t *d = dev(addr);
    if (!d || !d->present) return false;
    for (size_t i = 0; i < len; i++) buf[i] = d->reg[(uint8_t)(reg + i)];
    return true;
}
