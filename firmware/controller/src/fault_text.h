#pragma once

#include <stddef.h>

#include "fault_log.h"

// One-line human description of a fault-log record for the console, the
// JSON API and Home Assistant attributes: "otw1+cc+ocp", "conv-ocp+plug-hot"
// (a gen-3 blade's own words), "probe: ina226", "boot: watchdog timeout",
// "bus low: 18.70 V".
// Hardware-free. Returns the bytes written.
size_t fault_text(const fault_rec_t *r, char *buf, size_t cap);
