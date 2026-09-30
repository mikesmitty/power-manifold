#include "tcpp02.h"

#include "board.h"
#include "bus.h"

uint8_t tcpp02_ctrl_byte(const tcpp02_ctrl_t *c) {
    uint8_t b = TCPP02_CTRL_ONE;
    if (c->mode == TCPP02_NORMAL) b |= TCPP02_CTRL_PM1;
    if (c->mode == TCPP02_LOW_POWER) b |= TCPP02_CTRL_PM2;
    if (c->vconn == TCPP02_VCONN_CC1) b |= TCPP02_CTRL_V1;
    if (c->vconn == TCPP02_VCONN_CC2) b |= TCPP02_CTRL_V2;
    if (c->vbus_on) b |= TCPP02_CTRL_GDP;
    if (c->vbus_discharge) b |= TCPP02_CTRL_VBUSD;
    if (c->vconn_discharge) b |= TCPP02_CTRL_VCONND;
    return b;
}

bool tcpp02_probe(void) {
    uint8_t flags;
    return tcpp02_read_flags(&flags) && (flags & TCPP02_FLAG_IS_TCPP02);
}

bool tcpp02_set(const tcpp02_ctrl_t *c) {
    uint8_t buf[2] = {TCPP02_REG_CTRL, tcpp02_ctrl_byte(c)};
    if (!bus_write(ADDR_TCPP02, buf, sizeof buf)) return false;
    uint8_t ack;
    if (!bus_read_reg(ADDR_TCPP02, TCPP02_REG_ACK, &ack, 1)) return false;
    return ack == buf[1];
}

bool tcpp02_read_flags(uint8_t *flags) {
    return bus_read_reg(ADDR_TCPP02, TCPP02_REG_FLAGS, flags, 1);
}
