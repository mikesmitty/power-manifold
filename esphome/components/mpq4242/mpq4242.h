#pragma once
#include "esphome/core/defines.h"
#include "esphome/core/component.h"
#ifdef USE_BINARY_SENSOR
#include "esphome/components/binary_sensor/binary_sensor.h"
#endif
#ifdef USE_BUTTON
#include "esphome/components/button/button.h"
#endif
#ifdef USE_SENSOR
#include "esphome/components/sensor/sensor.h"
#endif
#include "esphome/components/i2c/i2c.h"

namespace esphome {
namespace mpq4242 {

#define CHECK_BIT(var, pos) (((var) >> (pos)) & 1)

enum MPQ4242Command {
  MPQ4242_COMMAND_SEND_SRC_CAP,
  MPQ4242_COMMAND_SEND_HARD_RESET,
};

enum MPQ4242PdoType : uint8_t {
  MPQ4242_PDO_TYPE_FIXED,
  MPQ4242_PDO_TYPE_PPS,
};

enum MPQ4242Gpio1Function : uint8_t {
  MPQ4242_GPIO1_FN_POWER_SHARE_LOW,  // When GPIO1 is pulled low, the MPQ4242 enters reduced power output mode
  MPQ4242_GPIO1_FN_GATE,             // GPIO1 controls a power MOSFET protecting against batteries shorting to GND
  MPQ4242_GPIO1_FN_FAULT,            // GPIO1 is an active low open-drain fault indicator
  MPQ4242_GPIO1_FN_NTC2,             // GPIO1 is an external NTC thermistor input
  MPQ4242_GPIO1_FN_ATTACH_FLT_ALT,   // GPIO1 pulls low for 12µs when a sink plug-in is detected. If a fault
                                     // occurs, it pulls low
  MPQ4242_GPIO1_FN_ATTACH_FLT_ALT2,  // Behavior not differentiated from previous option in datasheet
  MPQ4242_GPIO1_FN_PLACEHOLDER,      // Value not listed in datasheet
  MPQ4242_GPIO1_FN_IMON,             // GPIO1 is an analog current monitor output
};

enum MPQ4242Gpio2Function : uint8_t {
  MPQ4242_GPIO2_FN_DISABLED,  // GPIO2 is not used (default)
  MPQ4242_GPIO2_FN_POLARITY,  // GPIO2 USB Type-C polarity indication. The POL pin is an open drain. When CC1 is
                              // selected as the CC line, POL is pulled low; when CC2 is selected as the CC line,
                              // POL is an open drain
  MPQ4242_GPIO2_FN_NTC,       // GPIO2 is an external NTC thermistor input
  MPQ4242_GPIO2_FN_VCONN_IN,  // GPIO2 is a 1W 5V input to power e-marked cables. Without this, VCONN is limited
                              // to 100mW provided by the chip's internal 5V regulator
  MPQ4242_GPIO2_FN_LED_PWM,   // GPIO2 is a 25kHz PWM output for an LED, configurable from 5-100% duty cycle over
                              // i2c with a max output of 15mA
  MPQ4242_GPIO2_FN_ATTACH,    // GPIO2 pulls low when a device is plugged in
  MPQ4242_GPIO2_FN_POWER_SHARE_HIGH,  // When GPIO2 is pulled high, the MPQ4242 enters power share (reduced power
                                      // output) mode
};

struct MPQ4242Pdo {
  bool enabled;
  float voltage;
  float min_voltage;
  float max_current;
  MPQ4242PdoType pdo_type;
};

static const uint8_t MPQ4242_REGISTER_PDO_SET1 = 0x00;
static const uint8_t MPQ4242_REGISTER_PDO_SET2 = 0x01;
static const uint8_t MPQ4242_REGISTER_PDO_I1 = 0x03;
static const uint8_t MPQ4242_REGISTER_PDO_V2_L = 0x04;
static const uint8_t MPQ4242_REGISTER_PDO_V2_H = 0x05;
static const uint8_t MPQ4242_REGISTER_PDO_I2 = 0x06;
static const uint8_t MPQ4242_REGISTER_PDO_V3_L = 0x07;
static const uint8_t MPQ4242_REGISTER_PDO_V3_H = 0x08;
static const uint8_t MPQ4242_REGISTER_PDO_I3 = 0x09;
static const uint8_t MPQ4242_REGISTER_PDO_V4_L = 0x0A;
static const uint8_t MPQ4242_REGISTER_PDO_V4_H = 0x0B;
static const uint8_t MPQ4242_REGISTER_PDO_I4 = 0x0C;
static const uint8_t MPQ4242_REGISTER_PDO_V5_L = 0x0D;
static const uint8_t MPQ4242_REGISTER_PDO_V5_H = 0x0E;
static const uint8_t MPQ4242_REGISTER_PDO_I5 = 0x0F;
static const uint8_t MPQ4242_REGISTER_PDO_V6_L = 0x10;
static const uint8_t MPQ4242_REGISTER_PDO_V6_H = 0x11;
static const uint8_t MPQ4242_REGISTER_PDO_I6 = 0x12;
static const uint8_t MPQ4242_REGISTER_PDO_V7_L = 0x13;
static const uint8_t MPQ4242_REGISTER_PDO_V7_H = 0x14;
static const uint8_t MPQ4242_REGISTER_PDO_I7 = 0x15;
static const uint8_t MPQ4242_REGISTER_PD_CTL2 = 0x17;
static const uint8_t MPQ4242_REGISTER_PWR_CTL1 = 0x18;
static const uint8_t MPQ4242_REGISTER_PWR_CTL2 = 0x19;
static const uint8_t MPQ4242_REGISTER_CTL_SYS1 = 0x1E;
static const uint8_t MPQ4242_REGISTER_CTL_SYS16 = 0x2D;
static const uint8_t MPQ4242_REGISTER_CTL_SYS17 = 0x2E;
static const uint8_t MPQ4242_REGISTER_STATUS1 = 0x30;
static const uint8_t MPQ4242_REGISTER_STATUS2 = 0x31;
static const uint8_t MPQ4242_REGISTER_STATUS3 = 0x32;
static const uint8_t MPQ4242_REGISTER_FW_REV = 0x35;
static const uint8_t MPQ4242_REGISTER_MAX_REQ_CUR = 0x36;
static const uint8_t MPQ4242_REGISTER_DEV_ID = 0x38;
static const uint8_t MPQ4242_REGISTER_CLK_ON = 0x39;

/// This class includes i2c support for the MPQ4242 USB PD controller.
/// The device has 7 configurable PDOs, 2 GPIOs with several functions
/// and is capable of up to 100W charging. This class is for the
/// MPQ4242 configuration.
class MPQ4242Component : public i2c::I2CDevice, public Component {
 public:
  void loop() override;

