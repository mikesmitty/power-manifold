#include "pwrman_charger_sensor.h"

#include "esphome/components/i2c/i2c.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/core/component.h"
#include "esphome/core/log.h"

namespace esphome {
namespace pwrman_charger {

static const char *TAG = "pwrman_charger.sensor";

float PwrmanChargerSensor::get_setup_priority() const { return setup_priority::DATA; }

void PwrmanChargerSensor::setup() { ESP_LOGCONFIG(TAG, "Setting up PwrmanChargerSensor..."); }

void PwrmanChargerSensor::update() {
  // Get 8-bit value from CONTRACT_POWER register and convert from 0.5W units to W
  if (this->contract_power_sensor_ != nullptr) {
    this->parent_->read_register(PWRMAN_CHARGER_REGISTER_CONTRACT_POWER, this->resp_bytes_, 1);
    this->contract_power_sensor_->publish_state(this->resp_bytes_[0] * 0.5);
  }

  // Get 16-bit value from IOUT register and convert from 10mA units to A
  if (this->output_current_sensor_ != nullptr) {
    this->parent_->read_register(PWRMAN_CHARGER_REGISTER_IOUT, this->resp_bytes_, 2);
    this->output_current_sensor_->publish_state((this->resp_bytes_[0] << 8 | this->resp_bytes_[1]) * 0.01);
  }

  // Get 16-bit value from VOUT register and convert from 10mV units to V
  if (this->output_voltage_sensor_ != nullptr) {
    this->parent_->read_register(PWRMAN_CHARGER_REGISTER_VOUT, this->resp_bytes_, 2);
    this->output_voltage_sensor_->publish_state((this->resp_bytes_[0] << 8 | this->resp_bytes_[1]) * 0.01);
  }

  // Get 16-bit value from VIN register and convert from 10mV units to V
  if (this->input_voltage_sensor_ != nullptr) {
    this->parent_->read_register(PWRMAN_CHARGER_REGISTER_VIN, this->resp_bytes_, 2);
    this->input_voltage_sensor_->publish_state((this->resp_bytes_[0] << 8 | this->resp_bytes_[1]) * 0.01);
  }

  // Check if the selected PDO has changed
  this->parent_->read_register(PWRMAN_CHARGER_REGISTER_CUR_PDO_NUM, this->resp_bytes_, 1);
  uint8_t selected_pdo = this->resp_bytes_[0];

  if (selected_pdo != this->selected_pdo_) {
    this->selected_pdo_ = selected_pdo;

    if (selected_pdo == 0) {
      if (this->pdo_max_current_sensor_ != nullptr) {
        this->pdo_max_current_sensor_->publish_state(NAN);
      }
      if (this->pdo_min_voltage_sensor_ != nullptr) {
        this->pdo_min_voltage_sensor_->publish_state(NAN);
      }
      if (this->pdo_voltage_sensor_ != nullptr) {
        this->pdo_voltage_sensor_->publish_state(NAN);
      }
    }

    // Get current PDO's max current in 50mA units
    if (this->pdo_max_current_sensor_ != nullptr && selected_pdo != 0) {
      this->parent_->read_register(PWRMAN_CHARGER_REGISTER_CUR_PDO_AMP, this->resp_bytes_, 1);
      this->pdo_max_current_sensor_->publish_state(this->resp_bytes_[0] * 0.05);
    }
    // Get current PDO's min voltage in 100mV units (PPS only)
    if (this->pdo_min_voltage_sensor_ != nullptr && selected_pdo != 0) {
      this->parent_->read_register(PWRMAN_CHARGER_REGISTER_CUR_PDO_MIN_VOLT, this->resp_bytes_, 1);
      this->pdo_min_voltage_sensor_->publish_state(this->resp_bytes_[0] * 0.1);
    }
    // Get current PDO's voltage in 100mV units
    if (this->pdo_voltage_sensor_ != nullptr && selected_pdo != 0) {
      this->parent_->read_register(PWRMAN_CHARGER_REGISTER_CUR_PDO_VOLT, this->resp_bytes_, 1);
      this->pdo_voltage_sensor_->publish_state(this->resp_bytes_[0] * 0.1);
    }
  }
}

void PwrmanChargerSensor::dump_config() {
  ESP_LOGCONFIG(TAG, "pwrman_charger Sensor:");
  LOG_SENSOR("  ", "ContractPowerSensor", this->contract_power_sensor_);
  LOG_SENSOR("  ", "InputVoltageSensor", this->input_voltage_sensor_);
  LOG_SENSOR("  ", "OutputCurrentSensor", this->output_current_sensor_);
  LOG_SENSOR("  ", "OutputVoltageSensor", this->output_voltage_sensor_);
  LOG_SENSOR("  ", "PdoMaxCurrentSensor", this->pdo_max_current_sensor_);
  LOG_SENSOR("  ", "PdoMinVoltageSensor", this->pdo_min_voltage_sensor_);
  LOG_SENSOR("  ", "PdoVoltageSensor", this->pdo_voltage_sensor_);
}

}  // namespace pwrman_charger
}  // namespace esphome
