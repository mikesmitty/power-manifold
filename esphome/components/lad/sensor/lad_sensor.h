#pragma once

#include "../lad_component.h"
#include "esphome/core/component.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/uart/uart.h"

namespace esphome {
namespace lad_component {

/// This class includes UART support for the Meanwell LAD series of battery-backed
/// power supplies.
class LadSensor : public sensor::Sensor, public PollingComponent, public uart::UARTDevice {
 public:
  LadSensor(LadComponent *parent) : parent_(parent) {}

  /** Sets the sensor that will report the input voltage to the UPS. */
  void set_input_voltage_sensor(sensor::Sensor *sensor) { this->input_voltage_sensor_ = sensor; }

  /** Sets the sensor that will report the output current from the UPS. */
  void set_current_sensor(sensor::Sensor *sensor) { this->current_sensor_ = sensor; }

  /** Sets the sensor that will report the backup battery voltage from the UPS. */
  void set_battery_voltage_sensor(sensor::Sensor *sensor) { this->battery_voltage_sensor_ = sensor; }

  /** Sets the sensor that will report the voltage of battery 1 from the UPS. */
  void set_battery_1_voltage_sensor(sensor::Sensor *sensor) { this->battery_1_voltage_sensor_ = sensor; }

  /** Sets the sensor that will report the voltage of battery 2 from the UPS. */
  void set_battery_2_voltage_sensor(sensor::Sensor *sensor) { this->battery_2_voltage_sensor_ = sensor; }

  /** Sets the sensor that will report the voltage of battery 3 from the UPS. */
  void set_battery_3_voltage_sensor(sensor::Sensor *sensor) { this->battery_3_voltage_sensor_ = sensor; }

  /** Sets the sensor that will report the voltage of battery 4 from the UPS. */
  void set_battery_4_voltage_sensor(sensor::Sensor *sensor) { this->battery_4_voltage_sensor_ = sensor; }

  /** Used by ESPHome framework. */
  void dump_config() override;
  /** Used by ESPHome framework. */
  void setup() override;
  /** Used by ESPHome framework. */
  void update() override;
  /** Used by ESPHome framework. */
  float get_setup_priority() const override;

 protected:
  LadComponent *parent_;

  uint8_t input_voltage_[2] = {0x20, 0xEF};      // Returns 2 bytes
  uint8_t current_[2] = {0x30, 0x9F};            // Returns 2 bytes
  uint8_t battery_voltage_[2] = {0x40, 0xC8};    // Returns 2 bytes
  uint8_t cell_voltage_[2] = {0x50, 0xB8};       // Returns 8 bytes
  uint8_t battery_uvp_point_[2] = {0x60, 0x28};  // Returns 2 bytes

  sensor::Sensor *input_voltage_sensor_{nullptr};
  sensor::Sensor *current_sensor_{nullptr};
  sensor::Sensor *battery_voltage_sensor_{nullptr};
  sensor::Sensor *battery_1_voltage_sensor_{nullptr};
  sensor::Sensor *battery_2_voltage_sensor_{nullptr};
  sensor::Sensor *battery_3_voltage_sensor_{nullptr};
  sensor::Sensor *battery_4_voltage_sensor_{nullptr};
};

}  // namespace lad_component
}  // namespace esphome