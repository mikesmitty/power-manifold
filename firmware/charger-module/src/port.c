#include "port.h"

#include <string.h>

#include "blade.h"

static pdo_limits_t limits;
static pdo_table_t table;
static bool attached;
static bool have_pending, have_contract;
static pdo_request_t pending, contract;

static bool rebuild(void) {
    pdo_table_t was = table;
    pdo_build(&limits, &table);
    return was.count != table.count ||
           memcmp(was.obj, table.obj, table.count * sizeof table.obj[0]) != 0;
}

void port_init(void) {
    limits = (pdo_limits_t){.cable_ma = CABLE_DEFAULT_MA};
    table.count = 0;
    attached = false;
    have_pending = have_contract = false;
}

bool port_set_limits(uint16_t max_ma, uint16_t max_mv) {
    limits.max_ma = max_ma;
    limits.max_mv = max_mv;
    return rebuild();
}

bool port_set_cable_ma(uint16_t ma) {
    limits.cable_ma = ma;
    return rebuild();
}

const pdo_table_t *port_table(void) {
    return &table;
}

void port_attach(void) {
    attached = true;
    have_pending = have_contract = false;
}

void port_detach(void) {
    attached = false;
    have_pending = have_contract = false;
    limits.cable_ma = CABLE_DEFAULT_MA;
    rebuild();
}

bool port_attached(void) {
    return attached;
}

bool port_request(uint32_t rdo) {
    pdo_request_t r;
    if (!attached || !pdo_check_request(&table, rdo, &r)) return false;
    pending = r;
    have_pending = true;
    return true;
}

const pdo_request_t *port_pending(void) {
    return have_pending ? &pending : NULL;
}

void port_delivered(void) {
    if (!have_pending) return;
    contract = pending;
    have_contract = true;
    have_pending = false;
}

const pdo_request_t *port_contract(void) {
    return have_contract ? &contract : NULL;
}

void port_contract_lost(void) {
    have_pending = have_contract = false;
}

void port_report(regmap_live_t *live) {
    live->status &= (uint8_t)~(BLADE_ST_ATTACHED | BLADE_ST_CONTRACT | BLADE_ST_PPS | BLADE_ST_CABLE_5A);
    live->pdo = 0;
    live->contract_mv = 0;
    live->contract_ma = 0;
    if (!attached) return;
    live->status |= BLADE_ST_ATTACHED;
    if (limits.cable_ma >= 5000) live->status |= BLADE_ST_CABLE_5A;
    if (!have_contract) return;
    live->status |= BLADE_ST_CONTRACT;
    if (contract.pps) live->status |= BLADE_ST_PPS;
    live->pdo = contract.position;
    live->contract_mv = contract.mv;
    live->contract_ma = contract.op_ma;
}
