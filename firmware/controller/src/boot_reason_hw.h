#pragma once

#include "boot_reason.h"

// The hardware half of boot_reason: read the chip's reset state once at boot,
// keep the answer, and stamp marks before deliberate reboots or after a fault.

void boot_reason_read(void);             // first thing in main(); clears the mark
const boot_cause_t *boot_reason_last(void);
void boot_reason_mark(boot_reason_t r, unsigned core, uint32_t pc, uint32_t lr, uint32_t cfsr);
