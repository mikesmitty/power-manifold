#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "manifold.h"

// Simulated backplane and blades implementing the engine's driver APIs
// (tca9548a / tca9539 / ina226 / mpq4242 / blade3) behind the same headers,
// each slot a gen-2 (default) or gen-3 blade. Linked in
// place of the real drivers by the host-side test build and by the
// FAKE_BLADES firmware build, so the whole management plane runs with no
// backplane attached. All state is synchronous with the driver calls the
// engine already makes; only the engine's thread may touch it — same
// ownership rule as the real bus.

void sim_reset(void); // all slots absent, faults clear, mux deselected

// blade seating and sink behavior
void sim_set_present(uint8_t slot, bool present);
void sim_set_gen(uint8_t slot, uint8_t gen);  // 2 (default) or 3; set before seating
void sim_blade_restart(uint8_t slot);         // gen 3: the blade's MCU restarts, configuration gone
void sim_attach(uint8_t slot, uint16_t req_mv, uint32_t req_ma); // sink plugs in
void sim_detach(uint8_t slot);

// fault injection
void sim_set_mpq_fault(uint8_t slot, uint8_t fault_bits); // gen 2: sticky until cleared
void sim_set_blade_fault(uint8_t slot, uint16_t fault_bits); // gen 3: BLADE_FAULT_* latched now, and held as a condition (re-latches on clear) until set to 0
void sim_trip_ocp(uint8_t slot);   // gen 2: latched INA226 alert, clears on read (LEN); gen 3: the VBUS switch's OCP latch
bool sim_alert_asserted(void);     // GLOBAL_ALERT# wire-OR equivalent

// scripted infrastructure failures
void sim_set_probe_ok(uint8_t slot, bool ina_ok, bool mpq_ok); // gen 2
void sim_set_blade_ok(uint8_t slot, bool ok);                  // gen 3: the register file answers
void sim_set_mux_fail(bool fail);      // tca9548a_select() fails until hw reset
void sim_set_expander_fail(bool fail); // tca9539 I/O fails until hw reset

// telemetry shaping: measured draw as a percentage of the contract current
void sim_set_load_pct(uint8_t slot, uint8_t pct);
// gen 3: pin the converter and receptacle thermometers (0.1 degC, PORT_TEMP_NONE
// = open NTC) instead of the load-driven model; sim_model_temps() lets it go
void sim_set_temps(uint8_t slot, int16_t conv_dc, int16_t plug_dc);
void sim_model_temps(uint8_t slot);

// inspection hooks for tests
bool     sim_en(uint8_t slot);
bool     sim_fan(void);
uint32_t sim_advertised_ma(uint8_t slot); // last programmed PDO ceiling
uint16_t sim_advertised_mv(uint8_t slot); // last programmed voltage cap
uint32_t sim_contract_mw(uint8_t slot);   // live contract (gen 2: STATUS3 rounding)
bool     sim_blade_port_en(uint8_t slot); // gen 3: BLADE_CTL_PORT_EN as written
uint16_t sim_blade_faults(uint8_t slot);  // gen 3: the latched BLADE_FAULT_* word
uint32_t sim_blade_clear_count(uint8_t slot); // gen 3: BLADE_CMD_CLEAR_FAULTS received
uint32_t sim_blade_config_writes(uint8_t slot); // gen 3: configuration transfers received
uint32_t sim_ina_alert_ma(uint8_t slot);  // programmed over-current threshold
uint32_t sim_src_cap_count(uint8_t slot);
uint32_t sim_hard_reset_count(uint8_t slot);
uint32_t sim_mux_reset_count(void);
uint32_t sim_en_change_count(uint8_t slot); // times EN actually switched
