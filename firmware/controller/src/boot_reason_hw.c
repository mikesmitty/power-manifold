#include "boot_reason_hw.h"

#include "hardware/regs/powman.h"
#include "hardware/structs/powman.h"
#include "hardware/structs/watchdog.h"
#include "hardware/watchdog.h"

_Static_assert(BOOT_CR_POR == POWMAN_CHIP_RESET_HAD_POR_BITS, "CHIP_RESET bit moved");
_Static_assert(BOOT_CR_BOR == POWMAN_CHIP_RESET_HAD_BOR_BITS, "CHIP_RESET bit moved");
_Static_assert(BOOT_CR_RUN_LOW == POWMAN_CHIP_RESET_HAD_RUN_LOW_BITS, "CHIP_RESET bit moved");
_Static_assert(BOOT_CR_DP_RESET_REQ == POWMAN_CHIP_RESET_HAD_DP_RESET_REQ_BITS, "CHIP_RESET bit moved");
_Static_assert(BOOT_CR_RESCUE == POWMAN_CHIP_RESET_HAD_RESCUE_BITS, "CHIP_RESET bit moved");
_Static_assert(BOOT_CR_GLITCH == POWMAN_CHIP_RESET_HAD_GLITCH_DETECT_BITS, "CHIP_RESET bit moved");
_Static_assert(BOOT_CR_HZD_SYS_RESET == POWMAN_CHIP_RESET_HAD_HZD_SYS_RESET_REQ_BITS, "CHIP_RESET bit moved");
_Static_assert(BOOT_CR_WATCHDOG_ANY == (POWMAN_CHIP_RESET_HAD_WATCHDOG_RESET_RSM_BITS |
                                        POWMAN_CHIP_RESET_HAD_WATCHDOG_RESET_SWCORE_BITS |
                                        POWMAN_CHIP_RESET_HAD_WATCHDOG_RESET_POWMAN_BITS |
                                        POWMAN_CHIP_RESET_HAD_WATCHDOG_RESET_POWMAN_ASYNC_BITS),
               "CHIP_RESET watchdog bits moved");

static boot_cause_t last;

void boot_reason_read(void) {
    uint32_t scratch[4];
    for (int i = 0; i < 4; i++) scratch[i] = watchdog_hw->scratch[i];
    boot_reason_decode(powman_hw->chip_reset, watchdog_hw->reason,
                       watchdog_enable_caused_reboot(), scratch, &last);
    // leave the sentinel: a reset that keeps scratch but sets no mark and no
    // watchdog reason is then reported as a warm reset, not a stale POWMAN cause
    for (int i = 1; i < 4; i++) watchdog_hw->scratch[i] = 0;
    watchdog_hw->scratch[0] = BOOT_MARK(BOOT_RESET, 0);
}

const boot_cause_t *boot_reason_last(void) {
    return &last;
}

void boot_reason_mark(boot_reason_t r, unsigned core, uint32_t pc, uint32_t lr, uint32_t cfsr) {
    watchdog_hw->scratch[1] = pc;
    watchdog_hw->scratch[2] = lr;
    watchdog_hw->scratch[3] = cfsr;
    watchdog_hw->scratch[0] = BOOT_MARK(r, core); // last: the mark validates the rest
}
