#include "mpq4242_sensor.h"

#include "esphome/components/i2c/i2c.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/core/component.h"
#include "esphome/core/log.h"

namespace esphome {
namespace mpq4242 {

static const char *TAG = "mpq4242.sensor";

float MPQ4242Sensor::get_setup_priority() const { return setup_priority::DATA; }

void MPQ4242Sensor::setup() { ESP_LOGCONFIG(TAG, "Setting up MPQ4242Sensor..."); }

void MPQ4242Sensor::update() {
  uint8_t selected_pdo = this->STATUS2.get() >> 1 & 0b111;
  MPQ4242Pdo pdo = this->parent_->get_pdo(selected_pdo);

  float contract_power = this->STATUS3.get() * 0.5;
  if (contract_power != this->contract_power_) {
    this->contract_power_ = contract_power;
    if (this->contract_power_sensor_ != nullptr) {
      this->contract_power_sensor_->publish_state(contract_power);
    }
  }

  float max_requested_current = this->MAX_REQ_CUR.get() * 0.02;  // 20mA units
  if (max_requested_current != this->max_requested_current_) {
    this->max_requested_current_ = max_requested_current;
    if (this->max_requested_current_sensor_ != nullptr) {
      this->max_requested_current_sensor_->publish_state(max_requested_current);
    }
  }

  uint8_t fw_rev_number = this->FW_REV.get() & 0b11111;
  if (fw_rev_number != this->fw_rev_) {
    this->fw_rev_ = fw_rev_number;
    if (this->fw_rev_sensor_ != nullptr) {
      this->fw_rev_sensor_->publish_state(fw_rev_number);
    }
  }

  uint8_t pwr_ctl2 = PWR_CTL2.get();
  uint8_t otp_threshold = pwr_ctl2 & 0b111;
  uint8_t otw_threshold_1 = pwr_ctl2 >> 3 & 0b111;

  if (otp_threshold != this->otp_threshold_) {
    this->otp_threshold_ = otp_threshold;
    if (this->otp_threshold_sensor_ != nullptr) {
      this->otp_threshold_sensor_->publish_state(this->otp_threshold_index[otp_threshold]);
    }
  }

  if (otw_threshold_1 != this->otw_threshold_1_) {
    this->otw_threshold_1_ = otw_threshold_1;
    if (this->otw_threshold_1_sensor_ != nullptr) {
      this->otw_threshold_1_sensor_->publish_state(this->otw_threshold_index[otw_threshold_1]);
    }
  }

  i2c::I2CRegister CTL_SYS16 = this->parent_->reg(MPQ4242_REGISTER_CTL_SYS16);
  uint8_t otw_threshold_2 = CTL_SYS16.get() >> 1 & 0b111;
  if (otw_threshold_2 != this->otw_threshold_2_) {
    this->otw_threshold_2_ = otw_threshold_2;
    if (this->otw_threshold_2_sensor_ != nullptr) {
      this->otw_threshold_2_sensor_->publish_state(this->otw_threshold_index[otw_threshold_2]);
    }
  }

  if (selected_pdo != this->selected_pdo_) {
    this->selected_pdo_ = selected_pdo;
    if (this->selected_pdo_sensor_ != nullptr) {
      this->selected_pdo_sensor_->publish_state(selected_pdo);
    }
    if (this->pdo_max_current_sensor_ != nullptr) {
      this->pdo_max_current_sensor_->publish_state(pdo.max_current);
    }
    if (this->pdo_min_voltage_sensor_ != nullptr) {
      this->pdo_min_voltage_sensor_->publish_state(pdo.min_voltage);
    }
    if (this->pdo_voltage_sensor_ != nullptr) {
      this->pdo_voltage_sensor_->publish_state(pdo.voltage);
    }
  }
}

void MPQ4242Sensor::dump_config() {
  ESP_LOGCONFIG(TAG, "MPQ4242 Sensor:");
  LOG_SENSOR("  ", "ContractPowerSensor", this->contract_power_sensor_);
  LOG_SENSOR("  ", "FwRevSensor", this->fw_rev_sensor_);
  LOG_SENSOR("  ", "MaxRequestedCurrentSensor", this->max_requested_current_sensor_);
  LOG_SENSOR("  ", "OtpThresholdSensor", this->otp_threshold_sensor_);
  LOG_SENSOR("  ", "OtwThreshold1Sensor", this->otw_threshold_1_sensor_);
  LOG_SENSOR("  ", "OtwThreshold2Sensor", this->otw_threshold_2_sensor_);
  LOG_SENSOR("  ", "PdoMaxCurrentSensor", this->pdo_max_current_sensor_);
  LOG_SENSOR("  ", "PdoMinVoltageSensor", this->pdo_min_voltage_sensor_);
  LOG_SENSOR("  ", "PdoVoltageSensor", this->pdo_voltage_sensor_);
  LOG_SENSOR("  ", "SelectedPdoSensor", this->selected_pdo_sensor_);
}

}  // namespace mpq4242
}  // namespace esphome
