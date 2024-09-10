#pragma once

#include "../pwrman_charger.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/core/defines.h"
#include "esphome/core/component.h"

namespace esphome {
namespace pwrman_charger {

/// This class includes i2c support for the Power Manifold Charger module.
/// The device has 7 configurable PDOs, 2 GPIOs with several functions
/// and is capable of up to 100W charging. This class is for the
/// PwrmanCharger configuration.
class PwrmanChargerSensor : public PollingComponent {
 public:
  PwrmanChargerSensor(PwrmanCharger *parent) : parent_(parent) {}

  /** Sets the sensor that will report the contract power negotiated with the sink. */
  void set_contract_power_sensor(sensor::Sensor *sensor) { this->contract_power_sensor_ = sensor; }

  /** Sets the sensor that will report the input voltage to the charger module. */
  void set_input_voltage_sensor(sensor::Sensor *sensor) { this->input_voltage_sensor_ = sensor; }

  /** Sets the sensor that will report the USB output current from the module. */
  void set_output_current_sensor(sensor::Sensor *sensor) { this->output_current_sensor_ = sensor; }

  /** Sets the sensor that will report the USB output voltage from the module. */
  void set_output_voltage_sensor(sensor::Sensor *sensor) { this->output_voltage_sensor_ = sensor; }

  /** Sets the sensor that will report the selected PDO's configured max current. */
  void set_pdo_max_current_sensor(sensor::Sensor *sensor) { this->pdo_max_current_sensor_ = sensor; }

  /** Sets the sensor that will report the selected PDO's configured minimum voltage. */
  void set_pdo_min_voltage_sensor(sensor::Sensor *sensor) { this->pdo_min_voltage_sensor_ = sensor; }

  /** Sets the sensor that will report the selected PDO's configured voltage. */
  void set_pdo_voltage_sensor(sensor::Sensor *sensor) { this->pdo_voltage_sensor_ = sensor; }

  /** Used by ESPHome framework. */
  void dump_config() override;
  /** Used by ESPHome framework. */
  void setup() override;
  /** Used by ESPHome framework. */
  void update() override;
  /** Used by ESPHome framework. */
  float get_setup_priority() const override;

 protected:
  PwrmanCharger *parent_;

  float selected_pdo_;

  uint8_t resp_bytes_[2] = {0, 0};

  sensor::Sensor *contract_power_sensor_{nullptr};
  sensor::Sensor *input_voltage_sensor_{nullptr};
  sensor::Sensor *output_current_sensor_{nullptr};
  sensor::Sensor *output_voltage_sensor_{nullptr};
  sensor::Sensor *pdo_max_current_sensor_{nullptr};
  sensor::Sensor *pdo_min_voltage_sensor_{nullptr};
  sensor::Sensor *pdo_voltage_sensor_{nullptr};
};

}  // namespace pwrman_charger
}  // namespace esphome