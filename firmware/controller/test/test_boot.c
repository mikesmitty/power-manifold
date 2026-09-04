#include <string.h>

#include "boot_reason.h"
#include "fault_text.h"
#include "microtest.h"

// boot_reason: the precedence of what the chip and our own marks leave behind.

static const uint32_t NO_MARK[4] = {0, 0, 0, 0};

static void test_chip_reset_bits(void) {
    boot_cause_t b;
    boot_reason_decode(BOOT_CR_POR, 0, false, NO_MARK, &b);
    MT_ASSERT_EQ(b.reason, BOOT_POWER_ON);
    boot_reason_decode(BOOT_CR_BOR, 0, false, NO_MARK, &b);
    MT_ASSERT_EQ(b.reason, BOOT_BROWNOUT);
    boot_reason_decode(BOOT_CR_RUN_LOW, 0, false, NO_MARK, &b);
    MT_ASSERT_EQ(b.reason, BOOT_RUN_PIN);
    boot_reason_decode(BOOT_CR_DP_RESET_REQ, 0, false, NO_MARK, &b);
    MT_ASSERT_EQ(b.reason, BOOT_DEBUGGER);
    boot_reason_decode(BOOT_CR_RESCUE, 0, false, NO_MARK, &b);
    MT_ASSERT_EQ(b.reason, BOOT_DEBUGGER);
    boot_reason_decode(BOOT_CR_GLITCH, 0, false, NO_MARK, &b);
    MT_ASSERT_EQ(b.reason, BOOT_GLITCH);
    boot_reason_decode(BOOT_CR_HZD_SYS_RESET, 0, false, NO_MARK, &b);
    MT_ASSERT_EQ(b.reason, BOOT_REQUESTED);
    boot_reason_decode(0, 0, false, NO_MARK, &b);
    MT_ASSERT_EQ(b.reason, BOOT_UNKNOWN);
}

static void test_watchdog(void) {
    boot_cause_t b;
    // the SDK's enable-magic in scratch[4] separates a timeout from watchdog_reboot()
    boot_reason_decode(BOOT_CR_WATCHDOG_ANY, 1, true, NO_MARK, &b);
    MT_ASSERT_EQ(b.reason, BOOT_WATCHDOG);
    boot_reason_decode(BOOT_CR_WATCHDOG_ANY, 1, false, NO_MARK, &b);
    MT_ASSERT_EQ(b.reason, BOOT_REQUESTED);
    // a watchdog reset with no reason register read (chip bits only)
    boot_reason_decode(BOOT_CR_WATCHDOG_ANY, 0, false, NO_MARK, &b);
    MT_ASSERT_EQ(b.reason, BOOT_WATCHDOG);
}

static void test_marks_win(void) {
    boot_cause_t b;
    uint32_t m[4] = {BOOT_MARK(BOOT_UPDATE, 0), 0, 0, 0};
    boot_reason_decode(BOOT_CR_WATCHDOG_ANY, 1, true, m, &b);
    MT_ASSERT_EQ(b.reason, BOOT_UPDATE);

    uint32_t hf[4] = {BOOT_MARK(BOOT_HARDFAULT, 1), 0x10001234, 0x10005678, 0x00008200};
    boot_reason_decode(BOOT_CR_WATCHDOG_ANY, 1, true, hf, &b);
    MT_ASSERT_EQ(b.reason, BOOT_HARDFAULT);
    MT_ASSERT_EQ(b.core, 1);
    MT_ASSERT_EQ(b.pc, 0x10001234u);
    MT_ASSERT_EQ(b.lr, 0x10005678u);
    MT_ASSERT_EQ(b.cfsr, 0x8200u);

    // wrong magic or an out-of-range code: ignored, fall through
    uint32_t bad[4] = {0x504E0000u | BOOT_UPDATE, 0, 0, 0};
    boot_reason_decode(BOOT_CR_POR, 0, false, bad, &b);
    MT_ASSERT_EQ(b.reason, BOOT_POWER_ON);
    uint32_t junk[4] = {BOOT_MARK_MAGIC | 0xEE, 0, 0, 0};
    boot_reason_decode(BOOT_CR_POR, 0, false, junk, &b);
    MT_ASSERT_EQ(b.reason, BOOT_POWER_ON);
}

