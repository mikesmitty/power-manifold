#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "pdo.h"
#include "regmap.h"

// What the port offers and what it has agreed to, hardware-free and free of
// the PD stack: the stack's callbacks ask here.
//
// The table follows the controller's limits and the cable. A change to
// either while a sink is attached returns true, and the caller has the
// stack send Source_Capabilities again.

void port_init(void);

bool port_set_limits(uint16_t max_ma, uint16_t max_mv); // true: the table changed
bool port_set_cable_ma(uint16_t ma);                    // true: the table changed
const pdo_table_t *port_table(void);

// A sink without a contract goes by the Type-C current its CC line
// advertises: the highest level within the controller's limit. The
// converter holds it to the same current (power_set_typec_ma).
typedef enum {
    PORT_RP_DEFAULT, // Default USB power: 500 mA, or 900 mA for USB 3
    PORT_RP_1A5,
    PORT_RP_3A0,
} port_rp_t;
port_rp_t port_rp(void);
uint16_t  port_typec_ma(void); // the controller's limit, at most 3 A

void port_attach(void);
void port_detach(void);        // the cable goes with the sink: back to CABLE_DEFAULT_MA
bool port_attached(void);

// A Request against the table. Accepted ones are held until the power path
// has delivered them; then they are the contract.
bool port_request(uint32_t rdo);
const pdo_request_t *port_pending(void);  // NULL: none
void port_delivered(void);
const pdo_request_t *port_contract(void); // NULL: none
void port_contract_lost(void);            // hard reset: back to the implicit 5 V
void port_hard_reset(bool by_sink);       // counts one for BLADE_REG_HR_SENT / HR_RECEIVED

// BLADE_ST_ATTACHED / CONTRACT / PPS / CABLE_5A, the PDO position, the
// contract's figures and the hard-reset counts
void port_report(regmap_live_t *live);
