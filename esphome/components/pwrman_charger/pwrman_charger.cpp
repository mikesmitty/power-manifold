#include "pwrman_charger.h"

#include "esphome/components/i2c/i2c.h"
#include "esphome/core/component.h"
#include "esphome/core/log.h"

namespace esphome {
namespace pwrman_charger {

static const char *TAG = "pwrman_charger.component";

static const uint8_t PWRMAN_CHARGER_CHIP_ID = 0x42;  // Pwrman Charger device id

float PwrmanCharger::get_setup_priority() const { return setup_priority::DATA; }

void PwrmanCharger::setup() {
  ESP_LOGCONFIG(TAG, "Setting up Pwrman Charger...");

  // Make sure we're talking to the right chip
  uint8_t chip_id = reg(PWRMAN_CHARGER_REGISTER_DEV_ID).get();
  if (chip_id != PWRMAN_CHARGER_DEV_ID) {
    ESP_LOGE(TAG, "Wrong chip ID %02X", chip_id);
    this->mark_failed();
    return;
  }
}

void PwrmanCharger::send_hard_reset() {
  const uint8_t data[1] = {1};
  this->write_register(PWRMAN_CHARGER_REGISTER_SEND_HARD_RESET, data, 1);
}

void PwrmanCharger::send_src_cap() {
  const uint8_t data[1] = {1};
  this->write_register(PWRMAN_CHARGER_REGISTER_SEND_SRC_CAP, data, 1);
}

void PwrmanCharger::dump_config() {
  ESP_LOGCONFIG(TAG, "Pwrman Charger component:");
  LOG_I2C_DEVICE(this);
  if (this->is_failed()) {
    ESP_LOGE(TAG, "Communication with Pwrman Charger failed!");
  }
}

}  // namespace pwrman_charger
}  // namespace esphome