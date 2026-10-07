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
