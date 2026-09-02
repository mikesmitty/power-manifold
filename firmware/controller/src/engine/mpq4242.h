#pragma once

#include <stdbool.h>
#include <stdint.h>

// MPQ4242 buck-boost USB-PD controller, one per blade behind the mux (select
// the channel first). Register map ported from the V1 CircuitPython driver.
// The part negotiates PD autonomously; this driver constrains the advertised
// PDO set and observes the resulting contracts.

#define MPQ4242_CHIP_ID 0x58

// GPIO functions (CTL_SYS2). V2 blades: GPIO1 = FAULT -> blade ALERT#/B15,
// GPIO2 = unused with a 10k pull-down.
#define MPQ4242_GPIO1_FN_FAULT     2
#define MPQ4242_GPIO2_FN_DISABLED  0

#define MPQ4242_PDO_TYPE_FIXED     0
#define MPQ4242_PDO_TYPE_PPS       1

#define MPQ4242_PEAK_CL_8A         0
#define MPQ4242_PEAK_CL_12A        1
#define MPQ4242_PEAK_CL_16A        2
#define MPQ4242_PEAK_CL_20A        3

// CC over-current blank time (CTL_SYS5). Datasheet Rev 1.0 lists conflicting
// OTP defaults (2ms vs 16ms), so configure() programs it rather than trusting
// the -0000 part; 16ms keeps the <=10ms PD peak-current overload tiers usable.
#define MPQ4242_CC_BLANK_NONE      0
#define MPQ4242_CC_BLANK_2MS       1
#define MPQ4242_CC_BLANK_16MS      2
#define MPQ4242_CC_BLANK_32MS      3

typedef struct {
    bool     attached;      // STATUS1[7]
    uint8_t  selected_pdo;  // STATUS2[3:1], 0 = none
    uint8_t  fault_bits;    // MPQ_FAULT_* (manifold.h), from STATUS1+STATUS2
    uint32_t contract_mw;   // STATUS3 * 0.5W
} mpq4242_status_t;

bool mpq4242_probe(void);                  // DEV_ID == 0x58
bool mpq4242_unlock(void);                 // CLK_ON=1 enables register writes
bool mpq4242_configure(uint32_t max_ma); // GPIOs, peak CL, CC blank, dither, PDOs
bool mpq4242_read_status(mpq4242_status_t *s);

// Default advertised PDO set:
//   PDO1: Fixed 5V
//   PDO2: Fixed 9V
//   PDO3: Fixed 12V
//   PDO4: Fixed 15V
//   PDO5: Fixed 20V
//   PDO6: PPS 3.3V - 11.0V (phone fast-charge)
//   PDO7: PPS 3.3V - 21.0V (laptop/high-power fast-charge)
bool mpq4242_set_max_current_ma(uint32_t ma); // all PDOs
bool mpq4242_set_pdo_fixed(uint8_t pdo, uint16_t mv, uint32_t ma, bool enabled);
bool mpq4242_set_pdo_pps(uint8_t pdo, uint16_t min_mv, uint16_t max_mv, uint32_t ma, bool enabled);
bool mpq4242_set_pdo_enabled(uint8_t pdo, bool enabled); // PDOs 2-7
bool mpq4242_send_src_cap(void);           // re-advertise after PDO changes
bool mpq4242_send_hard_reset(void);
