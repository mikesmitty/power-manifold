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

typedef enum {
    BLADE_GEN_NONE = 0,
    BLADE_GEN_2 = 2,
    BLADE_GEN_3 = 3,
} blade_gen_t;

// EVT_PROBE_FAIL codes (fault_text.c names them; 5 is the engine's
// chassis-level "expander reset")
#define PROBE_FAIL_MUX     1
#define PROBE_FAIL_INA226  2
#define PROBE_FAIL_MPQ4242 3
#define PROBE_FAIL_EN      4
#define PROBE_FAIL_BLADE   6 // a gen-3 blade stopped answering or refused its configuration
#define PROBE_FAIL_NONE    7 // nothing answered on the channel

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
} blade_status_t;

// Which generation answers on the selected channel.
blade_gen_t blade_detect(void);

// Probe steps: 0 on success, else a PROBE_FAIL_* code.
// A freshly powered blade: configure it from scratch.
uint16_t blade_setup(blade_gen_t gen, uint32_t max_ma, uint32_t max_mv);
// A blade adopted powered at a warm start: read what latched while nobody
// was watching (st->fault_bits, for the caller to act on), then check its
// configuration against ours and rewrite it — re-advertising to an attached
// sink — only when it differs, so a live contract rides through untouched.
uint16_t blade_adopt(blade_gen_t gen, uint32_t max_ma, uint32_t max_mv, blade_status_t *st);

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
