#include "vin.h"

#include <stdio.h>
#include <string.h>

#include "settings.h"
#include "vin_hw.h"

#define SAMPLE_MS      100
#define CONVERSIONS    8   // per sample, back to back
#define WINDOW         10  // samples in the moving average: one second
#define FULL_SCALE_MV  42900 // 3.3 V x 13
#define ADC_COUNTS     4096

static bool fitted;
static uint32_t ring[WINDOW]; // per-sample sums of CONVERSIONS counts
static unsigned ring_n, ring_i;
static uint32_t last_sample_ms, last_raw;
static bool low, high;
static char status_buf[24];

void vin_init(void) {
    fitted = vin_hw_init();
    memset(ring, 0, sizeof(ring));
    ring_n = ring_i = 0;
    last_sample_ms = 0;
    last_raw = 0;
    low = high = false;
}

static uint32_t cal(void) {
    uint32_t c = g_settings.vin_cal;
    return (c < VIN_CAL_MIN || c > VIN_CAL_MAX) ? VIN_CAL_DEFAULT : c;
}

uint32_t vin_mv(void) {
    if (!fitted || !ring_n) return 0;
    uint64_t total = 0;
    for (unsigned i = 0; i < ring_n; i++) total += ring[i];
    // counts -> mV: total / (n x CONVERSIONS) x FULL_SCALE / 4096 x cal / 1000
    return (uint32_t)((total * FULL_SCALE_MV * cal()) /
                      ((uint64_t)ring_n * CONVERSIONS * ADC_COUNTS * 1000u));
}

static void update_flags(void) {
    uint32_t mv = vin_mv();
    if (!low && mv < VIN_LOW_MV) {
        low = true;
        printf("vin: bus %lu.%02lu V, under %u V\n", (unsigned long)mv / 1000,
               (unsigned long)(mv % 1000) / 10, VIN_LOW_MV / 1000);
    } else if (low && mv >= VIN_LOW_MV + VIN_HYST_MV) {
        low = false;
        printf("vin: bus back in range, %lu.%02lu V\n", (unsigned long)mv / 1000,
               (unsigned long)(mv % 1000) / 10);
    }
    if (!high && mv > VIN_HIGH_MV) {
        high = true;
        printf("vin: bus %lu.%02lu V, over %u V\n", (unsigned long)mv / 1000,
               (unsigned long)(mv % 1000) / 10, VIN_HIGH_MV / 1000);
    } else if (high && mv <= VIN_HIGH_MV - VIN_HYST_MV) {
        high = false;
        printf("vin: bus back in range, %lu.%02lu V\n", (unsigned long)mv / 1000,
               (unsigned long)(mv % 1000) / 10);
    }
}

void vin_poll(uint32_t now_ms) {
    if (!fitted) return;
    if (ring_n && now_ms - last_sample_ms < SAMPLE_MS) return;
    last_sample_ms = now_ms;
    uint32_t sum = 0;
    for (int i = 0; i < CONVERSIONS; i++) {
        last_raw = vin_hw_read();
        sum += last_raw;
    }
    ring[ring_i] = sum;
    ring_i = (ring_i + 1) % WINDOW;
    if (ring_n < WINDOW) ring_n++;
    update_flags();
}

bool vin_fitted(void) { return fitted; }
uint32_t vin_raw(void) { return last_raw; }
bool vin_low(void) { return fitted && low; }
bool vin_high(void) { return fitted && high; }

uint16_t vin_cal_for(uint32_t measured_mv) {
    uint32_t now = vin_mv();
    if (!now) return 0;
    uint32_t c = (uint32_t)(((uint64_t)cal() * measured_mv + now / 2) / now);
    if (c < VIN_CAL_MIN) c = VIN_CAL_MIN;
    if (c > VIN_CAL_MAX) c = VIN_CAL_MAX;
    return (uint16_t)c;
}

const char *vin_status_str(void) {
    if (!fitted) return "not fitted";
    if (!ring_n) return "no sample yet";
    uint32_t mv = vin_mv();
    snprintf(status_buf, sizeof(status_buf), "%lu.%02lu V%s", (unsigned long)mv / 1000,
             (unsigned long)(mv % 1000) / 10, low ? " LOW" : high ? " HIGH" : "");
    return status_buf;
}
