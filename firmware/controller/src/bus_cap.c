#include "bus_cap.h"

static bool on;
static bool first;          // true until the first reading has been looked at
static uint32_t held_since; // while on: when the bus last reached BUS_CAP_OFF_MV, 0 if it is below

void bus_cap_init(void) {
    on = false;
    first = true;
    held_since = 0;
}

bool bus_cap_poll(uint32_t now_ms, uint32_t vin_mv) {
    if (!vin_mv) return false;
    uint32_t on_mv = first ? BUS_CAP_OFF_MV : BUS_CAP_ON_MV;
    first = false;

    if (!on) {
        if (vin_mv >= on_mv) return false;
        on = true;
        held_since = 0;
        return true;
    }
    if (vin_mv < BUS_CAP_OFF_MV) {
        held_since = 0; // a dip below 20.5 V restarts the 30 s
        return false;
    }
    if (!held_since) {
        held_since = now_ms ? now_ms : 1;
        return false;
    }
    if (now_ms - held_since < BUS_CAP_HOLD_MS) return false;
    on = false;
    return true;
}

bool bus_cap_on(void) { return on; }

uint32_t bus_cap_ma(void) { return on ? BUS_CAP_MA : 0; }
