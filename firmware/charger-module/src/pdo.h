#pragma once

#include <stdbool.h>
#include <stdint.h>

// Source capabilities and request checking, hardware-free: the objects as
// they travel in Source_Capabilities and Request messages, whichever PD
// stack carries them.
//
// The table, in the order PD 3.2 sends it:
//   Fixed 5 V, 9 V, 12 V, 15 V, 20 V
//   SPR AVS 9 V - 20 V
//   PPS 5 V - 21 V
// The port's power can be turned down by the controller, which makes it a
// Managed Capability Port in PD 3.2's terms (section 3.2.3): what it offers
// follows the power it may deliver at the moment, its Port Present PDP.
// That is the controller's power limit, and each object's current follows
// from it. Objects ruled out by the voltage cap are left out, so positions
// are those of the message sent, not of this list.

#define PDO_MAX_OBJECTS 7

typedef struct {
    uint8_t  max_w;     // the controller's power limit (BLADE_REG_MAX_W)
    uint16_t max_mv;    // the controller's voltage cap (BLADE_REG_MAX_MV)
    uint16_t cable_ma;  // what the cable carries: 3000, or 5000 by its e-marker
} pdo_limits_t;

typedef struct {
    uint32_t obj[PDO_MAX_OBJECTS];
    uint8_t  count;
    uint32_t power_mw;  // the power limit the table was built for
} pdo_table_t;

typedef enum {
    PDO_FIXED,
    PDO_PPS,
    PDO_AVS,
} pdo_kind_t;

typedef struct {
    uint8_t    position;  // 1-based object position
    pdo_kind_t kind;
    bool       mismatch;  // the sink flagged Capability Mismatch
    uint16_t   mv;        // voltage to deliver
    uint16_t   op_ma;     // operating current the sink asked for
    // Fixed and AVS: the current the object advertises at this voltage,
    // which the converter's limit goes above with a margin. PPS: the
    // current to regulate to, which is what the sink asked for, held to the
    // power limit at this voltage.
    uint16_t   limit_ma;
} pdo_request_t;

// Every fixed object's current is the lowest of 5 A, the cable, and the
// power limit divided by its voltage, rounded down to its 10 mA step. A
// power limit of 0 builds an empty table.
void pdo_build(const pdo_limits_t *lim, pdo_table_t *out);

// A Request data object against the table last sent. False = reject.
bool pdo_check_request(const pdo_table_t *t, uint32_t rdo, pdo_request_t *out);

// The power the table offers, in whole watts: the largest of its fixed
// objects' voltage times current, which Source_Info calls Port Reported PDP.
uint32_t pdo_reported_w(const pdo_table_t *t);

// Field access, also for the tests
bool     pdo_is_apdo(uint32_t pdo); // PPS or AVS: neither exists for a PD 2.0 sink
bool     pdo_is_pps(uint32_t pdo);
bool     pdo_is_avs(uint32_t pdo);
uint32_t pdo_fixed_mv(uint32_t pdo);
uint32_t pdo_fixed_ma(uint32_t pdo);
uint32_t pdo_pps_min_mv(uint32_t pdo);
uint32_t pdo_pps_max_mv(uint32_t pdo);
uint32_t pdo_pps_ma(uint32_t pdo);
bool     pdo_pps_power_limited(uint32_t pdo);
uint32_t pdo_avs_max_mv(uint32_t pdo);
uint32_t pdo_avs_ma_15v(uint32_t pdo); // from 9 V to 15 V
uint32_t pdo_avs_ma_20v(uint32_t pdo); // from 15 V to 20 V, 0 when only 15 V is offered