  /** Reads the PDO configuration from the device and returns it as a struct.
   * @param pdo_num The PDO number (2-7)
   */
  MPQ4242Pdo get_pdo(uint8_t pdo_num);

  /** Reads the current from a given PDO.
   * @param pdo_num The PDO number (2-7)
   * @param pdo The PDO type
   */
  float get_pdo_current(uint8_t pdo_num, MPQ4242PdoType pdo_type);

  /** Extracts the PDO type of a given PDO from the provided PDO_SET2 register value.
   * @param pdo_number The PDO number (1-7)
   * @param pdo_set2 The PDO_SET2 register value
   */
  MPQ4242PdoType get_pdo_type_bit(uint8_t pdo_number, uint8_t pdo_set2);

  /** Returns the PDO type of a given PDO
   * @param pdo_number The PDO number (1-7)
   */
  MPQ4242PdoType get_pdo_type(uint8_t pdo_number);

  /** Returns the voltage of a given fixed PDO
   * @param pdo_num The PDO number (1-7)
   */
  float get_fixed_voltage(uint8_t pdo_num);

  /** Returns the voltages of a given PPS PDO (APDO)
   * @param pdo_num The PDO number (2-7)
   */
  MPQ4242Pdo get_pps_voltage(uint8_t pdo_num);

  /** Sends a command to the MPQ4242 to perform a hard reset of the port. */
  void send_hard_reset();

  /** Sends a command to the MPQ4242 to send the source capabilities. */
  void send_src_cap();

  /** Sets the max current for all PDOs.
   * @param current The current in A
   */
  void set_max_current(float current) { this->pdo_current_ = current; }

  /** Sets which PDO will be configured to be a 12V PDO. This is useful for PD trigger boards powering 12V devices.
   * @param pdo The PDO number (2-7) to be configured as a 12V PDO
   */
  void set_12v_pdo_number(uint8_t pdo) { this->pdo_12v_ = pdo; }

