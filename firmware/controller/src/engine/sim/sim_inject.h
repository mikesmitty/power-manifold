#pragma once

#include <stdbool.h>
#include <stdint.h>

// Fault injection for FAKE_BLADES builds: the console drives the simulated
// backplane through CMD_SIM (manifold.h), one packed argument per request,
// so faults, detaches and probe failures can be exercised on a bare board
// exactly the way the host tests do. The engine unpacks and applies it.

enum {
    SIM_SEAT,      // blade present in the slot
    SIM_UNSEAT,    // blade pulled
    SIM_ATTACH,    // sink plugs in: mv, ma
    SIM_DETACH,    // sink unplugs
    SIM_LOAD,      // measured draw as % of the contract current: value
    SIM_OCP,       // INA226 over-current trip (latched alert)
    SIM_MPQ_FAULT, // MPQ4242 fault bits, sticky until cleared: value (0 clears)
    SIM_PROBE,     // next probes: value 0 = succeed, 1 = INA226 silent, 2 = MPQ4242 silent
    SIM_MUX,       // value 1: tca9548a_select fails until the engine resets it
    SIM_EXPANDER,  // value 1: tca9539 I/O fails until the engine resets it
    SIM_SCENARIO,  // value 1: run the demo script, 0: pause it (engine.c handles this one)
};

// arg = op << 28 | (mv / 10) << 16 | value  (value doubles as mA for SIM_ATTACH)
uint32_t sim_inject_pack(unsigned op, unsigned mv, unsigned value);
unsigned sim_inject_op(uint32_t arg);
unsigned sim_inject_mv(uint32_t arg);
unsigned sim_inject_value(uint32_t arg);

// Apply one request to the simulated blades (engine core). SIM_SCENARIO is
// not a blade action and is ignored here.
void sim_inject(uint8_t port, uint32_t arg);
