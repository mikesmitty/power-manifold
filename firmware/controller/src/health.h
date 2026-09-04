#pragma once

#include <stddef.h>

#include "manifold.h"

// Chassis-level "needs attention" aggregate for Home Assistant's problem
// binary sensor, the status JSON, the page and `info`: any port in FAULT,
// the engine stalled, a trial firmware image not yet committed, or the
// wired link down while WiFi carries the traffic. Core 0. Writes a short
// description ("faults: Port 2, Desk; trial firmware uncommitted", empty
// when healthy) and returns the number of problems found.
unsigned health_problems(const telemetry_t *t, char *buf, size_t cap);