  /** Sets whether the 12V PDO is enabled.
   * @param enabled Whether the 12V PDO is enabled
   */
  void set_12v_pdo_enabled(bool enabled) { this->pdo_12v_enabled_ = enabled; }

  /** Sets the function of GPIO1.
   * @param function The GPIO1 function
   */
  void set_gpio1_function(MPQ4242Gpio1Function function) { this->gpio1_function_ = function; }

  /** Sets the function of GPIO2.
   * @param function The GPIO2 function
   */
  void set_gpio2_function(MPQ4242Gpio2Function function) { this->gpio2_function_ = function; }

  /** Sets the current limit for all PDOs.
   * @param current The current limit in A
   */
  void set_pdo_current(float current) { this->pdo_current_ = current; }

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

  /** Sets the button for sending a hard reset message to the sink. */
  void set_hard_reset_button(button::Button *button) { this->hard_reset_button_ = button; }

  /** Sets the button for sending a SRC_CAP message to the sink. */
  void set_src_cap_button(button::Button *button) { this->src_cap_button_ = button; }

  /** Sets the sensor that will report the contract power negotiated with the sink. */
  void set_contract_power_sensor(sensor::Sensor *sensor) { this->contract_power_sensor_ = sensor; }

  /** Sets the sensor that will report the firmware revision of the sink. */
  void set_fw_rev_sensor(sensor::Sensor *sensor) { this->fw_rev_sensor_ = sensor; }

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

  /** Sets the sensor that will report the current requested by the sink. */
  void set_requested_current_sensor(sensor::Sensor *sensor) { this->requested_current_sensor_ = sensor; }

  /** Sets the sensor that will report the selected PDO number. */
  void set_selected_pdo_sensor(sensor::Sensor *sensor) { this->selected_pdo_sensor_ = sensor; }

  /** Used by ESPHome framework. */
  void setup() override;
  /** Used by ESPHome framework. */
  void dump_config() override;
  /** Used by ESPHome framework. */
  float get_setup_priority() const override;

 protected:
  i2c::ErrorCode last_error_;
  bool pdo_12v_enabled_;
  float contract_power_;
  float pdo_current_;
  float requested_current_;
  uint8_t fw_rev_;
  uint8_t gpio1_function_;
  uint8_t gpio2_function_;
  uint8_t otp_threshold_{0};
  uint8_t otw_threshold_1_{0};
  uint8_t otw_threshold_2_{0};
  uint8_t pdo_12v_;
  uint8_t selected_pdo_{0};

  uint8_t otp_threshold_index[8] = {155, 165, 175, 185, 0, 0, 0, 0};
  uint8_t otw_threshold_index[8] = {0, 105, 115, 125, 135, 145, 155, 165};

  binary_sensor::BinarySensor *cable_5a_capable_binary_sensor_{nullptr};
  binary_sensor::BinarySensor *current_mismatch_binary_sensor_{nullptr};
  binary_sensor::BinarySensor *giveback_flag_binary_sensor_{nullptr};
  binary_sensor::BinarySensor *otw_threshold_1_binary_sensor_{nullptr};
  binary_sensor::BinarySensor *otw_threshold_2_binary_sensor_{nullptr};
  binary_sensor::BinarySensor *pps_mode_binary_sensor_{nullptr};
  binary_sensor::BinarySensor *sink_attached_binary_sensor_{nullptr};

  button::Button *hard_reset_button_{nullptr};
  button::Button *src_cap_button_{nullptr};

  sensor::Sensor *contract_power_sensor_{nullptr};
  sensor::Sensor *fw_rev_sensor_{nullptr};
  sensor::Sensor *otp_threshold_sensor_{nullptr};
  sensor::Sensor *otw_threshold_1_sensor_{nullptr};
  sensor::Sensor *otw_threshold_2_sensor_{nullptr};
  sensor::Sensor *pdo_max_current_sensor_{nullptr};
  sensor::Sensor *pdo_min_voltage_sensor_{nullptr};
  sensor::Sensor *pdo_voltage_sensor_{nullptr};
  sensor::Sensor *requested_current_sensor_{nullptr};
  sensor::Sensor *selected_pdo_sensor_{nullptr};
};

}  // namespace mpq4242
}  // namespace esphome