#include "pdo.h"

#include "blade.h"
#include "blade_regs.h"

// Fixed supply PDO: [31:30] = 00, [19:10] voltage in 50 mV, [9:0] current in
// 10 mA. Flags live in the first (5 V) object only.
#define FIXED_UNCONSTRAINED   (1u << 27) // fed from the chassis supply, not a battery
#define FIXED_STEP_MA         10

// SPR PPS APDO: [31:30] = 11, [29:28] = 00, [27] PPS Power Limited, [24:17]
// maximum voltage in 100 mV, [15:8] minimum voltage in 100 mV, [6:0]
// current in 50 mA.
#define APDO_PPS              (3u << 30)
#define PPS_POWER_LIMITED     (1u << 27)
#define PPS_STEP_MA           50
#define PPS_STEP_MV           20
#define PPS_MIN_MV            5000

// SPR AVS APDO: [31:30] = 11, [29:28] = 10, [19:10] maximum current from
// 9 V to 15 V in 10 mA, [9:0] maximum current from 15 V to 20 V in 10 mA.
// The two currents are those of the 15 V and the 20 V fixed objects; the
// second is 0 when the range stops at 15 V.
#define APDO_AVS              ((3u << 30) | (2u << 28))
#define AVS_STEP_MA           10
#define AVS_MIN_MV            9000
#define AVS_MID_MV            15000
#define AVS_TOP_MV            20000

// Request: [31:28] object position, [26] capability mismatch.
// Fixed: [19:10] operating current in 10 mA.
// PPS: [20:9] output voltage in 20 mV, [6:0] operating current in 50 mA.
// AVS: [20:9] output voltage in 25 mV with [10:9] = 00, so 100 mV steps;
// [6:0] operating current in 50 mA.
#define RDO_POSITION(rdo)     (((rdo) >> 28) & 0xFu)
#define RDO_MISMATCH          (1u << 26)
#define RDO_VOLTAGE(rdo)      (((rdo) >> 9) & 0xFFFu)
#define RDO_APDO_MA(rdo)      (((rdo) & 0x7Fu) * 50)
#define AVS_RDO_STEP_MV       25

static const uint16_t FIXED_MV[] = {5000, 9000, 12000, 15000, 20000};

// The PPS ranges, widest first; the table carries the first that fits under
// the voltage cap. Each range's current is figured at its PD 3.2 "Prog
// Voltage" (Table 3.4): the power limit divided by 20 V or 9 V.
static const struct {
    uint16_t max_mv;
    uint16_t prog_mv;
} PPS_RANGES[] = {{21000, 20000}, {11000, 9000}};

bool pdo_is_apdo(uint32_t pdo) { return (pdo >> 30) == 3u; }
bool pdo_is_pps(uint32_t pdo) { return (pdo >> 28) == 0xCu; }
bool pdo_is_avs(uint32_t pdo) { return (pdo >> 28) == 0xEu; }
uint32_t pdo_fixed_mv(uint32_t pdo) { return ((pdo >> 10) & 0x3FFu) * 50; }
uint32_t pdo_fixed_ma(uint32_t pdo) { return (pdo & 0x3FFu) * 10; }
uint32_t pdo_pps_min_mv(uint32_t pdo) { return ((pdo >> 8) & 0xFFu) * 100; }
uint32_t pdo_pps_max_mv(uint32_t pdo) { return ((pdo >> 17) & 0xFFu) * 100; }
uint32_t pdo_pps_ma(uint32_t pdo) { return (pdo & 0x7Fu) * 50; }
bool pdo_pps_power_limited(uint32_t pdo) { return (pdo & PPS_POWER_LIMITED) != 0; }
uint32_t pdo_avs_ma_15v(uint32_t pdo) { return ((pdo >> 10) & 0x3FFu) * 10; }
uint32_t pdo_avs_ma_20v(uint32_t pdo) { return (pdo & 0x3FFu) * 10; }
uint32_t pdo_avs_max_mv(uint32_t pdo) { return pdo_avs_ma_20v(pdo) ? AVS_TOP_MV : AVS_MID_MV; }

// The current that keeps mv within power_mw
static uint32_t power_ma(uint32_t power_mw, uint32_t mv) {
    return (uint32_t)(((uint64_t)power_mw * 1000u) / mv);
}

// The most current an object at mv may advertise: the lowest of 5 A, the
// cable, and the power limit divided by mv, rounded down to step_ma
static uint32_t current_for(const pdo_limits_t *lim, uint32_t power_mw, uint32_t mv,
                            uint32_t step_ma) {
    uint32_t ma = BLADE_MAX_MA_LIMIT;
    if (ma > lim->cable_ma) ma = lim->cable_ma;
    uint32_t pma = power_ma(power_mw, mv);
    if (ma > pma) ma = pma;
    return ma - ma % step_ma;
}

