#include "sense.h"

#include <stddef.h>

#include "board.h"

uint32_t sense_vdda_mv(uint16_t vrefint_raw, uint16_t vrefint_cal) {
    if (!vrefint_raw) return 0;
    return ((uint32_t)SENSE_CAL_VDDA_MV * vrefint_cal) / vrefint_raw;
}

static uint32_t pin_uv(uint16_t raw, uint32_t vdda_mv) {
    return (uint32_t)(((uint64_t)raw * vdda_mv * 1000u) / SENSE_ADC_MAX);
}

uint32_t sense_rail_mv(uint16_t raw, uint32_t vdda_mv) {
    return (pin_uv(raw, vdda_mv) * SENSE_DIV_NUM + 500) / 1000;
}

uint32_t sense_iana_ma(uint16_t raw, uint32_t vdda_mv) {
    return (pin_uv(raw, vdda_mv) + IANA_UV_PER_MA / 2) / IANA_UV_PER_MA;
}

// ADC counts every 5 degC from -40 to 125:
//   R = 10k * exp(3380 * (1/T - 1/298.15)),  counts = 4095 * R / (R + 10k)
#define NTC_T0_DC    (-400)
#define NTC_STEP_DC  50
static const uint16_t NTC_COUNTS[] = {
    3928, 3872, 3802, 3716, 3613, 3492, // -40 degC on
    3353, 3196, 3024, 2839, 2644, 2445, // -10
    2245, 2048, 1857, 1675, 1505, 1347, //  20
    1203, 1072,  954,  849,  755,  672, //  50
     598,  533,  476,  425,  380,  341, //  80
     306,  276,  249,  224,             // 110
};
#define NTC_N (sizeof NTC_COUNTS / sizeof NTC_COUNTS[0])

// Past these the divider is not reading a thermistor: open above, short below
#define NTC_OPEN_COUNTS   4000
#define NTC_SHORT_COUNTS  100

int16_t sense_ntc_dc(uint16_t raw) {
    if (raw > NTC_OPEN_COUNTS || raw < NTC_SHORT_COUNTS) return SENSE_TEMP_OPEN_DC;
    if (raw >= NTC_COUNTS[0]) return NTC_T0_DC;
    for (size_t i = 1; i < NTC_N; i++) {
        if (raw < NTC_COUNTS[i]) continue;
        int32_t span = NTC_COUNTS[i - 1] - NTC_COUNTS[i];
        int32_t into = NTC_COUNTS[i - 1] - raw;
        return (int16_t)(NTC_T0_DC + (int32_t)(i - 1) * NTC_STEP_DC +
                         (into * NTC_STEP_DC + span / 2) / span);
    }
    return (int16_t)(NTC_T0_DC + (int32_t)(NTC_N - 1) * NTC_STEP_DC);
}

int16_t sense_mcu_dc(uint16_t raw, uint32_t vdda_mv, uint16_t ts_cal1, uint16_t ts_cal2) {
    if (ts_cal2 <= ts_cal1) return SENSE_TEMP_OPEN_DC; // blank calibration
    // the sample as it would have read at the calibration rail
    int32_t at_cal = (int32_t)(((uint64_t)raw * vdda_mv) / SENSE_CAL_VDDA_MV);
    return (int16_t)(300 + ((at_cal - ts_cal1) * 1000) / (ts_cal2 - ts_cal1));
}
