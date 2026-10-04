#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "manifold.h"

// Chassis fan control, owned by the engine. Manual mode pins the fan. Auto
// mode decides from each tick's telemetry snapshot. The fan turns on when any
// one of these three rules holds, and turns off only once all three are clear:
//   - chassis power, with hysteresis: on at or above fan_on_w, off at or
//     below fan_off_w;
//   - contract current: any attached port holding a contract above fan_on_ma;
//   - blade temperature: any blade whose converter thermometer reads at or
//     above FAN_BLADE_ON_DC, clearing once every blade reads below
//     FAN_BLADE_OFF_DC.
// The current rule exists because blade heating is I²R in the shunt, the
// converter and the card-edge fingers, so it follows current rather than
// watts: a 5 V/5 A contract is only 25 W of chassis load. The temperature
// rule covers what load cannot predict, such as a hot room, a blocked intake
// or one blade running hot on its own. Only gen-3 blades report a
// temperature; a port without a reading (a gen-2 blade, an empty slot or an
// open thermistor) simply does not take part in that rule. Any change of
// state starts a hold so that a borderline load cannot flap the fan.
//
// The two temperatures are fixed, not settings, and sit well under the
// blade's own 100 °C converter trip. They are provisional until the gen-3
// blades have been measured in the closed chassis.

#define FAN_MIN_HOLD_MS (30 * 1000)
#define FAN_BLADE_ON_DC  650 // converter thermometer, tenths of a degree C
#define FAN_BLADE_OFF_DC 550

void fan_policy_init(bool auto_mode, bool fan_is_on); // fan_is_on: the expander bit found at start
void fan_policy_set_manual(bool on); // drops out of auto
void fan_policy_set_auto(void);      // policy acts on the next tick
void fan_policy_tick(const telemetry_t *t, uint32_t now_ms);
bool fan_policy_on(void);
bool fan_policy_auto(void);

// Contract current a port's reservation implies at its bus voltage, in mA;
// 0 when no sink is attached (an idle port's base reserve is not a contract).
uint32_t fan_policy_contract_ma(const port_telemetry_t *p);
