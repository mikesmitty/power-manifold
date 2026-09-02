#pragma once

#include <stdint.h>

// HardFault trap for both cores. The SDK default is a breakpoint, which with
// no debugger attached escalates to a locked-up core that a probe can no
// longer even read. This handler records the stacked PC/LR and the fault
// status registers per core, then parks the core in WFI: core 0's console
// can report a core 1 fault, and the watchdog still reboots a core 0 fault.
typedef struct {
    volatile uint32_t hit; // 0 = no fault recorded
    volatile uint32_t pc, lr, xpsr, cfsr, hfsr, bfar;
} fault_record_t;

extern fault_record_t g_fault[2];
