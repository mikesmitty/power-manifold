#pragma once

#include "../pwrman_charger.h"
#include "esphome/core/component.h"
#include "esphome/components/switch/switch.h"

namespace esphome {
namespace pwrman_charger {

class PwrmanChargerSwitch : public switch_::Switch, public PollingComponent {
 public:
  void set_parent(PwrmanCharger *parent) { this->parent_ = parent; }

  /** Used by ESPHome framework. */
  float get_setup_priority() const override;
  /** Used by ESPHome framework. */
  void setup() override;
  /** Used by ESPHome framework. */
  void update() override;
  /** Used by ESPHome framework. */
  void dump_config() override;

 protected:
  PwrmanCharger *parent_;
  uint8_t enabled_;

  void write_state(bool state) override;
};

}  // namespace pwrman_charger
}  // namespace esphome