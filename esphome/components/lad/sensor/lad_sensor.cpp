#include "lad_sensor.h"

#include "esphome/components/sensor/sensor.h"
#include "esphome/components/uart/uart.h"
#include "esphome/core/component.h"
#include "esphome/core/log.h"

namespace esphome {
namespace lad_component {

static const char *TAG = "lad_component.sensor";

float LadSensor::get_setup_priority() const { return setup_priority::DATA; }

void LadSensor::setup() { ESP_LOGCONFIG(TAG, "Setting up LadSensor..."); }

void LadSensor::update() {
  // Response is 5 bytes plus the length of the data. Largest response is 8 bytes
  // R/W | LEN | CMD 2 bytes | DATA | CRC
  uint8_t data[13] = {0};

  // Read the current register and convert from 10 mA units to A
  if (this->current_sensor_ != nullptr && this->parent_->read_register_(this->current_, data, 7)) {
    this->current_sensor_->publish_state((data[4] << 8 | data[5]) * 0.01);
  }

  // Read the input voltage register and convert from 100mV units to V
  if (this->input_voltage_sensor_ != nullptr && this->parent_->read_register_(this->input_voltage_, data, 7)) {
    this->input_voltage_sensor_->publish_state((data[4] << 8 | data[5]) * 0.1);
  }

  // Read the battery voltage register and convert from 10mV units to V
  if (this->battery_voltage_sensor_ != nullptr && this->parent_->read_register_(this->battery_voltage_, data, 7)) {
    this->battery_voltage_sensor_->publish_state((data[4] << 8 | data[5]) * 0.01);
  }

  // Read the cell voltage registers and convert from 10mV units to V
  if (this->battery_1_voltage_sensor_ != nullptr || this->battery_2_voltage_sensor_ != nullptr ||
      this->battery_3_voltage_sensor_ != nullptr || this->battery_4_voltage_sensor_ != nullptr) {
    if (this->parent_->read_register_(this->cell_voltage_, data, 13)) {
      if (this->battery_1_voltage_sensor_ != nullptr) {
        this->battery_1_voltage_sensor_->publish_state((data[4] << 8 | data[5]) * 0.01);
      }
      if (this->battery_2_voltage_sensor_ != nullptr) {
        this->battery_2_voltage_sensor_->publish_state((data[6] << 8 | data[7]) * 0.01);
      }
      if (this->battery_3_voltage_sensor_ != nullptr) {
        this->battery_3_voltage_sensor_->publish_state((data[8] << 8 | data[9]) * 0.01);
      }
      if (this->battery_4_voltage_sensor_ != nullptr) {
        this->battery_4_voltage_sensor_->publish_state((data[10] << 8 | data[11]) * 0.01);
      }
    }
  }
}

void LadSensor::dump_config() {
  ESP_LOGCONFIG(TAG, "LadSensor:");
  LOG_SENSOR("  ", "InputVoltageSensor", this->input_voltage_sensor_);
  LOG_SENSOR("  ", "CurrentSensor", this->current_sensor_);
  LOG_SENSOR("  ", "BatteryVoltageSensor", this->battery_voltage_sensor_);
  LOG_SENSOR("  ", "Battery1VoltageSensor", this->battery_1_voltage_sensor_);
  LOG_SENSOR("  ", "Battery2VoltageSensor", this->battery_2_voltage_sensor_);
  LOG_SENSOR("  ", "Battery3VoltageSensor", this->battery_3_voltage_sensor_);
  LOG_SENSOR("  ", "Battery4VoltageSensor", this->battery_4_voltage_sensor_);
}

}  // namespace lad_component
}  // namespace esphome
