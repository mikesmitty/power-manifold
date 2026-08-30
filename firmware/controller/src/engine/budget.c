#include "budget.h"

static uint32_t total;
static uint32_t reserved[NUM_PORTS];

void budget_init(uint32_t total_mw) {
    total = total_mw;
    for (int i = 0; i < NUM_PORTS; i++) reserved[i] = 0;
}

void budget_set_total(uint32_t total_mw) {
    total = total_mw;
}

uint32_t budget_total(void) {
    return total;
}

uint32_t budget_reserved(void) {
    uint32_t sum = 0;
    for (int i = 0; i < NUM_PORTS; i++) sum += reserved[i];
    return sum;
}

uint32_t budget_headroom(void) {
    uint32_t sum = budget_reserved();
    return sum >= total ? 0 : total - sum;
}

bool budget_try_reserve(uint8_t port, uint32_t mw) {
    uint32_t others = budget_reserved() - reserved[port];
    if (others + mw > total) return false;
    reserved[port] = mw;
    return true;
}

void budget_force_reserve(uint8_t port, uint32_t mw) {
    reserved[port] = mw;
}

void budget_release(uint8_t port) {
    reserved[port] = 0;
}

uint32_t budget_port_reservation(uint8_t port) {
    return reserved[port];
}
