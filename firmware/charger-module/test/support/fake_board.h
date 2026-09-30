#pragma once

#include <stdbool.h>
#include <stdint.h>

// The board around the power path, enough of it to check the order things
// are done in: time, the TCPP_EN line with its over-voltage comparator, and
// a VBUS that follows the converter and the switch the way the hardware
// would (the parts' registers are fake_bus.h's).
//
//   - VBUS rises and falls at the converter's slew rate while the switch is
//     closed and the output enabled;
//   - without forced PWM and without a load it does not fall at all;
//   - with the switch open it falls only through a discharge path;
//   - the comparator trips the moment the converter output is above the
//     threshold the DAC holds, and the trip is remembered.

void fake_board_reset(void);
void fake_board_set_load(bool loaded);        // a sink drawing current: VBUS follows downward in PFM too
void fake_board_set_vdda_mv(uint32_t mv);
void fake_board_set_vout_mv(uint32_t mv);     // charge left on the converter output
void fake_board_fit_tcpp02(bool fitted);
void fake_board_stick_vbus(bool stuck);       // VBUS stops moving
void fake_board_set_port_fault(bool low);     // FLGn
void fake_board_set_converter_fault(bool low);// FB/INT

void fake_board_set_en(bool high);            // the slot's EN line (the converter's presence is fake_bus.h's)
void fake_board_set_temps_dc(int16_t conv, int16_t plug);

bool fake_board_alert(void);                  // ALERT# asserted
unsigned fake_board_watchdog_feeds(void);

// The MCU's boot option and the resets the firmware asks for (which, on
// the board, do not return)
void     fake_board_set_boot_via_loader(bool set);
unsigned fake_board_loader_resets(void);      // hw_reset_to_loader() calls
unsigned fake_board_boot_option_writes(void); // hw_program_boot_via_loader() calls

uint32_t fake_board_vbus_mv(void);
uint32_t fake_board_vout_mv(void);
uint32_t fake_board_trip_mv(void);            // what the DAC code amounts to
uint32_t fake_board_peak_vbus_mv(void);
bool     fake_board_comparator_tripped(void); // at any time since the reset
bool     fake_board_output_on_with_switch_open(void); // ever
