#include "mpq4242_binary_sensor.h"

#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/core/component.h"
#include "esphome/core/log.h"

namespace esphome {
namespace mpq4242 {

static const char *TAG = "mpq4242.binary_sensor";

float MPQ4242BinarySensor::get_setup_priority() const { return setup_priority::DATA; }

void MPQ4242BinarySensor::setup() { ESP_LOGCONFIG(TAG, "Setting up MPQ4242BinarySensor..."); }

void MPQ4242BinarySensor::loop() {
  uint8_t fw_rev = FW_REV.get();
  uint8_t status1 = STATUS1.get();
  uint8_t status2 = STATUS2.get();
  uint8_t pdo_type = STATUS3.get() & 0b1;

  if (this->cable_5a_capable_binary_sensor_ != nullptr) {
    this->cable_5a_capable_binary_sensor_->publish_state(CHECK_BIT(fw_rev, 5));
  }
  if (this->current_mismatch_binary_sensor_ != nullptr) {
    this->current_mismatch_binary_sensor_->publish_state(CHECK_BIT(fw_rev, 7));
  }
  if (this->giveback_flag_binary_sensor_ != nullptr) {
    this->giveback_flag_binary_sensor_->publish_state(CHECK_BIT(status1, 6));
  }
  if (this->otw_threshold_1_binary_sensor_ != nullptr) {
    this->otw_threshold_1_binary_sensor_->publish_state(CHECK_BIT(status2, 5));
  }
  if (this->otw_threshold_2_binary_sensor_ != nullptr) {
    this->otw_threshold_2_binary_sensor_->publish_state(CHECK_BIT(status2, 4));
  }
  if (this->pps_mode_binary_sensor_ != nullptr) {
    this->pps_mode_binary_sensor_->publish_state(pdo_type == MPQ4242_PDO_TYPE_PPS);
  }
  if (this->sink_attached_binary_sensor_ != nullptr) {
    this->sink_attached_binary_sensor_->publish_state(CHECK_BIT(status1, 7));
  }
}

void MPQ4242BinarySensor::dump_config() {
  ESP_LOGCONFIG(TAG, "MPQ4242 Sensor:");
  LOG_BINARY_SENSOR("  ", "Cable5ACapableBinarySensor", this->cable_5a_capable_binary_sensor_);
  LOG_BINARY_SENSOR("  ", "CurrentMismatchBinarySensor", this->current_mismatch_binary_sensor_);
  LOG_BINARY_SENSOR("  ", "GivebackFlagBinarySensor", this->giveback_flag_binary_sensor_);
  LOG_BINARY_SENSOR("  ", "OtwThreshold1BinarySensor", this->otw_threshold_1_binary_sensor_);
  LOG_BINARY_SENSOR("  ", "OtwThreshold2BinarySensor", this->otw_threshold_2_binary_sensor_);
  LOG_BINARY_SENSOR("  ", "PpsModeBinarySensor", this->pps_mode_binary_sensor_);
  LOG_BINARY_SENSOR("  ", "SinkAttachedBinarySensor", this->sink_attached_binary_sensor_);
}

}  // namespace mpq4242
}  // namespace esphome
