#include "civil_time.h"

#include <stdio.h>

// Howard Hinnant's civil_from_days
void civil_from_days(uint32_t days, unsigned *y, unsigned *m, unsigned *d) {
    int64_t z = (int64_t)days + 719468;
    int64_t era = z / 146097;
    unsigned doe = (unsigned)(z - era * 146097);
    unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    unsigned mp = (5 * doy + 2) / 153;
    *d = doy - (153 * mp + 2) / 5 + 1;
    *m = mp < 10 ? mp + 3 : mp - 9;
    *y = (unsigned)(yoe + era * 400) + (*m <= 2);
}

// Howard Hinnant's days_from_civil
uint32_t civil_days(unsigned y, unsigned m, unsigned d) {
    int64_t yy = (int64_t)y - (m <= 2);
    int64_t era = (yy >= 0 ? yy : yy - 399) / 400;
    unsigned yoe = (unsigned)(yy - era * 400);
    unsigned doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    int64_t days = era * 146097 + (int64_t)doe - 719468;
    return days < 0 ? 0 : (uint32_t)days;
}

static uint32_t shifted(uint32_t epoch, int16_t tz_offset_min) {
    int64_t t = (int64_t)epoch + (int64_t)tz_offset_min * 60;
    return t < 0 ? 0 : (uint32_t)t;
}

size_t civil_format(char *out, size_t cap, uint32_t epoch, int16_t tz_offset_min) {
    uint32_t t = shifted(epoch, tz_offset_min);
    unsigned y, m, d;
    civil_from_days(t / 86400, &y, &m, &d);
    uint32_t rem = t % 86400;
    int n = snprintf(out, cap, "%04u-%02u-%02u %02lu:%02lu", y, m, d,
                     (unsigned long)(rem / 3600), (unsigned long)(rem % 3600 / 60));
    return n < 0 ? 0 : (size_t)n < cap ? (size_t)n : cap - 1;
}

uint16_t civil_local_minute(uint32_t epoch, int16_t tz_offset_min) {
    return (uint16_t)(shifted(epoch, tz_offset_min) % 86400 / 60);
}
