#include "pwrman_charger_switch.h"
#include "esphome/core/log.h"

namespace esphome {
namespace pwrman_charger {

static const char *const TAG = "pwrman_charger.switch";

float PwrmanChargerSwitch::get_setup_priority() const { return setup_priority::DATA; }
void PwrmanChargerSwitch::setup() {
  ESP_LOGCONFIG(TAG, "Setting up PwrmanCharger Switch...");

  if (this->get_initial_state_with_restore_mode().has_value()) {
    ESP_LOGCONFIG(TAG, "Restoring initial state: %d", this->get_initial_state_with_restore_mode().value());  // FIXME
    this->enabled_ = this->get_initial_state_with_restore_mode().value();
  } else {
    this->parent_->read_register(PWRMAN_CHARGER_REGISTER_ENABLE, &this->enabled_, 1);
  }
  if (this->enabled_) {
    this->turn_on();
  } else {
    this->turn_off();
  }
}
void PwrmanChargerSwitch::dump_config() {
  ESP_LOGCONFIG(TAG, "CANARY: PwrmanChargerSwitch");  // FIXME
  LOG_SWITCH(TAG, "PwrmanCharger Switch", this);
}
void PwrmanChargerSwitch::update() {
  this->parent_->read_register(PWRMAN_CHARGER_REGISTER_ENABLE, &this->enabled_, 1);
  if (this->enabled_) {
    this->turn_on();
  } else {
    this->turn_off();
  }
  /*
  uint8_t enabled;
  this->parent_->read_register(PWRMAN_CHARGER_REGISTER_ENABLE, &enabled, 1);
  if (enabled != this->enabled_) {
    this->enabled_ = enabled;
    this->publish_state(enabled);
  }
  */
}
void PwrmanChargerSwitch::write_state(bool state) {
  this->enabled_ = static_cast<uint8_t>(state);
  this->parent_->write_register(PWRMAN_CHARGER_REGISTER_ENABLE, &this->enabled_, 1);
  this->publish_state(state);
}

}  // namespace pwrman_charger
}  // namespace esphome