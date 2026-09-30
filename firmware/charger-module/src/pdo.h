#pragma once

#include <stdbool.h>
#include <stdint.h>

// Source capabilities and request checking, hardware-free: the objects as
// they travel in Source_Capabilities and Request messages, whichever PD
// stack carries them.
//
// The table is the gen-2 blade's:
//   Fixed 5 V, 9 V, 12 V, 15 V, 20 V
//   PPS 3.3 V - 11.0 V
//   PPS 3.3 V - 21.0 V
// Objects ruled out by the voltage cap are left out, so positions are those
// of the message sent, not of this list.

#define PDO_MAX_OBJECTS 7

typedef struct {
    uint16_t max_ma;    // the controller's current ceiling (BLADE_REG_MAX_MA)
    uint16_t max_mv;    // the controller's voltage cap (BLADE_REG_MAX_MV)
    uint16_t cable_ma;  // what the cable carries: 3000, or 5000 by its e-marker
} pdo_limits_t;

typedef struct {
    uint32_t obj[PDO_MAX_OBJECTS];
    uint8_t  count;
} pdo_table_t;

typedef struct {
    uint8_t  position;  // 1-based object position
    bool     pps;
    bool     mismatch;  // the sink flagged Capability Mismatch
    uint16_t mv;        // voltage to deliver
    uint16_t op_ma;     // operating current the sink asked for
    uint16_t limit_ma;  // fixed: the object's advertised current; PPS: the requested limit
} pdo_request_t;

// Every object's current is the lowest of the ceiling, the cable, and what
// keeps the object within PORT_POWER_MAX_MW at the top of its range, rounded
// down to the object's step. A ceiling of 0 builds an empty table.
void pdo_build(const pdo_limits_t *lim, pdo_table_t *out);

// A Request data object against the table last sent. False = reject.
bool pdo_check_request(const pdo_table_t *t, uint32_t rdo, pdo_request_t *out);

// Field access, also for the tests
bool     pdo_is_pps(uint32_t pdo);
uint32_t pdo_fixed_mv(uint32_t pdo);
uint32_t pdo_fixed_ma(uint32_t pdo);
uint32_t pdo_pps_min_mv(uint32_t pdo);
uint32_t pdo_pps_max_mv(uint32_t pdo);
uint32_t pdo_pps_ma(uint32_t pdo);
