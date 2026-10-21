#pragma once
// One I2C transaction on the backplane bus, kept whole.
//
// Core 0 parks this core for every flash write it makes (an update image,
// the boot slot's commit, settings, the fault log; flash_safe_execute), at
// whatever point this core has reached, for as long as a sector erase
// takes. The I2C block finishes the byte it is on, but the SDK's transfer
// loop wakes up past its deadline, reports a timeout for a byte that went
// through and walks away from the transfer halfway. On the bench an update
// download timed out about two hundred blade reads that way; now and then
// one left its blade holding SDA (a `probe: bus held` record at the next
// select), and a blade update lost a write to a retry.
//
// A transaction masks the interrupt the park arrives on, so core 0 waits
// until it has ended: at most a stretched bootloader acknowledge
// (STRETCH_TIMEOUT_US in stboot.c, 200 ms), inside the 500 ms each
// flash_safe_execute call allows. The mask is only lifted if it was on
// before, so a nested transaction, or one before the engine has installed
// the park's handler, leaves it as it was.
#include <stdbool.h>

#include "hardware/irq.h"
#include "pico/multicore.h"

static inline bool i2c_txn_begin(void) {
    uint irq = SIO_FIFO_IRQ_NUM(get_core_num());
    bool was_on = irq_is_enabled(irq);
    irq_set_enabled(irq, false);
    return was_on;
}

static inline void i2c_txn_end(bool was_on) {
    if (was_on) irq_set_enabled(SIO_FIFO_IRQ_NUM(get_core_num()), true);
}
