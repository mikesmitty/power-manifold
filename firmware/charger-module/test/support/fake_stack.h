#pragma once

// The PD stack as the supervisor sees it (stack.h), and the console

#include "port.h"

void fake_stack_reset(void);
unsigned fake_stack_capabilities_sent(void);
unsigned fake_stack_hard_resets(void);
unsigned fake_stack_alerts_sent(void); // over-temperature Alerts asked for
port_rp_t fake_stack_rp(void); // the CC line's level, as last set
