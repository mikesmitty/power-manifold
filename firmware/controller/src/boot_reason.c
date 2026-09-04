#include "boot_reason.h"

#include <stdio.h>
#include <string.h>

void boot_reason_decode(uint32_t chip_reset, uint32_t wd_reason, bool wd_timed_out,
                        const uint32_t scratch[4], boot_cause_t *out) {
    memset(out, 0, sizeof(*out));

    bool sentinel = scratch[0] == BOOT_MARK(BOOT_RESET, 0);
    if (!sentinel && (scratch[0] & BOOT_MARK_MASK) == BOOT_MARK_MAGIC) {
        uint32_t reason = scratch[0] & 0xFFu;
        if (reason < BOOT_REASON_COUNT && reason != BOOT_RESET) {
            out->reason = (boot_reason_t)reason;
            out->core = (uint8_t)((scratch[0] >> 8) & 0xFFu);
            out->pc = scratch[1];
            out->lr = scratch[2];
            out->cfsr = scratch[3];
            return;
        }
    }
    if (wd_reason) {
        // watchdog_reboot() without one of our marks counts as requested
        out->reason = wd_timed_out ? BOOT_WATCHDOG : BOOT_REQUESTED;
        return;
    }
    if (sentinel) { // scratch intact, nothing else claimed it: unrecorded warm reset
        out->reason = BOOT_RESET;
        return;
    }
    if (chip_reset & BOOT_CR_POR)                out->reason = BOOT_POWER_ON;
    else if (chip_reset & BOOT_CR_BOR)           out->reason = BOOT_BROWNOUT;
    else if (chip_reset & BOOT_CR_RUN_LOW)       out->reason = BOOT_RUN_PIN;
    else if (chip_reset & (BOOT_CR_DP_RESET_REQ | BOOT_CR_RESCUE)) out->reason = BOOT_DEBUGGER;
    else if (chip_reset & BOOT_CR_GLITCH)        out->reason = BOOT_GLITCH;
    else if (chip_reset & BOOT_CR_WATCHDOG_ANY)  out->reason = BOOT_WATCHDOG;
    else if (chip_reset & BOOT_CR_HZD_SYS_RESET) out->reason = BOOT_REQUESTED;
    else                                         out->reason = BOOT_UNKNOWN;
}

const char *boot_reason_name(boot_reason_t r) {
    switch (r) {
    case BOOT_POWER_ON:     return "power-on";
    case BOOT_BROWNOUT:     return "brown-out";
    case BOOT_RUN_PIN:      return "run-pin";
    case BOOT_DEBUGGER:     return "debugger";
    case BOOT_GLITCH:       return "glitch";
    case BOOT_WATCHDOG:     return "watchdog";
    case BOOT_REQUESTED:    return "requested";
    case BOOT_UPDATE:       return "update";
    case BOOT_TRIAL_REVERT: return "trial-revert";
    case BOOT_HARDFAULT:    return "hardfault";
    case BOOT_RESET:        return "reset";
    default:                return "unknown";
    }
}

size_t boot_reason_text(const boot_cause_t *b, char *buf, size_t cap) {
    int n;
    switch (b->reason) {
    case BOOT_WATCHDOG:
        n = snprintf(buf, cap, "watchdog timeout");
        break;
    case BOOT_HARDFAULT:
        n = snprintf(buf, cap, "hardfault core %u pc=%08lx lr=%08lx cfsr=%08lx", b->core,
                     (unsigned long)b->pc, (unsigned long)b->lr, (unsigned long)b->cfsr);
        break;
    case BOOT_TRIAL_REVERT:
        n = snprintf(buf, cap, "trial image reverted");
        break;
    case BOOT_UPDATE:
        n = snprintf(buf, cap, "firmware update");
        break;
    case BOOT_RESET:
        n = snprintf(buf, cap, "warm reset (debugger or software)");
        break;
    default:
        n = snprintf(buf, cap, "%s", boot_reason_name(b->reason));
        break;
    }
    if (n < 0) return 0;
    return (size_t)n < cap ? (size_t)n : cap - 1;
}
