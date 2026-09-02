#pragma once

#include <stdint.h>

// Stack high-water marks. Each core paints its unused stack with a pattern
// early in its life; stack_probe_free_min() then reports how much of that
// paint is still intact, i.e. the worst-case headroom seen since boot. Core 0
// lives at the top of main SRAM (memmap_bigstack.ld); core 1 in the 4KB
// SCRATCH_X bank. Neither overflow faults loudly — core 0 would eat the heap
// top, core 1 the .scratch_x region — so measure rather than hope.
void     stack_probe_paint(void);          // run on the core being measured
uint32_t stack_probe_size(int core);
uint32_t stack_probe_free_min(int core);   // 0 means the bottom was reached
