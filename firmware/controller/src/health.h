#pragma once

#include <stddef.h>

#include "manifold.h"

// Chassis-level "needs attention" aggregate for Home Assistant's problem
// binary sensor, the status JSON, the page and `info`: any port in FAULT,
// a blade update waiting on a port in use, the engine stalled, a trial firmware image not yet committed, or the
// wired link down while WiFi carries the traffic, the UPS running on its
// battery or reporting a battery fault, HTTPS on without a certificate or
// with one about to expire. Core 0. Writes a short
// description ("faults: Port 2, Desk; trial firmware uncommitted", empty
// when healthy) and returns the number of problems found.
unsigned health_problems(const telemetry_t *t, char *buf, size_t cap);

// The ports whose blade has a firmware update waiting until the port is
// empty or its device switched off (drawing under the charged threshold), by
// name ("Port 3, Desk"; empty when none).
// Returns how many there are.
unsigned health_blades_waiting(const telemetry_t *t, char *buf, size_t cap);

// How the engine started, for the log and `info`: "cold start (expander
// reset)", or "warm start, ports 1 and 3 kept powered". Returns the length.
unsigned health_start_text(const telemetry_t *t, char *buf, size_t cap);
