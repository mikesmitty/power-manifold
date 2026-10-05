#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define FW_VERSION      "0.12.0" // x-release-please-version
#define NUM_PORTS       6

// Accepted chassis budget range, every surface (CLI/MQTT/REST). The floor is
// one port's base reserve; the ceiling is the largest supply the chassis is
// specced for (6 blades x 100W).
#define BUDGET_MIN_W    15
#define BUDGET_MAX_W    600

// Per-port supervisory state. Differs from the spec's table in one way: the
// blade negotiates PD contracts autonomously (the MPQ4242 on a gen-2 blade,
// the STM32's PD stack on a gen-3 one), so there is no in-line "NEGOTIATING"
// state — the engine constrains the advertised PDO set ahead of time and
// reacts to the contract changes it reads back.
typedef enum {
    PORT_STATE_ABSENT = 0,  // PRES# high: no blade in slot
    PORT_STATE_PROBE,       // blade seated, finding out which generation it is and configuring it
    PORT_STATE_IDLE,        // powered, advertising, no sink attached
    PORT_STATE_ACTIVE,      // sink attached, contract in place
    PORT_STATE_THROTTLED,   // active with a budget-restricted PDO set
    PORT_STATE_FAULT,       // latched fault, EN low, cooldown running
    PORT_STATE_DISABLED,    // administratively off
    PORT_STATE_UPDATE,      // a gen-3 blade in its ROM bootloader: being checked, programmed or started
    PORT_STATE_COUNT,
} port_state_t;

const char *port_state_name(port_state_t s);

// Port fault bits, one vocabulary for both blade generations. Bits 0-7 are
// the MPQ4242's flags as a gen-2 blade reports them (the V1 register-map
// encoding, unchanged); the rest come from a gen-3 blade's fault register,
// folded where the controller needs no finer distinction — the blade keeps
// the detail, and EVT_FAULT's arg carries its raw word (PORT_FAULT_ARG_GEN3).
#define PORT_FAULT_GENERAL      (1u << 0)  // gen 2
#define PORT_FAULT_OTW1         (1u << 1)  // gen 2: over-temperature warning
#define PORT_FAULT_OTW2         (1u << 2)  // gen 2
#define PORT_FAULT_NTC1         (1u << 3)  // gen 2: thermistor
#define PORT_FAULT_NTC2         (1u << 4)  // gen 2
#define PORT_FAULT_CC           (1u << 5)  // CC line fault (gen 3: over-voltage on CC)
#define PORT_FAULT_SHORT_VBATT  (1u << 6)  // gen 2: output short
#define PORT_FAULT_VBATT_LOW    (1u << 7)  // gen 2: output under-voltage
#define PORT_FAULT_OCP          (1u << 8)  // over-current: the INA226 trip (gen 2), the VBUS switch's latch (gen 3)
#define PORT_FAULT_OVP          (1u << 9)  // gen 3: the over-voltage comparator opened the VBUS switch
#define PORT_FAULT_VCONN        (1u << 10) // gen 3: VCONN over-current
#define PORT_FAULT_PORT_HOT     (1u << 11) // gen 3: the port protector's own over-temperature
#define PORT_FAULT_CONVERTER    (1u << 12) // gen 3: converter short / current limit / over-voltage
#define PORT_FAULT_CONV_HOT     (1u << 13) // gen 3: converter thermistor over its limit
#define PORT_FAULT_PLUG_HOT     (1u << 14) // gen 3: receptacle thermistor over its limit
#define PORT_FAULT_BLADE        (1u << 15) // gen 3: the blade's own parts or PD stack failed it

// EVT_FAULT arg: a gen-3 record carries the blade's raw BLADE_FAULT_* word
// under this tag; anything else is a gen-2 record whose arg is the INA226
// trip flag, as it always was.
#define PORT_FAULT_ARG_GEN3     (3u << 24)
#define PORT_FAULT_ARG_IS_GEN3(arg) (((arg) >> 24) == 3u)

typedef struct {
    uint8_t  state;         // port_state_t
    uint8_t  gen;           // blade generation (2 or 3) once probed, 0 before
    bool     attached;
    bool     charged;       // attached sink's draw stayed under the charged floor (see settings)
    uint8_t  selected_pdo;  // 1-7, 0 = none
    uint16_t fault_bits;    // PORT_FAULT_* accumulated since last clear
    uint16_t bus_mv;        // VBUS at the blade's meter
    int32_t  current_ma;    // port current (signed on a gen-2 blade's INA226)
    uint32_t power_mw;
    uint32_t contract_mw;   // budget reservation held by this port
    uint32_t energy_mwh;    // delivered since boot (not persisted)
    uint8_t  update_pct;    // PORT_STATE_UPDATE: how far the image has been written, 0-100
    bool     update_due;    // the blade's firmware is to be updated once nothing is plugged into the port
    bool     silent;        // powered, and the blade is not answering: what is shown is its last answer
    // A gen-3 blade's thermometers, 0.1 degC while the port is powered and
    // polled; PORT_TEMP_NONE otherwise, and for an NTC the blade reads as
    // open. A gen-2 blade has none the controller can read.
    int16_t  temp_conv_dc;  // converter
    int16_t  temp_plug_dc;  // receptacle
    int16_t  temp_mcu_dc;
} port_telemetry_t;

