#include "fake_stack.h"

#include <stdint.h>

#include "console.h"
#include "power.h"
#include "stack.h"

static bool armed;
static unsigned caps, resets;

void fake_stack_reset(void) {
    armed = false;
    caps = resets = 0;
}

unsigned fake_stack_capabilities_sent(void) { return caps; }
unsigned fake_stack_hard_resets(void) { return resets; }

// as usbpd/usbpd_pwr_user.c does, with the stack waiting for a sink
void stack_port_arm(void) {
    armed = true;
    (void)power_cc_mode(TCPP02_LOW_POWER);
}

void stack_port_disarm(void) {
    armed = false;
    power_shutdown();
    (void)power_cc_mode(TCPP02_HIBERNATE);
}

bool stack_port_armed(void) { return armed; }
void stack_send_capabilities(void) { caps++; }
void stack_hard_reset(void) { resets++; }

void console_str(const char *s) { (void)s; }
void console_dec(int32_t v) { (void)v; }
void console_tenths(int32_t v) { (void)v; }
void console_hex(uint32_t v, unsigned digits) {
    (void)v;
    (void)digits;
}