static void test_sentinel(void) {
    boot_cause_t b;
    uint32_t s[4] = {BOOT_MARK(BOOT_RESET, 0), 0, 0, 0};
    // a debugger SYSRESETREQ leaves the previous POWMAN cause latched
    boot_reason_decode(BOOT_CR_DP_RESET_REQ, 0, false, s, &b);
    MT_ASSERT_EQ(b.reason, BOOT_RESET);
    boot_reason_decode(BOOT_CR_POR, 0, false, s, &b);
    MT_ASSERT_EQ(b.reason, BOOT_RESET);
    // but the watchdog still outranks it, and an explicit mark too
    boot_reason_decode(BOOT_CR_POR, 1, true, s, &b);
    MT_ASSERT_EQ(b.reason, BOOT_WATCHDOG);
    boot_reason_decode(BOOT_CR_POR, 1, false, s, &b);
    MT_ASSERT_EQ(b.reason, BOOT_REQUESTED);
    // resets POWMAN records wipe scratch, so the bits win once it is gone
    boot_reason_decode(BOOT_CR_DP_RESET_REQ, 0, false, NO_MARK, &b);
    MT_ASSERT_EQ(b.reason, BOOT_DEBUGGER);
    MT_ASSERT(!strcmp(boot_reason_name(BOOT_RESET), "reset"));
}

static void test_text(void) {
    char buf[80];
    boot_cause_t b = {.reason = BOOT_WATCHDOG};
    boot_reason_text(&b, buf, sizeof(buf));
    MT_ASSERT(!strcmp(buf, "watchdog timeout"));
    b = (boot_cause_t){.reason = BOOT_HARDFAULT, .core = 1, .pc = 0x10001234, .lr = 0x1000abcd, .cfsr = 0x8200};
    boot_reason_text(&b, buf, sizeof(buf));
    MT_ASSERT(!strcmp(buf, "hardfault core 1 pc=10001234 lr=1000abcd cfsr=00008200"));
    b = (boot_cause_t){.reason = BOOT_POWER_ON};
    MT_ASSERT_EQ(boot_reason_text(&b, buf, sizeof(buf)), 8);
    MT_ASSERT(!strcmp(buf, "power-on"));
    MT_ASSERT(!strcmp(boot_reason_name(BOOT_TRIAL_REVERT), "trial-revert"));
    MT_ASSERT(!strcmp(boot_reason_name((boot_reason_t)99), "unknown"));
    // truncation stays NUL-terminated
    b = (boot_cause_t){.reason = BOOT_HARDFAULT};
    char tiny[10];
    MT_ASSERT_EQ(boot_reason_text(&b, tiny, sizeof(tiny)), 9);
    MT_ASSERT_EQ(strlen(tiny), 9);
}

static void test_fault_text(void) {
    char buf[64];
    fault_rec_t r = {.type = EVT_FAULT, .code = MPQ_FAULT_OTW1 | MPQ_FAULT_CC, .arg = 1};
    fault_text(&r, buf, sizeof(buf));
    MT_ASSERT(!strcmp(buf, "otw1+cc +ocp"));
    r = (fault_rec_t){.type = EVT_FAULT, .code = 0, .arg = 1};
    fault_text(&r, buf, sizeof(buf));
    MT_ASSERT(!strcmp(buf, "ocp"));
    r = (fault_rec_t){.type = EVT_FAULT, .code = MPQ_FAULT_VBATT_LOW, .arg = 0};
    fault_text(&r, buf, sizeof(buf));
    MT_ASSERT(!strcmp(buf, "vbatt-low"));
    r = (fault_rec_t){.type = EVT_FAULT, .code = 0, .arg = 0};
    fault_text(&r, buf, sizeof(buf));
    MT_ASSERT(!strcmp(buf, "none"));
    r = (fault_rec_t){.type = EVT_PROBE_FAIL, .code = 2};
    fault_text(&r, buf, sizeof(buf));
    MT_ASSERT(!strcmp(buf, "probe: ina226"));
    r = (fault_rec_t){.type = EVT_PROBE_FAIL, .code = 9};
    fault_text(&r, buf, sizeof(buf));
    MT_ASSERT(!strcmp(buf, "probe: ?"));
    r = (fault_rec_t){.type = EVT_BOOT, .code = BOOT_HARDFAULT | (1u << 8), .arg = 0x10000010,
                      .power_mw = 0x10000020, .contract_mw = 0x400};
    fault_text(&r, buf, sizeof(buf));
    MT_ASSERT(!strcmp(buf, "boot: hardfault core 1 pc=10000010 lr=10000020 cfsr=00000400"));
    r = (fault_rec_t){.type = EVT_BOOT, .code = BOOT_POWER_ON};
    fault_text(&r, buf, sizeof(buf));
    MT_ASSERT(!strcmp(buf, "boot: power-on"));
    // capped output never overruns
    r = (fault_rec_t){.type = EVT_FAULT, .code = 0xFF, .arg = 1};
    char small[12];
    fault_text(&r, small, sizeof(small));
    MT_ASSERT_EQ(strlen(small), 11);
}

void run_boot_tests(void) {
    mt_run("boot: CHIP_RESET bits", test_chip_reset_bits);
    mt_run("boot: watchdog timeout vs requested", test_watchdog);
    mt_run("boot: scratch marks take precedence", test_marks_win);
    mt_run("boot: warm-reset sentinel", test_sentinel);
    mt_run("boot: reason text", test_text);
    mt_run("fault_text: fault, probe and boot records", test_fault_text);
}
