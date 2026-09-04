#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Why the last boot happened, decoded from what the chip and our own reboot
// paths leave behind. Hardware-free: main() feeds it the raw registers.
//
// Deliberate reboots (console/web reboot, OTA, trial revert) and HardFaults
// stamp a mark into watchdog scratch[0..3] just before the reset; the SDK
// uses scratch[4..7]. Every boot then leaves a BOOT_RESET sentinel there,
// because (measured on an RP2350 A2) a Cortex-M SYSRESETREQ, the reset a
// debugger or a bare software reset issues, is not recorded in POWMAN's
// CHIP_RESET at all: the previous cause stays latched, while the scratch
// registers survive. Resets POWMAN does record (power-on, brown-out, RUN
// pin, debug-port request, rescue, glitch) also wipe the scratch registers,
// so a surviving sentinel with no other mark means an unrecorded warm reset.

typedef enum {
    BOOT_UNKNOWN = 0,
    BOOT_POWER_ON,
    BOOT_BROWNOUT,
    BOOT_RUN_PIN,      // RUN pin pulled low
    BOOT_DEBUGGER,     // debug port reset request, or rescue
    BOOT_GLITCH,       // glitch detector fired
    BOOT_WATCHDOG,     // the armed watchdog timed out (a core stalled)
    BOOT_REQUESTED,    // console/web reboot
    BOOT_UPDATE,       // OTA flash-update reboot into the new slot
    BOOT_TRIAL_REVERT, // trial image never became healthy
    BOOT_HARDFAULT,    // a core faulted, the watchdog followed
    BOOT_RESET,        // warm reset POWMAN did not record (debugger SYSRESETREQ, software)
    BOOT_REASON_COUNT
} boot_reason_t;

typedef struct {
    boot_reason_t reason;
    uint8_t  core;          // BOOT_HARDFAULT: which core
    uint32_t pc, lr, cfsr;  // BOOT_HARDFAULT: from the fault frame
} boot_cause_t;

// scratch[0] layout: 0x504D ("PM") in the top half, core in bits 8-15,
// boot_reason_t in the low byte; scratch[1..3] = pc, lr, cfsr. The boot-time
// sentinel is BOOT_MARK(BOOT_RESET, 0) with the others zero.
#define BOOT_MARK_MAGIC   0x504D0000u
#define BOOT_MARK_MASK    0xFFFF0000u
#define BOOT_MARK(reason, core) (BOOT_MARK_MAGIC | ((uint32_t)(core) << 8) | (uint32_t)(reason))

// POWMAN CHIP_RESET "HAD_*" bits (RP2350 datasheet); asserted against the SDK
// values where the hardware is present.
#define BOOT_CR_POR             (1u << 16)
#define BOOT_CR_BOR             (1u << 17)
#define BOOT_CR_RUN_LOW         (1u << 18)
#define BOOT_CR_DP_RESET_REQ    (1u << 19)
#define BOOT_CR_RESCUE          (1u << 21)
#define BOOT_CR_WATCHDOG_ANY    0x11C00000u // RSM (28), SWCORE (24), POWMAN (23), POWMAN_ASYNC (22)
#define BOOT_CR_GLITCH          (1u << 26)
#define BOOT_CR_HZD_SYS_RESET   (1u << 27)

// chip_reset = POWMAN CHIP_RESET; wd_reason = watchdog REASON register
// (non-zero after any watchdog-driven reset); wd_timed_out = the SDK's
// watchdog_enable_caused_reboot(); scratch = watchdog scratch[0..3].
// Precedence: explicit mark, watchdog, sentinel, CHIP_RESET bits.
void boot_reason_decode(uint32_t chip_reset, uint32_t wd_reason, bool wd_timed_out,
                        const uint32_t scratch[4], boot_cause_t *out);

const char *boot_reason_name(boot_reason_t r); // short, stable token
// "watchdog timeout", "hardfault core 1 pc=... lr=... cfsr=..."; bytes written
size_t boot_reason_text(const boot_cause_t *b, char *buf, size_t cap);
