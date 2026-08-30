#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "manifold.h"

// Chassis power accounting. Contract wattage is reserved in full when granted
// (spec §6.3); measured draw is telemetry, not accounting.

#define BUDGET_BASE_RESERVE_MW 15000 // 5V @ 3A held by every powered port

void budget_init(uint32_t total_mw);
void budget_set_total(uint32_t total_mw);
uint32_t budget_total(void);
uint32_t budget_reserved(void);
uint32_t budget_headroom(void);

// Try to change port's reservation to mw; false (and unchanged) if it would
// exceed the chassis budget.
bool budget_try_reserve(uint8_t port, uint32_t mw);
// Unconditionally set port's reservation (base allocation, throttled grants).
void budget_force_reserve(uint8_t port, uint32_t mw);
void budget_release(uint8_t port);
uint32_t budget_port_reservation(uint8_t port);
