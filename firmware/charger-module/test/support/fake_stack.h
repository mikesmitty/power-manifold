#pragma once

// The PD stack as the supervisor sees it (stack.h), and the console

void fake_stack_reset(void);
unsigned fake_stack_capabilities_sent(void);
unsigned fake_stack_hard_resets(void);
unsigned fake_stack_alerts_sent(void); // over-temperature Alerts asked for