void pdo_build(const pdo_limits_t *lim, pdo_table_t *out) {
    out->count = 0;
    uint32_t power_mw = (uint32_t)lim->max_w * 1000u;
    if (power_mw > PORT_POWER_MAX_MW) power_mw = PORT_POWER_MAX_MW;
    out->power_mw = power_mw;
    if (!power_mw) return;
    uint32_t cap_mv = lim->max_mv >= BLADE_MAX_MV_ALL ? 21000u : lim->max_mv;

    for (unsigned i = 0; i < sizeof FIXED_MV / sizeof FIXED_MV[0]; i++) {
        uint32_t mv = FIXED_MV[i];
        if (i && mv > cap_mv) continue; // 5 V always stays
        uint32_t pdo = ((mv / 50) << 10) | (current_for(lim, power_mw, mv, FIXED_STEP_MA) / 10);
        if (!i) pdo |= FIXED_UNCONSTRAINED;
        out->obj[out->count++] = pdo;
    }

    // SPR AVS only exists alongside the 15 V fixed object (PD 3.2 Table 3.2)
    if (AVS_MID_MV <= cap_mv) {
        uint32_t ma15 = current_for(lim, power_mw, AVS_MID_MV, AVS_STEP_MA);
        uint32_t ma20 = AVS_TOP_MV <= cap_mv ? current_for(lim, power_mw, AVS_TOP_MV, AVS_STEP_MA) : 0;
        out->obj[out->count++] = APDO_AVS | ((ma15 / 10) << 10) | (ma20 / 10);
    }

    for (unsigned i = 0; i < sizeof PPS_RANGES / sizeof PPS_RANGES[0]; i++) {
        uint32_t top = PPS_RANGES[i].max_mv;
        if (top > cap_mv) continue;
        uint32_t ma = current_for(lim, power_mw, PPS_RANGES[i].prog_mv, PPS_STEP_MA);
        uint32_t pdo = APDO_PPS | ((top / 100) << 17) | ((PPS_MIN_MV / 100) << 8) | (ma / 50);
        // At the top of the range that current would go past the power
        // limit. The flag tells the sink so, and the converter holds the
        // power there (pdo_check_request).
        if ((uint64_t)top * ma > (uint64_t)power_mw * 1000u) pdo |= PPS_POWER_LIMITED;
        out->obj[out->count++] = pdo;
        break;
    }
}

static bool check_pps(const pdo_table_t *t, uint32_t pdo, uint32_t rdo, pdo_request_t *out) {
    uint32_t mv = RDO_VOLTAGE(rdo) * PPS_STEP_MV;
    uint32_t ma = RDO_APDO_MA(rdo);
    if (mv < pdo_pps_min_mv(pdo) || mv > pdo_pps_max_mv(pdo)) return false;
    if (ma > pdo_pps_ma(pdo)) return false;
    uint32_t limit = ma;
    if (pdo_pps_power_limited(pdo)) {
        uint32_t pma = power_ma(t->power_mw, mv);
        if (limit > pma) limit = pma;
    }
    out->kind = PDO_PPS;
    out->mv = (uint16_t)mv;
    out->op_ma = (uint16_t)ma;
    out->limit_ma = (uint16_t)limit;
    return true;
}

static bool check_avs(uint32_t pdo, uint32_t rdo, pdo_request_t *out) {
    uint32_t field = RDO_VOLTAGE(rdo);
    if (field & 3u) return false; // not a 100 mV step
    uint32_t mv = field * AVS_RDO_STEP_MV;
    uint32_t ma = RDO_APDO_MA(rdo);
    if (mv < AVS_MIN_MV || mv > pdo_avs_max_mv(pdo)) return false;
    // Each half of the range has its own current. At exactly 15 V the sink
    // may ask for either (PD 3.2 Table 3.1, note 2).
    uint32_t ma15 = pdo_avs_ma_15v(pdo), ma20 = pdo_avs_ma_20v(pdo);
    uint32_t max_ma = mv < AVS_MID_MV ? ma15 : mv > AVS_MID_MV ? ma20 : (ma15 > ma20 ? ma15 : ma20);
    if (ma > max_ma) return false;
    out->kind = PDO_AVS;
    out->mv = (uint16_t)mv;
    out->op_ma = (uint16_t)ma;
    out->limit_ma = (uint16_t)max_ma;
    return true;
}

bool pdo_check_request(const pdo_table_t *t, uint32_t rdo, pdo_request_t *out) {
    uint32_t pos = RDO_POSITION(rdo);
    if (!pos || pos > t->count) return false;
    uint32_t pdo = t->obj[pos - 1];

    out->position = (uint8_t)pos;
    out->mismatch = (rdo & RDO_MISMATCH) != 0;
    if (pdo_is_pps(pdo)) return check_pps(t, pdo, rdo, out);
    if (pdo_is_avs(pdo)) return check_avs(pdo, rdo, out);

    uint32_t ma = ((rdo >> 10) & 0x3FFu) * 10;
    if (ma > pdo_fixed_ma(pdo)) return false;
    out->kind = PDO_FIXED;
    out->mv = (uint16_t)pdo_fixed_mv(pdo);
    out->op_ma = (uint16_t)ma;
    out->limit_ma = (uint16_t)pdo_fixed_ma(pdo);
    return true;
}

uint32_t pdo_reported_w(const pdo_table_t *t) {
    uint32_t best_mw = 0;
    for (uint8_t i = 0; i < t->count; i++) {
        if (pdo_is_apdo(t->obj[i])) continue;
        uint32_t mw = pdo_fixed_mv(t->obj[i]) * pdo_fixed_ma(t->obj[i]) / 1000u;
        if (mw > best_mw) best_mw = mw;
    }
    return best_mw / 1000u;
}
