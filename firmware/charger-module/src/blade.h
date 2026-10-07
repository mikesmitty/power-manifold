#pragma once

#define FW_VERSION "0.4.0" // x-release-please-version
// FW_MAJOR / FW_MINOR / FW_PATCH are parsed out of FW_VERSION by
// cmake/version.cmake and passed on the command line.

// No port promises more than this at the top of a PDO's voltage range
#define PORT_POWER_MAX_MW   100000u
// Advertised above 3 A only once the cable's e-marker has said 5 A
#define CABLE_DEFAULT_MA    3000u
// Converter limit from attach until the first contract sets its own: what a
// cable without an e-marker may carry, with the fixed-contract margin
#define CABLE_LIMIT_AT_ATTACH_MA 3300u

// Temperature limits, tenths of a degree. A reading past either one for
// 50 ms switches the port off and latches the fault. A sensor that reads
// open or shorted counts as past its limit: a port that cannot see its
// temperature does not run.
#define TEMP_LIMIT_CONV_DC  1000 // board copper at the converter
#define TEMP_LIMIT_PLUG_DC  700  // receptacle shell

// Warning levels, below the trips. A reading that holds at or above one for
// the same 50 ms makes the sensor "warm": the blade sends the sink one PD
// Alert with the over-temperature bit and answers Get_Status with "Warning"
// until the reading has fallen TEMP_WARN_CLEAR_DC under the level. The port
// keeps running. Like the trips, these are provisional until the blade has
// been measured in the closed chassis.
#define TEMP_WARN_CONV_DC   850
#define TEMP_WARN_PLUG_DC   600
#define TEMP_WARN_CLEAR_DC  50
