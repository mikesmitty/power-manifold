#pragma once

#include <stddef.h>
#include <stdint.h>

// Unix time -> calendar, without a timezone database: local time is UTC
// plus a fixed offset from settings. Hardware-free.
void civil_from_days(uint32_t days, unsigned *y, unsigned *m, unsigned *d);
// "2026-09-04 21:40" for epoch shifted by tz_offset_min; returns bytes written
size_t civil_format(char *out, size_t cap, uint32_t epoch, int16_t tz_offset_min);
// minutes after local midnight for epoch shifted by tz_offset_min
uint16_t civil_local_minute(uint32_t epoch, int16_t tz_offset_min);
