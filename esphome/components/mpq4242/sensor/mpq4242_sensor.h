#pragma once

#include "../mpq4242.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/core/defines.h"
#include "esphome/core/component.h"

namespace esphome {
namespace mpq4242 {

// FIXME: Update this description for the sensor
/// This class includes i2c support for the MPQ4242 USB PD controller.
/// The device has 7 configurable PDOs, 2 GPIOs with several functions
/// and is capable of up to 100W charging. This class is for the
/// MPQ4242 configuration.
class MPQ4242Sensor : public PollingComponent {
 public:
  MPQ4242Sensor(MPQ4242Component *parent) : parent_(parent) {}

  /** Sets the sensor that will report the contract power negotiated with the sink. */
  void set_contract_power_sensor(sensor::Sensor *sensor) { this->contract_power_sensor_ = sensor; }

  /** Sets the sensor that will report the firmware revision of the sink. */
  void set_fw_rev_sensor(sensor::Sensor *sensor) { this->fw_rev_sensor_ = sensor; }

  /** Sets the sensor that will report the current requested by the sink. */
  void set_max_requested_current_sensor(sensor::Sensor *sensor) { this->max_requested_current_sensor_ = sensor; }

  /** Sets the sensor that will report the otp threshold temperature. */
  void set_otp_threshold_sensor(sensor::Sensor *sensor) { this->otp_threshold_sensor_ = sensor; }

  /** Sets the sensor that will report the otw threshold 1 temperature. */
  void set_otw_threshold_1_sensor(sensor::Sensor *sensor) { this->otw_threshold_1_sensor_ = sensor; }

  /** Sets the sensor that will report the otw threshold 2 temperature. */
  void set_otw_threshold_2_sensor(sensor::Sensor *sensor) { this->otw_threshold_2_sensor_ = sensor; }

  /** Sets the sensor that will report the selected PDO's configured max current. */
  void set_pdo_max_current_sensor(sensor::Sensor *sensor) { this->pdo_max_current_sensor_ = sensor; }

  /** Sets the sensor that will report the selected PDO's configured minimum voltage. */
  void set_pdo_min_voltage_sensor(sensor::Sensor *sensor) { this->pdo_min_voltage_sensor_ = sensor; }

  /** Sets the sensor that will report the selected PDO's configured voltage. */
  void set_pdo_voltage_sensor(sensor::Sensor *sensor) { this->pdo_voltage_sensor_ = sensor; }

  /** Sets the sensor that will report the selected PDO number. */
  void set_selected_pdo_sensor(sensor::Sensor *sensor) { this->selected_pdo_sensor_ = sensor; }

  /** Used by ESPHome framework. */
  void dump_config() override;
  /** Used by ESPHome framework. */
  void setup() override;
  /** Used by ESPHome framework. */
  void update() override;
  /** Used by ESPHome framework. */
  float get_setup_priority() const override;

 protected:
  MPQ4242Component *parent_;

  i2c::I2CRegister FW_REV = this->parent_->reg(MPQ4242_REGISTER_FW_REV);
  i2c::I2CRegister MAX_REQ_CUR = this->parent_->reg(MPQ4242_REGISTER_MAX_REQ_CUR);
  i2c::I2CRegister PWR_CTL2 = this->parent_->reg(MPQ4242_REGISTER_PWR_CTL2);
  i2c::I2CRegister STATUS2 = this->parent_->reg(MPQ4242_REGISTER_STATUS2);
  i2c::I2CRegister STATUS3 = this->parent_->reg(MPQ4242_REGISTER_STATUS3);

  float contract_power_;
  float max_requested_current_;

  uint8_t fw_rev_;
  uint8_t otp_threshold_{0};
  uint8_t otw_threshold_1_{0};
  uint8_t otw_threshold_2_{0};
  uint8_t selected_pdo_{0};

  uint8_t otp_threshold_index[8] = {155, 165, 175, 185, 0, 0, 0, 0};
  uint8_t otw_threshold_index[8] = {0, 105, 115, 125, 135, 145, 155, 165};

  sensor::Sensor *contract_power_sensor_{nullptr};
  sensor::Sensor *fw_rev_sensor_{nullptr};
  sensor::Sensor *max_requested_current_sensor_{nullptr};
  sensor::Sensor *otp_threshold_sensor_{nullptr};
  sensor::Sensor *otw_threshold_1_sensor_{nullptr};
  sensor::Sensor *otw_threshold_2_sensor_{nullptr};
  sensor::Sensor *pdo_max_current_sensor_{nullptr};
  sensor::Sensor *pdo_min_voltage_sensor_{nullptr};
  sensor::Sensor *pdo_voltage_sensor_{nullptr};
  sensor::Sensor *selected_pdo_sensor_{nullptr};
};

}  // namespace mpq4242
}  // namespace esphome