#pragma once

#include <stdbool.h>
#include <stdint.h>

#define FW_VERSION      "0.2.0" // x-release-please-version
#define NUM_PORTS       6

// Per-port supervisory state. Differs from the spec's table in one way: the
// MPQ4242 negotiates PD contracts autonomously, so there is no in-line
// "NEGOTIATING" state — the engine constrains the advertised PDO set ahead of
// time and reacts to contract changes it observes via STATUS2/STATUS3.
typedef enum {
    PORT_STATE_ABSENT = 0,  // PRES# high: no blade in slot
    PORT_STATE_PROBE,       // blade seated, probing/configuring INA226 + MPQ4242
    PORT_STATE_IDLE,        // powered, advertising, no sink attached
    PORT_STATE_ACTIVE,      // sink attached, contract in place
    PORT_STATE_THROTTLED,   // active with a budget-restricted PDO set
    PORT_STATE_FAULT,       // latched fault, EN low, cooldown running
    PORT_STATE_DISABLED,    // administratively off
    PORT_STATE_COUNT,
} port_state_t;

const char *port_state_name(port_state_t s);

// MPQ4242 fault bits (mirrors the V1 register-map encoding)
#define MPQ_FAULT_GENERAL      (1u << 0)
#define MPQ_FAULT_OTW1         (1u << 1)
#define MPQ_FAULT_OTW2         (1u << 2)
#define MPQ_FAULT_NTC1         (1u << 3)
#define MPQ_FAULT_NTC2         (1u << 4)
#define MPQ_FAULT_CC           (1u << 5)
#define MPQ_FAULT_SHORT_VBATT  (1u << 6)
#define MPQ_FAULT_VBATT_LOW    (1u << 7)

typedef struct {
    uint8_t  state;         // port_state_t
    bool     attached;
    uint8_t  selected_pdo;  // 1-7, 0 = none
    uint8_t  fault_bits;    // MPQ_FAULT_* accumulated since last clear
    uint16_t bus_mv;        // INA226 bus voltage
    int32_t  current_ma;    // INA226 current (signed)
    uint32_t power_mw;      // INA226 power
    uint32_t contract_mw;   // budget reservation held by this port
    uint32_t energy_mwh;    // delivered since boot (not persisted)
} port_telemetry_t;

typedef struct {
    port_telemetry_t port[NUM_PORTS];
    uint32_t total_mw;      // sum of measured port power
    uint32_t reserved_mw;   // sum of budget reservations
    uint32_t budget_mw;     // chassis budget in force
    uint32_t energy_mwh;    // chassis total delivered since boot
    bool     fan_on;
    bool     fan_auto;      // fan under the engine's auto policy
    bool     alert_active;  // GLOBAL_ALERT# currently asserted
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
} cmd_op_t;

typedef struct {
    uint8_t  op;   // cmd_op_t
    uint8_t  port; // 0-based, where applicable
    uint32_t arg;
} engine_cmd_t;

// Events, core 1 -> core 0 (published to MQTT / log)
typedef enum {
    EVT_STATE_CHANGE, // code = new port_state_t, arg = old
    EVT_FAULT,        // code = MPQ_FAULT_* bits, arg = INA226 alert flag
    EVT_CONTRACT,     // code = selected PDO, arg = contract mW
    EVT_PROBE_FAIL,   // code = which probe step failed
    EVT_THROTTLE,     // code = THROTTLE_*, arg = granted/restored mW
} evt_type_t;

// EVT_THROTTLE codes
#define THROTTLE_CLAMPED  0 // advertisement reduced to fit the budget
#define THROTTLE_RESTORED 1 // full advertisement restored after recovery
#define THROTTLE_STEP     2 // partial step-up toward the pre-throttle ask

typedef struct {
    uint8_t  type; // evt_type_t
    uint8_t  port;
    uint16_t code;
    uint32_t arg;
} engine_evt_t;
