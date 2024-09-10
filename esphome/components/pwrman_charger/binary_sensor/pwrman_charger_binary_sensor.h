#pragma once

#include "../pwrman_charger.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/core/defines.h"
#include "esphome/core/component.h"

namespace esphome {
namespace pwrman_charger {

/// This class includes i2c support for the Power Manifold Charger Module.
/// This hot-swappable module can be remotely managed and monitored via
/// I2C and is capable of up to 100W charging. This class is for the
/// binary sensor configuration.
class PwrmanChargerBinarySensor : public PollingComponent {
 public:
  PwrmanChargerBinarySensor(PwrmanCharger *parent) : parent_(parent) {}

  /** Sets the binary sensor indicating if a 5A-capable cable is attached. */
  void set_cable_5a_capable_binary_sensor(binary_sensor::BinarySensor *binary_sensor) {
    this->cable_5a_capable_binary_sensor_ = binary_sensor;
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
  void update() override;
  /** Used by ESPHome framework. */
  void setup() override;
  /** Used by ESPHome framework. */
  float get_setup_priority() const override;

 protected:
  PwrmanCharger *parent_;

  uint8_t resp_bytes_[1] = {0};

  binary_sensor::BinarySensor *cable_5a_capable_binary_sensor_{nullptr};
  binary_sensor::BinarySensor *otw_threshold_1_binary_sensor_{nullptr};
  binary_sensor::BinarySensor *otw_threshold_2_binary_sensor_{nullptr};
  binary_sensor::BinarySensor *pps_mode_binary_sensor_{nullptr};
  binary_sensor::BinarySensor *sink_attached_binary_sensor_{nullptr};
};

}  // namespace pwrman_charger
}  // namespace esphome
