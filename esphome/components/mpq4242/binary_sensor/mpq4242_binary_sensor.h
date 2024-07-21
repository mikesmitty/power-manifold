#pragma once

#include "../mpq4242.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/core/defines.h"
#include "esphome/core/component.h"

namespace esphome {
namespace mpq4242 {

// FIXME: Update this description for the sensor
/// This class includes i2c support for the MPQ4242 USB PD controller.
/// The device has 7 configurable PDOs, 2 GPIOs with several functions
/// and is capable of up to 100W charging. This class is for the
/// MPQ4242 configuration.
class MPQ4242BinarySensor : public Component {
 public:
  MPQ4242BinarySensor(MPQ4242Component *parent) : parent_(parent) {}

  /** Sets the binary sensor indicating if a 5A-capable cable is attached. */
  void set_cable_5a_capable_binary_sensor(binary_sensor::BinarySensor *binary_sensor) {
    this->cable_5a_capable_binary_sensor_ = binary_sensor;
  }

  /** Sets the binary sensor indicating if the client requests more current than can be provided. */
  void set_current_mismatch_binary_sensor(binary_sensor::BinarySensor *binary_sensor) {
    this->current_mismatch_binary_sensor_ = binary_sensor;
  }

  /** Sets the binary sensor indicating if the client can accept a lower power level. */
  void set_giveback_flag_binary_sensor(binary_sensor::BinarySensor *binary_sensor) {
    this->giveback_flag_binary_sensor_ = binary_sensor;
  }

  /** Sets the binary sensor indicating if the otw threshold 1 has been reached. */
  void set_otw_threshold_1_binary_sensor(binary_sensor::BinarySensor *binary_sensor) {
    this->otw_threshold_1_binary_sensor_ = binary_sensor;
  }

  /** Sets the binary sensor indicating if the otw threshold 2 has been reached. */
  void set_otw_threshold_2_binary_sensor(binary_sensor::BinarySensor *binary_sensor) {
    this->otw_threshold_2_binary_sensor_ = binary_sensor;
  }

  /** Sets the binary sensor indicating if the selected PDO uses PPS. */
  void set_pps_mode_binary_sensor(binary_sensor::BinarySensor *binary_sensor) {
    this->pps_mode_binary_sensor_ = binary_sensor;
  }

  /** Sets the binary sensor indicating if a sink is attached. */
  void set_sink_attached_binary_sensor(binary_sensor::BinarySensor *binary_sensor) {
    this->sink_attached_binary_sensor_ = binary_sensor;
  }

  /** Used by ESPHome framework. */
  void dump_config() override;
  /** Used by ESPHome framework. */
  void loop() override;
  /** Used by ESPHome framework. */
  void setup() override;
  /** Used by ESPHome framework. */
  float get_setup_priority() const override;

 protected:
  MPQ4242Component *parent_;

  i2c::I2CRegister FW_REV = this->parent_->reg(MPQ4242_REGISTER_FW_REV);
  i2c::I2CRegister STATUS1 = this->parent_->reg(MPQ4242_REGISTER_STATUS1);
  i2c::I2CRegister STATUS2 = this->parent_->reg(MPQ4242_REGISTER_STATUS2);
  i2c::I2CRegister STATUS3 = this->parent_->reg(MPQ4242_REGISTER_STATUS3);

  binary_sensor::BinarySensor *cable_5a_capable_binary_sensor_{nullptr};
  binary_sensor::BinarySensor *current_mismatch_binary_sensor_{nullptr};
  binary_sensor::BinarySensor *giveback_flag_binary_sensor_{nullptr};
  binary_sensor::BinarySensor *otw_threshold_1_binary_sensor_{nullptr};
  binary_sensor::BinarySensor *otw_threshold_2_binary_sensor_{nullptr};
  binary_sensor::BinarySensor *pps_mode_binary_sensor_{nullptr};
  binary_sensor::BinarySensor *sink_attached_binary_sensor_{nullptr};
};

}  // namespace mpq4242
}  // namespace esphome
