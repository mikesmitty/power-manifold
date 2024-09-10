#pragma once

#include "../lad_component.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/core/defines.h"
#include "esphome/core/component.h"

namespace esphome {
namespace lad_component {

#define CHECK_BIT(var, pos) (((var) >> (pos)) & 1)

/// This class includes UART support for the Meanwell LAD series of battery-backed power supplies.
class LadBinarySensor : public PollingComponent {
 public:
  LadBinarySensor(LadComponent *parent) : parent_(parent) {}

  /** Sets the binary sensor indicating if A/C power has a fault. */
  void set_ac_power_fault_binary_sensor(binary_sensor::BinarySensor *binary_sensor) {
    this->ac_power_fault_binary_sensor_ = binary_sensor;
  }

  /** Sets the binary sensor indicating if the battery is charged. */
  void set_battery_charged_binary_sensor(binary_sensor::BinarySensor *binary_sensor) {
    this->battery_charged_binary_sensor_ = binary_sensor;
  }

  /** Sets the binary sensor indicating if the battery is charging. */
  void set_battery_charging_binary_sensor(binary_sensor::BinarySensor *binary_sensor) {
    this->battery_charging_binary_sensor_ = binary_sensor;
  }

  /** Sets the binary sensor indicating if the battery has an imbalanced cell. */
  void set_battery_imbalance_binary_sensor(binary_sensor::BinarySensor *binary_sensor) {
    this->battery_imbalance_binary_sensor_ = binary_sensor;
  }

  /** Sets the binary sensor indicating if battery power has a fault. */
  void set_battery_power_fault_binary_sensor(binary_sensor::BinarySensor *binary_sensor) {
    this->battery_power_fault_binary_sensor_ = binary_sensor;
  }

  /** Sets the binary sensor indicating if the battery is reversed. */
  void set_battery_reversed_binary_sensor(binary_sensor::BinarySensor *binary_sensor) {
    this->battery_reversed_binary_sensor_ = binary_sensor;
  }

  /** Sets the binary sensor indicating if the battery switch is active. */
  void set_battery_switch_binary_sensor(binary_sensor::BinarySensor *binary_sensor) {
    this->battery_switch_binary_sensor_ = binary_sensor;
  }

  /** Sets the binary sensor indicating if battery 1 has a fault. */
  void set_battery1_fault_binary_sensor(binary_sensor::BinarySensor *binary_sensor) {
    this->battery1_fault_binary_sensor_ = binary_sensor;
  }

  /** Sets the binary sensor indicating if battery 2 has a fault. */
  void set_battery2_fault_binary_sensor(binary_sensor::BinarySensor *binary_sensor) {
    this->battery2_fault_binary_sensor_ = binary_sensor;
  }

  /** Sets the binary sensor indicating if battery 3 has a fault. */
  void set_battery3_fault_binary_sensor(binary_sensor::BinarySensor *binary_sensor) {
    this->battery3_fault_binary_sensor_ = binary_sensor;
  }

  /** Sets the binary sensor indicating if battery 4 has a fault. */
  void set_battery4_fault_binary_sensor(binary_sensor::BinarySensor *binary_sensor) {
    this->battery4_fault_binary_sensor_ = binary_sensor;
  }

  /** Sets the binary sensor indicating if the force start mode is active. */
  void set_force_start_binary_sensor(binary_sensor::BinarySensor *binary_sensor) {
    this->force_start_binary_sensor_ = binary_sensor;
  }

  /** Sets the binary sensor indicating if the link control status is active. */
  void set_link_control_status_binary_sensor(binary_sensor::BinarySensor *binary_sensor) {
    this->link_control_status_binary_sensor_ = binary_sensor;
  }

  /** Sets the binary sensor indicating if the low battery protection is active. */
  void set_low_battery_binary_sensor(binary_sensor::BinarySensor *binary_sensor) {
    this->low_battery_binary_sensor_ = binary_sensor;
  }

  /** Sets the binary sensor indicating if the output overload protection is enabled. */
  void set_output_overload_binary_sensor(binary_sensor::BinarySensor *binary_sensor) {
    this->output_overload_binary_sensor_ = binary_sensor;
  }

  /** Sets the binary sensor indicating if OVP is active. */
  void set_ovp_active_binary_sensor(binary_sensor::BinarySensor *binary_sensor) {
    this->ovp_active_binary_sensor_ = binary_sensor;
  }

  /** Sets the binary sensor indicating if standby power is active. */
  void set_standby_power_active_binary_sensor(binary_sensor::BinarySensor *binary_sensor) {
    this->standby_power_active_binary_sensor_ = binary_sensor;
  }

  /** Used by ESPHome framework. */
  void dump_config() override;
  /** Used by ESPHome framework. */
  void update() override;
  /** Used by ESPHome framework. */
  void setup() override;
  /** Used by ESPHome framework. */
  float get_setup_priority() const override;

 protected:
  LadComponent *parent_;

  uint8_t lad_status_[2] = {0x10, 0x7F};  // Returns 4 bytes

  binary_sensor::BinarySensor *ac_power_fault_binary_sensor_{nullptr};
  binary_sensor::BinarySensor *battery_charged_binary_sensor_{nullptr};
  binary_sensor::BinarySensor *battery_charging_binary_sensor_{nullptr};
  binary_sensor::BinarySensor *battery_imbalance_binary_sensor_{nullptr};
  binary_sensor::BinarySensor *battery_power_fault_binary_sensor_{nullptr};
  binary_sensor::BinarySensor *battery_reversed_binary_sensor_{nullptr};
  binary_sensor::BinarySensor *battery_switch_binary_sensor_{nullptr};
  binary_sensor::BinarySensor *battery1_fault_binary_sensor_{nullptr};
  binary_sensor::BinarySensor *battery2_fault_binary_sensor_{nullptr};
  binary_sensor::BinarySensor *battery3_fault_binary_sensor_{nullptr};
  binary_sensor::BinarySensor *battery4_fault_binary_sensor_{nullptr};
  binary_sensor::BinarySensor *force_start_binary_sensor_{nullptr};
  binary_sensor::BinarySensor *link_control_status_binary_sensor_{nullptr};
  binary_sensor::BinarySensor *low_battery_binary_sensor_{nullptr};
  binary_sensor::BinarySensor *output_overload_binary_sensor_{nullptr};
  binary_sensor::BinarySensor *ovp_active_binary_sensor_{nullptr};
  binary_sensor::BinarySensor *standby_power_active_binary_sensor_{nullptr};
};

}  // namespace lad_component
}  // namespace esphome
