#include "lad_binary_sensor.h"

#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/core/component.h"
#include "esphome/core/log.h"

namespace esphome {
namespace lad_component {

static const char *TAG = "lad.binary_sensor";

float LadBinarySensor::get_setup_priority() const { return setup_priority::DATA; }

void LadBinarySensor::setup() { ESP_LOGCONFIG(TAG, "Setting up LadBinarySensor..."); }

void LadBinarySensor::update() {
  // Response is 5 bytes plus the length of the data
  // R/W | LEN | CMD 2 bytes | DATA | CRC
  uint8_t data[9] = {0};

  if (!this->parent_->read_register_(this->lad_status_, data, 9)) {
    ESP_LOGE(TAG, "Failed to read LAD status register");
  }

  uint8_t battery_switch_status = data[5];
  uint8_t status_high = data[6];
  uint8_t status_low = data[7];

  if (this->ac_power_fault_binary_sensor_ != nullptr) {
    // AC power fault status is 0 when AC power is faulting
    this->ac_power_fault_binary_sensor_->publish_state(CHECK_BIT(status_low, 0) == 0);
  }
  if (this->battery_charged_binary_sensor_ != nullptr) {
    this->battery_charged_binary_sensor_->publish_state(CHECK_BIT(status_high, 4));
  }
  if (this->battery_charging_binary_sensor_ != nullptr) {
    this->battery_charging_binary_sensor_->publish_state(CHECK_BIT(status_high, 5));
  }
  if (this->battery_imbalance_binary_sensor_ != nullptr) {
    this->battery_imbalance_binary_sensor_->publish_state(CHECK_BIT(status_low, 7));
  }
  if (this->battery_power_fault_binary_sensor_ != nullptr) {
    this->battery_power_fault_binary_sensor_->publish_state(CHECK_BIT(status_low, 1));
  }
  if (this->battery_reversed_binary_sensor_ != nullptr) {
    this->battery_reversed_binary_sensor_->publish_state(CHECK_BIT(status_high, 6));
  }
  if (this->battery_switch_binary_sensor_ != nullptr) {
    // Battery switch status is 0 when the battery is online
    this->battery_switch_binary_sensor_->publish_state(battery_switch_status == 0);
  }
  if (this->battery1_fault_binary_sensor_ != nullptr) {
    this->battery1_fault_binary_sensor_->publish_state(CHECK_BIT(status_high, 0));
  }
  if (this->battery2_fault_binary_sensor_ != nullptr) {
    this->battery2_fault_binary_sensor_->publish_state(CHECK_BIT(status_high, 1));
  }
  if (this->battery3_fault_binary_sensor_ != nullptr) {
    this->battery3_fault_binary_sensor_->publish_state(CHECK_BIT(status_high, 2));
  }
  if (this->battery4_fault_binary_sensor_ != nullptr) {
    this->battery4_fault_binary_sensor_->publish_state(CHECK_BIT(status_high, 3));
  }
  if (this->force_start_binary_sensor_ != nullptr) {
    this->force_start_binary_sensor_->publish_state(CHECK_BIT(status_high, 7));
  }
  if (this->link_control_status_binary_sensor_ != nullptr) {
    this->link_control_status_binary_sensor_->publish_state(CHECK_BIT(status_low, 5));
  }
  if (this->low_battery_binary_sensor_ != nullptr) {
    this->low_battery_binary_sensor_->publish_state(CHECK_BIT(status_low, 3));
  }
  if (this->output_overload_binary_sensor_ != nullptr) {
    this->output_overload_binary_sensor_->publish_state(CHECK_BIT(status_low, 2));
  }
  if (this->ovp_active_binary_sensor_ != nullptr) {
    this->ovp_active_binary_sensor_->publish_state(CHECK_BIT(status_low, 6));
  }
  if (this->standby_power_active_binary_sensor_ != nullptr) {
    this->standby_power_active_binary_sensor_->publish_state(CHECK_BIT(status_low, 4));
  }
}

void LadBinarySensor::dump_config() {
  ESP_LOGCONFIG(TAG, "LAD Sensor:");
  LOG_BINARY_SENSOR("  ", "ACPowerFaultBinarySensor", this->ac_power_fault_binary_sensor_);
  LOG_BINARY_SENSOR("  ", "BatteryChargedBinarySensor", this->battery_charged_binary_sensor_);
  LOG_BINARY_SENSOR("  ", "BatteryChargingBinarySensor", this->battery_charging_binary_sensor_);
  LOG_BINARY_SENSOR("  ", "BatteryImbalanceBinarySensor", this->battery_imbalance_binary_sensor_);
  LOG_BINARY_SENSOR("  ", "BatteryPowerFaultBinarySensor", this->battery_power_fault_binary_sensor_);
  LOG_BINARY_SENSOR("  ", "BatteryReversedBinarySensor", this->battery_reversed_binary_sensor_);
  LOG_BINARY_SENSOR("  ", "BatterySwitchBinarySensor", this->battery_switch_binary_sensor_);
  LOG_BINARY_SENSOR("  ", "Battery1FaultBinarySensor", this->battery1_fault_binary_sensor_);
  LOG_BINARY_SENSOR("  ", "Battery2FaultBinarySensor", this->battery2_fault_binary_sensor_);
  LOG_BINARY_SENSOR("  ", "Battery3FaultBinarySensor", this->battery3_fault_binary_sensor_);
  LOG_BINARY_SENSOR("  ", "Battery4FaultBinarySensor", this->battery4_fault_binary_sensor_);
  LOG_BINARY_SENSOR("  ", "ForceStartBinarySensor", this->force_start_binary_sensor_);
  LOG_BINARY_SENSOR("  ", "LinkControlStatusBinarySensor", this->link_control_status_binary_sensor_);
  LOG_BINARY_SENSOR("  ", "LowBatteryBinarySensor", this->low_battery_binary_sensor_);
  LOG_BINARY_SENSOR("  ", "OutputOverloadBinarySensor", this->output_overload_binary_sensor_);
  LOG_BINARY_SENSOR("  ", "OvpActiveBinarySensor", this->ovp_active_binary_sensor_);
  LOG_BINARY_SENSOR("  ", "StandbyPowerActiveBinarySensor", this->standby_power_active_binary_sensor_);
}

}  // namespace lad_component
}  // namespace esphome
