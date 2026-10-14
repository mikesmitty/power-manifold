#pragma once

#include <stdbool.h>

#include "port.h"

// What the supervisor asks of the PD stack and its power glue, free of the
// stack's own headers (implemented in usbpd/, stood in for by the host
// tests).

// The port can be held dark: until it is armed, everything that would make
// it visible or put power on it is refused, and the TCPP02 stays in
// hibernate with its CC switches open, so a sink sees no source at all.
void stack_port_arm(void);    // what the stack wants of the TCPP02 now reaches it
void stack_port_disarm(void); // everything off, the TCPP02 back in hibernate
bool stack_port_armed(void);

void stack_send_capabilities(void); // the table changed under an attached sink
// One Alert message with the over-temperature bit, sent once the engine is
// free. Only a sink in a PD 3 contract can receive one; otherwise it is dropped.
void stack_send_alert_otp(void);
void stack_hard_reset(void);
// The Type-C current the CC line advertises (port_rp): to the next sink at
// once, and to an attached one while it has no contract. In a PD 3
// contract the policy engine moves the level itself for collision
// avoidance, and this one returns once the contract is gone.
void stack_set_rp(port_rp_t level);