#define PORT_TEMP_NONE INT16_MIN // the value the blade uses for an open NTC as well

// "37.5" (or "-0.5"), else `none` without a reading.
static inline void port_temp_text(char *out, size_t cap, int16_t dc, const char *none) {
    if (dc == PORT_TEMP_NONE) snprintf(out, cap, "%s", none);
    else snprintf(out, cap, "%s%d.%d", dc < 0 ? "-" : "", abs(dc / 10), abs(dc % 10));
}

typedef struct {
    port_telemetry_t port[NUM_PORTS];
    uint32_t total_mw;      // sum of measured port power
    uint32_t reserved_mw;   // sum of budget reservations
    uint32_t budget_mw;     // chassis budget in force
    uint32_t energy_mwh;    // chassis total delivered since boot
    bool     fan_on;
    bool     fan_auto;      // fan under the engine's auto policy
    bool     alert_active;  // GLOBAL_ALERT# currently asserted
    bool     warm_start;    // the expander was found programmed: no reset, ENs kept
    uint8_t  adopted;       // bit N: port N was powered through the start (warm only)
} telemetry_t;

// Commands, core 0 -> core 1. The engine is the sole owner of the I2C bus and
// the GPIO expander; every management surface mutates hardware only through
// these.
typedef enum {
    CMD_PORT_ENABLE,     // port
    CMD_PORT_DISABLE,    // port
    CMD_PORT_HARD_RESET, // port: PD hard reset to sink
    CMD_PORT_SRC_CAP,    // port: re-send source capabilities
    CMD_SET_BUDGET,      // arg = chassis budget mW
    CMD_FAN,             // arg = 0/1; also drops the fan out of auto mode
    CMD_FAN_AUTO,        // hand the fan back to the engine's auto policy
    CMD_LED_BRIGHTNESS,  // arg = 0-255
    CMD_LED_IDENTIFY,    // arg = ms: flash the whole chain (Improv identify)
    CMD_LED_CHASSIS,     // arg = LED_CHASSIS_* flags shown on the chassis light
    CMD_PORT_LIMIT,      // port, arg = mA: new advertised current ceiling (g_settings already holds it)
    CMD_PORT_VOLT,       // port, arg = mV: new voltage cap (g_settings already holds it)
    CMD_LED_HOLD,        // arg = 0-255: front-button hold progress shown on the chain (led_pattern.h)
    CMD_LED_ACK,         // arg = ms: brief white flash of the whole chain (a button press registered)
    CMD_PORT_UPDATE,     // port: write the bundled firmware to a gen-3 blade, whatever it runs (blade_update.h)
    CMD_SIM,             // FAKE_BLADES only: fault injection, arg packed per engine/sim/sim_inject.h
    CMD_I2C_DIAG,        // real builds only: bench bus access, arg packed per engine/i2c_diag.h
    CMD_SET_CEILING,     // arg = mA: a current limit applied to every port on top of its own setting; 0 removes it (bus_cap.h)
} cmd_op_t;

// Per-port advertised current ceiling (settings port_limit_ma). It is the
// current field of every PDO the blade advertises, so the wattage ceiling
// scales with the voltage the sink picks, up to PORT_POWER_MAX_MW: a PDO
// whose top voltage would take that current past 100 W advertises less (the
// 21 V PPS range stops at 4.75 A). The blade's own hardware limit is
// PORT_HW_MAX_MA; on a gen-2 blade the INA226 emergency trip sits at 125 %
// of that and does not move with the setting (the MPQ4242 enforces its own
// OCP). A gen-3 blade holds its own limits and needs no trip from here.
#define PORT_HW_MAX_MA    5000
#define PORT_LIMIT_MIN_MA 500
#define PORT_LIMIT_MAX_MA PORT_HW_MAX_MA
#define PORT_POWER_MAX_MW 100000

