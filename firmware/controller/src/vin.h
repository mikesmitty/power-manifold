#pragma once

#include <stdbool.h>
#include <stdint.h>

// DC bus (VIN) voltage monitor. The controller card divides VIN by 13
// (R13 120k / R14 10k, C18 100 nF across the bottom leg) into GP28/ADC2, so
// 3.3 V full scale reads 42.9 V, above the 36.7 V where the bus's SMCJ33CA
// starts to conduct. In a surge the TVS clamps near 53 V; C18 smooths a pulse that
// short, and even held at 53 V the divider pushes only tens of microamps
// into the pin's protection diode. Sampled on core 0 from the main loop:
// eight conversions every 100 ms, reported as a one-second moving average,
// about 10 mV a count. The reference is the card's 3.3 V rail and the
// divider is 1 % parts, so settings vin_cal (permille) trims the gain from
// one meter reading. Hardware-free above vin_hw.h; "not present" on a board
// without the divider.

#define VIN_CAL_DEFAULT 1000
#define VIN_CAL_MIN     900
#define VIN_CAL_MAX     1100

// The declared input is 24 V nominal, -15 %/+20 %: 20.4-28.8 V. The hardware
// tolerates 32 V: the SMCJ33CA's 33 V stand-off and the LM74800's ~34 V
// cut-off set that ceiling. The LM74800 also opens the bus below ~17.7 V
// (closing it again near 19.3 V), and the backplane's 5 V buck drops out
// near 15 V. The flags sit just outside the declared range and come with
// half a volt of hysteresis so a sagging supply does not flap the problem
// indicator. Both flags sit inside the hardware window so the controller can
// report the excursion before the bus is cut. The bus-sag cap (bus_cap.h)
// acts earlier still, at 20 V.
#define VIN_LOW_MV      19000
#define VIN_HIGH_MV     29000
#define VIN_HYST_MV     500

void vin_init(void);
void vin_poll(uint32_t now_ms);

bool     vin_fitted(void);
uint32_t vin_mv(void);   // one-second average; 0 until the first sample
uint32_t vin_raw(void);  // the last conversion's counts, for calibration
bool     vin_low(void);  // under VIN_LOW_MV (with hysteresis)
bool     vin_high(void); // over VIN_HIGH_MV (with hysteresis)

// The vin_cal that makes the current reading equal measured_mv (a meter on
// the bus); clamped to VIN_CAL_MIN..MAX. 0 when there is no reading yet.
uint16_t vin_cal_for(uint32_t measured_mv);

// "24.13 V", "18.70 V LOW", "33.40 V HIGH", "no sample yet", "not present"
const char *vin_status_str(void);
