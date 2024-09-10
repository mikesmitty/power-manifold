#pragma once
#include "esphome/core/component.h"
#include "esphome/core/defines.h"
#ifdef USE_BUTTON
#include "esphome/components/button/button.h"
#endif
#ifdef USE_SWITCH
#include "esphome/components/switch/switch.h"
#endif
#include "esphome/components/i2c/i2c.h"

namespace esphome {
namespace pwrman_charger {

#define CHECK_BIT(var, pos) (((var) >> (pos)) & 1)

enum PwrmanChargerPixelEffect : uint8_t {
  PWRMAN_CHARGER_PIXEL_EFFECT_SOLID,
  PWRMAN_CHARGER_PIXEL_EFFECT_PULSE,
  PWRMAN_CHARGER_PIXEL_EFFECT_BLINK,
  PWRMAN_CHARGER_PIXEL_EFFECT_RAINBOW,
};

enum PwrmanChargerPdoType : uint8_t {
  PWRMAN_CHARGER_PDO_TYPE_FIXED = 0x00,
  PWRMAN_CHARGER_PDO_TYPE_PPS = 0x01,
};

static const uint8_t PWRMAN_CHARGER_DEV_ID = 0x42;

// Read-write registers
static const uint8_t PWRMAN_CHARGER_REGISTER_PIXEL_EFFECT = 0x00;
static const uint8_t PWRMAN_CHARGER_REGISTER_PIXEL_R = 0x01;
static const uint8_t PWRMAN_CHARGER_REGISTER_PIXEL_G = 0x02;
static const uint8_t PWRMAN_CHARGER_REGISTER_PIXEL_B = 0x03;
static const uint8_t PWRMAN_CHARGER_REGISTER_PIXEL_W = 0x04;
static const uint8_t PWRMAN_CHARGER_REGISTER_ENABLE = 0x05;
static const uint8_t PWRMAN_CHARGER_REGISTER_MAX_CURRENT = 0x06;
static const uint8_t PWRMAN_CHARGER_REGISTER_ENABLE_12V_PDO = 0x07;
static const uint8_t PWRMAN_CHARGER_REGISTER_SEND_SRC_CAP = 0x08;
static const uint8_t PWRMAN_CHARGER_REGISTER_SEND_HARD_RESET = 0x09;

// Read-only registers
static const uint8_t PWRMAN_CHARGER_REGISTER_DEV_ID = 0x10;
static const uint8_t PWRMAN_CHARGER_REGISTER_IOUT = 0x11;
static const uint8_t PWRMAN_CHARGER_REGISTER_VOUT = 0x13;
static const uint8_t PWRMAN_CHARGER_REGISTER_VIN = 0x15;
static const uint8_t PWRMAN_CHARGER_REGISTER_CUR_PDO_NUM = 0x17;
static const uint8_t PWRMAN_CHARGER_REGISTER_CUR_PDO_MIN_VOLT = 0x18;
static const uint8_t PWRMAN_CHARGER_REGISTER_CUR_PDO_VOLT = 0x19;
static const uint8_t PWRMAN_CHARGER_REGISTER_CUR_PDO_AMP = 0x1A;
static const uint8_t PWRMAN_CHARGER_REGISTER_FAULT = 0x1B;
static const uint8_t PWRMAN_CHARGER_REGISTER_SINK_ATTACHED = 0x1C;
static const uint8_t PWRMAN_CHARGER_REGISTER_CONTRACT_POWER = 0x1D;
static const uint8_t PWRMAN_CHARGER_REGISTER_MISMATCH_FLAG = 0x1E;
static const uint8_t PWRMAN_CHARGER_REGISTER_GIVEBACK_FLAG = 0x1F;
static const uint8_t PWRMAN_CHARGER_REGISTER_CABLE_5A_FLAG = 0x20;
static const uint8_t PWRMAN_CHARGER_REGISTER_MAX_REQ_CUR = 0x21;
static const uint8_t PWRMAN_CHARGER_REGISTER_OTP_ID = 0x22;
static const uint8_t PWRMAN_CHARGER_REGISTER_OTP_SW_REV = 0x23;

/// This class includes i2c support for the Power Manifold Charger Module.
/// This hot-swappable module can be remotely managed and monitored via
/// I2C and is capable of up to 100W charging. This class is for the
/// central configuration.
class PwrmanCharger : public i2c::I2CDevice, public PollingComponent {
#ifdef USE_SWITCH
 protected:
  switch_::Switch *enable_12v_switch_{nullptr};
  switch_::Switch *power_switch_{nullptr};

 public:
  void set_enable_12v_switch(switch_::Switch *s) { this->enable_12v_switch_ = s; }
  void set_power_switch(switch_::Switch *s) { this->power_switch_ = s; }
#endif

  /** Sends a command to the USB controller to perform a hard reset of the port. */
  void send_hard_reset();

  /** Sends a command to the USB controller to send a SRC_CAP message to the sink. */
  void send_src_cap();

  /** Sets the button for sending a hard reset message to the sink. */
  void set_hard_reset_button(button::Button *button) { this->hard_reset_button_ = button; }

  /** Sets the button for sending a SRC_CAP message to the sink. */
  void set_src_cap_button(button::Button *button) { this->src_cap_button_ = button; }

  /** Used by ESPHome framework. */
  void setup() override;
  /** Used by ESPHome framework. */
  void dump_config() override;
  /** Used by ESPHome framework. */
  float get_setup_priority() const override;

 protected:
  i2c::ErrorCode last_error_;

  const uint8_t *pixel_effect_;

  button::Button *hard_reset_button_{nullptr};
  button::Button *src_cap_button_{nullptr};
};

}  // namespace pwrman_charger
}  // namespace esphome