// Per-port voltage cap (settings port_max_mv): the highest PDO the blade
// advertises, one of the fixed PDO voltages 5/9/12/15/20 V. Fixed PDOs
// above it and any PPS range reaching above it are withheld, so a sink can
// never negotiate past it. PORT_VOLT_MAX_MV means the whole table.
#define PORT_VOLT_MAX_MV 20000

// Administrative state a port takes at power-up (settings port_boot). Every
// surface that switches a port on or off records the new state in
// settings port_off_mask so PORT_BOOT_LAST can restore it.
#define PORT_BOOT_ON   0 // enabled (default)
#define PORT_BOOT_OFF  1 // disabled until switched on
#define PORT_BOOT_LAST 2 // whatever it was last switched to

// CMD_LED_CHASSIS flags: core 0's view of the management plane, shown on the
// chassis light at the head of the chain (see engine/led_pattern.h)
#define LED_CHASSIS_BLE_OPEN   (1u << 0) // Improv provisioning window open: blue
#define LED_CHASSIS_NET_DOWN   (1u << 1) // no link holds an address: white
#define LED_CHASSIS_SETUP_OPEN (1u << 2) // settings open for first-time setup, no API token yet: magenta
#define LED_CHASSIS_BUS_FAULT  (1u << 3) // the DC bus is past its low or high flag (vin.h): red, fast blink

// Power-up LED sweep style (settings.led_boot)
#define LED_BOOT_WHITE   0
#define LED_BOOT_RAINBOW 1

typedef struct {
    uint8_t  op;   // cmd_op_t
    uint8_t  port; // 0-based, where applicable
    uint32_t arg;
} engine_cmd_t;

// Events, core 1 -> core 0 (published to MQTT / log)
typedef enum {
    EVT_STATE_CHANGE, // code = new port_state_t, arg = old
    EVT_FAULT,        // code = PORT_FAULT_* bits, arg = INA226 alert flag (gen 2) or the blade's raw word (PORT_FAULT_ARG_GEN3)
    EVT_CONTRACT,     // code = selected PDO, arg = contract mW
    EVT_PROBE_FAIL,   // code = which probe step failed
    EVT_THROTTLE,     // code = THROTTLE_*, arg = granted/restored mW
    EVT_BOOT,         // core 0 only, fault-log record: code = boot_reason_t | core << 8, arg = pc
    EVT_CHARGE,       // code = CHARGE_*, arg: minutes since attach (DONE/RESUMED) or AUTO_OFF_*
    EVT_UPDATE,       // code = UPDATE_*, arg = the blade firmware version now running, major << 16 | minor << 8 | patch
    EVT_BUS,          // core 0 only, fault-log record: the DC bus crossed the low flag; code = BUS_*, arg = bus mV
} evt_type_t;

// EVT_BUS codes. The low flag is VIN_LOW_MV in vin.h (19 V). About a volt
// below that the backplane switches the whole bus off, controller included,
// so a fault-log record written at the flag is the only evidence of a
// brown-out that survives one.
#define BUS_LOW       1 // the bus fell under the flag
#define BUS_RECOVERED 0 // the bus came back over it
// Port number used in an event or fault-log record that is about the whole
// chassis rather than one port
#define CHASSIS_EVT_PORT 0xFF

// EVT_CHARGE codes
#define CHARGE_DONE     0 // draw stayed under settings charged_mw for charged_min: charged
#define CHARGE_RESUMED  1 // ...and then stayed above it as long: charging again
#define CHARGE_AUTO_OFF 2 // the port switched itself off; arg = AUTO_OFF_*
#define AUTO_OFF_CHARGED 0 // port_auto_off policy, once charged
#define AUTO_OFF_SLEEP   1 // port_sleep_min elapsed since the sink attached

// Per-port sleep timer ceiling, minutes (24 h)
#define PORT_SLEEP_MAX_MIN 1440

// EVT_THROTTLE codes
#define THROTTLE_CLAMPED  0 // advertisement reduced to fit the budget
#define THROTTLE_RESTORED 1 // full advertisement restored after recovery
#define THROTTLE_STEP     2 // partial step-up toward the pre-throttle ask

// EVT_UPDATE codes: what a trip through a gen-3 blade's ROM bootloader did
#define UPDATE_WRITTEN    1 // the bundled image was programmed and started
#define UPDATE_STARTED    2 // the image already there checked out and was started
#define UPDATE_BOOT_OPT   3 // the blade was told to boot through the bootloader from now on
#define UPDATE_VERSION(major, minor, patch) ((uint32_t)(major) << 16 | (uint32_t)(minor) << 8 | (patch))

typedef struct {
    uint8_t  type; // evt_type_t
    uint8_t  port;
    uint16_t code;
    uint32_t arg;
} engine_evt_t;
