#include "pwrman_charger_binary_sensor.h"

#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/core/component.h"
#include "esphome/core/log.h"

namespace esphome {
namespace pwrman_charger {

static const char *TAG = "pwrman_charger.binary_sensor";

float PwrmanChargerBinarySensor::get_setup_priority() const { return setup_priority::DATA; }

void PwrmanChargerBinarySensor::setup() { ESP_LOGCONFIG(TAG, "Setting up PwrmanChargerBinarySensor..."); }

void PwrmanChargerBinarySensor::update() {
  uint8_t fault_state = 0;
  this->parent_->read_register(PWRMAN_CHARGER_REGISTER_FAULT, &fault_state, 1);

  if (this->cable_5a_capable_binary_sensor_ != nullptr) {
    this->parent_->read_register(PWRMAN_CHARGER_REGISTER_CABLE_5A_FLAG, this->resp_bytes_, 1);
    this->cable_5a_capable_binary_sensor_->publish_state(this->resp_bytes_[0]);
  }

  if (this->otw_threshold_1_binary_sensor_ != nullptr) {
    this->otw_threshold_1_binary_sensor_->publish_state(CHECK_BIT(fault_state, 1));
  }

  if (this->otw_threshold_2_binary_sensor_ != nullptr) {
    this->otw_threshold_2_binary_sensor_->publish_state(CHECK_BIT(fault_state, 2));
  }

  if (this->pps_mode_binary_sensor_ != nullptr) {
    this->parent_->read_register(PWRMAN_CHARGER_REGISTER_CUR_PDO_MIN_VOLT, this->resp_bytes_, 1);
    this->pps_mode_binary_sensor_->publish_state(this->resp_bytes_[0] != 0);
  }

  if (this->sink_attached_binary_sensor_ != nullptr) {
    this->parent_->read_register(PWRMAN_CHARGER_REGISTER_SINK_ATTACHED, this->resp_bytes_, 1);
    this->sink_attached_binary_sensor_->publish_state(this->resp_bytes_[0]);
  }
}

void PwrmanChargerBinarySensor::dump_config() {
  ESP_LOGCONFIG(TAG, "PwrmanCharger Sensor:");
  LOG_BINARY_SENSOR("  ", "Cable5ACapableBinarySensor", this->cable_5a_capable_binary_sensor_);
  LOG_BINARY_SENSOR("  ", "OtwThreshold1BinarySensor", this->otw_threshold_1_binary_sensor_);
  LOG_BINARY_SENSOR("  ", "OtwThreshold2BinarySensor", this->otw_threshold_2_binary_sensor_);
  LOG_BINARY_SENSOR("  ", "PpsModeBinarySensor", this->pps_mode_binary_sensor_);
  LOG_BINARY_SENSOR("  ", "SinkAttachedBinarySensor", this->sink_attached_binary_sensor_);
}

}  // namespace pwrman_charger
}  // namespace esphome
