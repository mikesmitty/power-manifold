#pragma once

#include <stdbool.h>
#include <stdint.h>

// FAKE_BLADES builds only: drives the simulated blades through a repeating
// demo script so every port state, the budget arbiter, priority shedding,
// fault recovery, and the whole management plane can be exercised on a bare
// board. Called from the engine loop, engine core only.

void sim_scenario_tick(uint32_t now_ms);
// Pause the script so injected state sticks (sim_inject.h); running it again
// restarts from the baseline step.
void sim_scenario_set_running(bool run);
bool sim_scenario_running(void);
