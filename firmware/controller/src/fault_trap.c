#include "fault_trap.h"

#include "hardware/sync.h"
#include "pico.h"

#include "boot_reason_hw.h"

fault_record_t g_fault[2];

#define SCB_CFSR (*(volatile uint32_t *)0xE000ED28u)
#define SCB_HFSR (*(volatile uint32_t *)0xE000ED2Cu)
#define SCB_BFAR (*(volatile uint32_t *)0xE000ED38u)

void __attribute__((used)) hardfault_record(uint32_t *frame) {
    fault_record_t *f = &g_fault[get_core_num()];
    f->lr = frame[5];
    f->pc = frame[6];
    f->xpsr = frame[7];
    f->cfsr = SCB_CFSR;
    f->hfsr = SCB_HFSR;
    f->bfar = SCB_BFAR;
    f->hit = 1;
    // survives the watchdog reboot that follows a stalled core: the next boot
    // reports it as its reason and logs it
    boot_reason_mark(BOOT_HARDFAULT, get_core_num(), f->pc, f->lr, f->cfsr);
    for (;;) __wfi();
}

// EXC_RETURN bit 2 says which stack holds the exception frame
void __attribute__((naked)) isr_hardfault(void) {
    __asm volatile(
        "tst lr, #4\n"
        "ite eq\n"
        "mrseq r0, msp\n"
        "mrsne r0, psp\n"
        "b hardfault_record\n");
}
