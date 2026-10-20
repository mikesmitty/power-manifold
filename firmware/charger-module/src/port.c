#include "port.h"

#include <string.h>

#include "blade.h"

#define TYPEC_MV     5000u
#define TYPEC_3A0_MA 3000u
#define TYPEC_1A5_MA 1500u
#define CABLE_3A_PDP_W 60u // 20 V at 3 A

static pdo_limits_t limits;
static pdo_table_t table;
static bool attached;
static bool have_pending, have_contract;
static pdo_request_t pending, contract;
static uint8_t hr_sent, hr_received; // since the MCU started, wrapping

static bool rebuild(void) {
    pdo_table_t was = table;
    pdo_build(&limits, &table);
    return was.count != table.count ||
           memcmp(was.obj, table.obj, table.count * sizeof table.obj[0]) != 0;
}

void port_init(void) {
    limits = (pdo_limits_t){.cable_ma = CABLE_DEFAULT_MA};
    pdo_build(&limits, &table); // empty, and no power
    attached = false;
    have_pending = have_contract = false;
    hr_sent = hr_received = 0;
}

bool port_set_limits(uint8_t max_w, uint16_t max_mv) {
    limits.max_w = max_w;
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

// The current the power limit allows at 5 V
static uint32_t limit_ma_at_5v(void) {
    return table.power_mw * 1000u / TYPEC_MV;
}

port_rp_t port_rp(void) {
    uint32_t ma = limit_ma_at_5v();
    if (ma >= TYPEC_3A0_MA) return PORT_RP_3A0;
    if (ma >= TYPEC_1A5_MA) return PORT_RP_1A5;
    return PORT_RP_DEFAULT;
}

uint16_t port_typec_ma(void) {
    uint32_t ma = limit_ma_at_5v();
    return (uint16_t)(ma < TYPEC_3A0_MA ? ma : TYPEC_3A0_MA);
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

void port_hard_reset(bool by_sink) {
    if (by_sink) hr_received++;
    else hr_sent++;
    port_contract_lost();
}

void port_report(regmap_live_t *live) {
    live->status &= (uint8_t)~(BLADE_ST_ATTACHED | BLADE_ST_CONTRACT | BLADE_ST_PPS | BLADE_ST_CABLE_5A);
    live->pdo = 0;
    live->contract = BLADE_CONTRACT_NONE;
    live->contract_mv = 0;
    live->contract_ma = 0;
    live->hr_sent = hr_sent;
    live->hr_received = hr_received;
    if (!attached) return;
    live->status |= BLADE_ST_ATTACHED;
    if (limits.cable_ma >= 5000) live->status |= BLADE_ST_CABLE_5A;
    if (!have_contract) return;
    live->status |= BLADE_ST_CONTRACT;
    if (contract.kind == PDO_PPS) live->status |= BLADE_ST_PPS;
    live->pdo = contract.position;
    live->contract = contract.kind == PDO_PPS   ? BLADE_CONTRACT_PPS
                     : contract.kind == PDO_AVS ? BLADE_CONTRACT_AVS
                                                : BLADE_CONTRACT_FIXED;
    live->contract_mv = contract.mv;
    // At the top of a power-limited PPS range the port holds less than the
    // sink asked for (pdo_check_request). The report gives what it delivers.
    live->contract_ma = contract.op_ma < contract.limit_ma ? contract.op_ma : contract.limit_ma;
}

void port_source_info(uint32_t sido[2]) {
    uint32_t present_w = table.power_mw / 1000u;
    if (limits.cable_ma < 5000 && present_w > CABLE_3A_PDP_W) present_w = CABLE_3A_PDP_W;
    uint32_t reported_w = pdo_reported_w(&table);
    // SIDO1: [31] port type, 0 = Managed Capability Port; [23:16] Port
    // Maximum PDP, [15:8] Port Present PDP, [7:0] Port Reported PDP, each in
    // 1 W
    sido[0] = ((uint32_t)PORT_MAX_PDP_W << 16) | (present_w << 8) | reported_w;
    // SIDO2: [31] port type again, [30] DPS port, 0 = no; [17:9] Port
    // Maximum PDP and [8:0] Port Guaranteed PDP, each in 0.5 W
    sido[1] = ((uint32_t)(PORT_MAX_PDP_W * 2) << 9) | (PORT_GUARANTEED_PDP_W * 2);
}
