#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "manifold.h"

// Chassis fan control, owned by the engine. Manual mode pins the fan; auto
// mode decides from each tick's telemetry snapshot, turning the fan on when
// EITHER of these holds and off only once BOTH are clear:
//   - chassis power hysteresis: on at >= fan_on_w, off at <= fan_off_w;
//   - any attached port holding a contract above fan_on_ma.
// The current rule catches what the power rule can't: blade heating is I²R
// in the shunt, the buck-boost stage and the card-edge fingers, so it tracks
// current, not watts — a 5V/5A contract is only 25W of chassis load. No
// temperature sensor exists anywhere in the chassis (the MPQ4242 exposes
// die-warning flags, not a reading), so contract current is the best
// available proxy. Any state change starts a hold so borderline loads can't
// flap the fan.

#define FAN_MIN_HOLD_MS (30 * 1000)

void fan_policy_init(bool auto_mode);
void fan_policy_set_manual(bool on); // drops out of auto
void fan_policy_set_auto(void);      // policy acts on the next tick
void fan_policy_tick(const telemetry_t *t, uint32_t now_ms);
bool fan_policy_on(void);
bool fan_policy_auto(void);

// Contract current a port's reservation implies at its bus voltage, in mA;
// 0 when no sink is attached (an idle port's base reserve is not a contract).
uint32_t fan_policy_contract_ma(const port_telemetry_t *p);
