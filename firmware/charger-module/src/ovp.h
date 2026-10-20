#pragma once

#include <stdint.h>

// Tracking VBUS over-voltage threshold. The DAC on PA4 sets the trip point
// of comparator half A on the converter output (VOUT_SENSE, 1/11); the fixed
// 23 V half B sits above it. Either one pulls TCPP_EN low, which opens the
// VBUS switch without the firmware.
//
// Out of reset PA4 floats and R2 holds the threshold at 0 V, and nothing
// drives TCPP_EN, which R9 holds low: the switch stays open until the
// firmware has set a threshold and raised PA15.
//
// Order on a voltage change: the threshold always covers the higher of the
// two voltages while the output moves. Rising: threshold first, then the
// converter. Falling: converter first, threshold once VBUS has arrived.
//
// With the switch open, the threshold covers whatever the converter output
// still holds (never under the 5 V level), and comes down to the 5 V level
// only once that output is at 5 V, just before the switch closes. An empty
// receptacle discharges in milliseconds, the converter output (about 260 uF)
// far slower, and the comparator watches the converter output.

#define OVP_MARGIN_PCT  120 // trip at 1.2 x the contract voltage

uint32_t ovp_threshold_mv(uint32_t contract_mv);               // capped at OVP_CEILING_MV
uint32_t ovp_transition_mv(uint32_t from_mv, uint32_t to_mv);  // threshold to hold during a change
// 12-bit DAC code for a trip point. vdda_mv is the measured 3.3 V rail, the
// DAC's reference.
uint16_t ovp_dac_code(uint32_t threshold_mv, uint32_t vdda_mv);
