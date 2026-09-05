#include "sim_inject.h"

#include "manifold.h"
#include "sim_blades.h"

uint32_t sim_inject_pack(unsigned op, unsigned mv, unsigned value) {
    return (uint32_t)(op & 0xF) << 28 | (uint32_t)((mv / 10) & 0xFFF) << 16 | (value & 0xFFFF);
}

unsigned sim_inject_op(uint32_t arg) {
    return arg >> 28;
}

unsigned sim_inject_mv(uint32_t arg) {
    return ((arg >> 16) & 0xFFF) * 10;
}

unsigned sim_inject_value(uint32_t arg) {
    return arg & 0xFFFF;
}

void sim_inject(uint8_t port, uint32_t arg) {
    unsigned v = sim_inject_value(arg);
    switch (sim_inject_op(arg)) {
    case SIM_MUX:      sim_set_mux_fail(v != 0); return;
    case SIM_EXPANDER: sim_set_expander_fail(v != 0); return;
    default: break;
    }
    if (port >= NUM_PORTS) return;
    switch (sim_inject_op(arg)) {
    case SIM_SEAT:      sim_set_present(port, true); break;
    case SIM_UNSEAT:    sim_set_present(port, false); break;
    case SIM_ATTACH:    sim_attach(port, (uint16_t)sim_inject_mv(arg), v); break;
    case SIM_DETACH:    sim_detach(port); break;
    case SIM_LOAD:      sim_set_load_pct(port, (uint8_t)(v > 100 ? 100 : v)); break;
    case SIM_OCP:       sim_trip_ocp(port); break;
    case SIM_MPQ_FAULT: sim_set_mpq_fault(port, (uint8_t)v); break;
    case SIM_PROBE:     sim_set_probe_ok(port, v != 1, v != 2); break;
    default: break;
    }
}
