#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "manifold.h"

// The two blade generations behind one interface for the port state
// machine. Every call assumes the port's mux channel is selected.
//
//   gen 2: MPQ4242 + INA226 (mpq4242.h, ina226.h). The part negotiates on
//          its own; the controller writes its PDO table and reads back.
//   gen 3: an STM32G071 running the port (blade3.h). The controller hands
//          it limits through its register file and reads a snapshot.
//   loader: a gen-3 blade whose MCU is in its ROM bootloader (stboot.h) —
//          blank, told to go there, or set to boot through it — waiting to
//          be checked, programmed and started (blade_update.h).

typedef enum {
    BLADE_GEN_NONE = 0,
    BLADE_GEN_2 = 2,
    BLADE_GEN_3 = 3,
    BLADE_GEN_LOADER = 4,
} blade_gen_t;

// What a port's telemetry calls the generation: a blade in its bootloader is a gen-3 blade
static inline uint8_t blade_gen_number(blade_gen_t g) { return g == BLADE_GEN_LOADER ? 3 : (uint8_t)g; }

// EVT_PROBE_FAIL codes (fault_text.c names them; 5 is the engine's
// chassis-level "expander reset")
#define PROBE_FAIL_MUX     1
#define PROBE_FAIL_INA226  2
#define PROBE_FAIL_MPQ4242 3
#define PROBE_FAIL_EN      4
#define PROBE_FAIL_BLADE   6 // a gen-3 blade stopped answering or refused its configuration
#define PROBE_FAIL_NONE    7 // nothing answered on the channel
#define PROBE_FAIL_UPDATE  8 // a trip through the ROM bootloader failed; the arg says how (UPDATE_FAIL_*)
#define PROBE_FAIL_SILENT  9 // a powered blade stopped answering; it is left powered and polled on

typedef struct {
    bool     attached;
    bool     configured;   // gen 3: the blade still holds what it was given (a restart clears it); gen 2: always
    uint8_t  selected_pdo; // 1-7, 0 = none
    uint16_t fault_bits;   // PORT_FAULT_*, 0 while nothing is latched
    uint32_t fault_detail; // for EVT_FAULT's arg: the INA226 trip flag (gen 2), the raw blade word under PORT_FAULT_ARG_GEN3 (gen 3)
    uint32_t contract_mw;
    uint16_t bus_mv;
    int32_t  current_ma;
    uint32_t power_mw;
    // PD hard resets since the blade's MCU started, each count wrapping
    // (gen 3; a gen-2 blade has no counters and leaves them 0)
    uint8_t  hr_sent;     // the blade gave up on the sink
    uint8_t  hr_received; // the sink asked for one
    // Temperatures, 0.1 degC, valid once a gen-3 blade has been polled (a
    // gen-2 blade has no thermometer the controller can read). PORT_TEMP_NONE
    // is what the blade itself reports for an open or shorted NTC.
    bool     has_temps;
    int16_t  temp_conv_dc; // converter
    int16_t  temp_plug_dc; // receptacle
    int16_t  temp_mcu_dc;
} blade_status_t;

// Which generation answers on the selected channel.
blade_gen_t blade_detect(void);

// A gen-3 blade's identity block: firmware version, protocol, boot option.
typedef struct {
    uint8_t proto;
    uint8_t major, minor, patch;
    uint8_t boot; // BLADE_BOOT_*
} blade_identity_t;
bool blade_identity(blade_gen_t gen, blade_identity_t *id);
// Send a gen-3 blade to its ROM bootloader: a reset, or first the
// programming of its option bytes so every reset lands there (a PROTO 2
// blade; an older one ignores the command). The port goes dark with it, so
// the caller picks a moment when nothing is plugged in, or has been told to.
bool blade_request_loader(blade_gen_t gen, bool boot_option);

// Probe steps: 0 on success, else a PROBE_FAIL_* code.
// A freshly powered blade: configure it from scratch.
uint16_t blade_setup(blade_gen_t gen, uint32_t max_ma, uint32_t max_mv);
// A blade adopted powered at a warm start: read what latched while the
// controller was down (st->fault_bits, for the caller to act on), then check
// its configuration against ours and rewrite it — re-advertising to an
// attached sink — only when it differs, so a live contract is not interrupted.
uint16_t blade_adopt(blade_gen_t gen, uint32_t max_ma, uint32_t max_mv, blade_status_t *st);
// The limits a gen-3 blade holds while a device is on its port and the
// blade still has the configuration it was given: what a restarted
// controller leaves alone on a protected port. False otherwise, and always
// for a gen-2 blade, whose limits are not read back.
bool blade_running_limits(blade_gen_t gen, uint32_t *ma, uint32_t *mv);

// Telemetry, contract and faults. `alert` also reads the latch only the
// alert sweep clears (a gen-2 blade's INA226). False: the blade did not
// answer; st keeps its previous values.
bool blade_poll(blade_gen_t gen, bool alert, blade_status_t *st);

// The advertised current ceiling and voltage cap. A gen-3 blade
// re-advertises on its own when its table changes; a gen-2 part has to be
// asked, which `readvertise` does.
bool blade_set_limits(blade_gen_t gen, uint32_t ma, uint32_t mv, bool readvertise);
bool blade_send_src_cap(blade_gen_t gen);
bool blade_hard_reset(blade_gen_t gen);

// After the port was switched off for a fault. A gen-3 blade keeps the
// fault latched, ALERT# low with it, until told the controller has seen it;
// a gen-2 blade lost its flags with EN.
void blade_faulted(blade_gen_t gen);
