#include "pdo.h"

#include "blade.h"
#include "blade_regs.h"

// Fixed supply PDO: [31:30] = 00, [19:10] voltage in 50 mV, [9:0] current in
// 10 mA. Flags live in the first (5 V) object only.
#define FIXED_UNCONSTRAINED   (1u << 27) // fed from the chassis supply, not a battery
#define FIXED_STEP_MA         10

// SPR PPS APDO: [31:30] = 11, [29:28] = 00, [24:17] maximum voltage in
// 100 mV, [15:8] minimum voltage in 100 mV, [6:0] current in 50 mA
#define APDO_PPS              (3u << 30)
#define PPS_STEP_MA           50
#define PPS_STEP_MV           20
#define PPS_MIN_MV            3300

// Request: [31:28] object position, [26] capability mismatch.
// Fixed: [19:10] operating current in 10 mA.
// PPS: [20:9] output voltage in 20 mV, [6:0] operating current in 50 mA.
#define RDO_POSITION(rdo)     (((rdo) >> 28) & 0xFu)
#define RDO_MISMATCH          (1u << 26)

static const uint16_t FIXED_MV[] = {5000, 9000, 12000, 15000, 20000};
static const uint16_t PPS_MAX_MV[] = {11000, 21000};

bool pdo_is_pps(uint32_t pdo) { return (pdo >> 28) == 0xCu; }
uint32_t pdo_fixed_mv(uint32_t pdo) { return ((pdo >> 10) & 0x3FFu) * 50; }
uint32_t pdo_fixed_ma(uint32_t pdo) { return (pdo & 0x3FFu) * 10; }
uint32_t pdo_pps_min_mv(uint32_t pdo) { return ((pdo >> 8) & 0xFFu) * 100; }
uint32_t pdo_pps_max_mv(uint32_t pdo) { return ((pdo >> 17) & 0xFFu) * 100; }
uint32_t pdo_pps_ma(uint32_t pdo) { return (pdo & 0x7Fu) * 50; }

static uint32_t current_for(const pdo_limits_t *lim, uint32_t top_mv, uint32_t step_ma) {
    uint32_t ma = lim->max_ma;
    if (ma > BLADE_MAX_MA_LIMIT) ma = BLADE_MAX_MA_LIMIT;
    if (ma > lim->cable_ma) ma = lim->cable_ma;
    uint32_t power_ma = (uint32_t)(((uint64_t)PORT_POWER_MAX_MW * 1000u) / top_mv);
    if (ma > power_ma) ma = power_ma;
    return ma - ma % step_ma;
}

void pdo_build(const pdo_limits_t *lim, pdo_table_t *out) {
    out->count = 0;
    if (!lim->max_ma) return;
    uint32_t cap_mv = lim->max_mv >= BLADE_MAX_MV_ALL ? 21000u : lim->max_mv;

    for (unsigned i = 0; i < sizeof FIXED_MV / sizeof FIXED_MV[0]; i++) {
        uint32_t mv = FIXED_MV[i];
        if (i && mv > cap_mv) continue; // 5 V always stays
        uint32_t pdo = ((mv / 50) << 10) | (current_for(lim, mv, FIXED_STEP_MA) / 10);
        if (!i) pdo |= FIXED_UNCONSTRAINED;
        out->obj[out->count++] = pdo;
    }
    for (unsigned i = 0; i < sizeof PPS_MAX_MV / sizeof PPS_MAX_MV[0]; i++) {
        uint32_t top = PPS_MAX_MV[i];
        if (top > cap_mv) continue;
        out->obj[out->count++] = APDO_PPS | ((top / 100) << 17) | ((PPS_MIN_MV / 100) << 8) |
                                 (current_for(lim, top, PPS_STEP_MA) / 50);
    }
}

bool pdo_check_request(const pdo_table_t *t, uint32_t rdo, pdo_request_t *out) {
    uint32_t pos = RDO_POSITION(rdo);
    if (!pos || pos > t->count) return false;
    uint32_t pdo = t->obj[pos - 1];

    out->position = (uint8_t)pos;
    out->mismatch = (rdo & RDO_MISMATCH) != 0;
    out->pps = pdo_is_pps(pdo);
    if (out->pps) {
        uint32_t mv = ((rdo >> 9) & 0xFFFu) * PPS_STEP_MV;
        uint32_t ma = (rdo & 0x7Fu) * PPS_STEP_MA;
        if (mv < pdo_pps_min_mv(pdo) || mv > pdo_pps_max_mv(pdo)) return false;
        if (ma > pdo_pps_ma(pdo)) return false;
        out->mv = (uint16_t)mv;
        out->op_ma = (uint16_t)ma;
        out->limit_ma = (uint16_t)ma;
    } else {
        uint32_t ma = ((rdo >> 10) & 0x3FFu) * 10;
        if (ma > pdo_fixed_ma(pdo)) return false;
        out->mv = (uint16_t)pdo_fixed_mv(pdo);
        out->op_ma = (uint16_t)ma;
        out->limit_ma = (uint16_t)pdo_fixed_ma(pdo);
    }
    return true;
}
