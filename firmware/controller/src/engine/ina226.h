#pragma once

#include <stdbool.h>
#include <stdint.h>

// INA226 power monitor, one per blade behind the mux (select the channel
// first). Fixed scaling per the architecture spec: R_SHUNT = 10 mOhm,
// current_LSB = 0.25 mA -> CAL = 2048, power LSB = 6.25 mW.

typedef struct {
    uint16_t bus_mv;
    int32_t  current_ma;
    uint32_t power_mw;
} ina226_reading_t;

bool ina226_probe(void); // manufacturer ID check (0x5449 "TI")
bool ina226_configure(void); // continuous mode, AVG=4, 588us conversions, CAL
bool ina226_read(ina226_reading_t *r);

// Over-current alert on the ALERT# pin, latched (LEN) until read back.
bool ina226_set_alert_ma(uint32_t ma);
bool ina226_alert_tripped(bool *tripped); // reads Mask/Enable AFF; clears latch
