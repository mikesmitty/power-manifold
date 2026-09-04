#pragma once

#include <stddef.h>

#include "fault_log.h"

// One-line human description of a fault-log record for the console, the
// JSON API and Home Assistant attributes: "otw1+cc +ocp", "probe: ina226",
// "boot: watchdog timeout". Hardware-free. Returns the bytes written.
size_t fault_text(const fault_rec_t *r, char *buf, size_t cap);
