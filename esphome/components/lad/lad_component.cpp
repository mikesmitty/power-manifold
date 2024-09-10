#include "esphome/core/log.h"
#include "lad_component.h"

namespace esphome {
namespace lad_component {

static const char *TAG = "lad_component.component";

float LadComponent::get_setup_priority() const { return setup_priority::DATA; }

void LadComponent::setup() { ESP_LOGCONFIG(TAG, "Setting up LAD..."); }

void LadComponent::dump_config() { ESP_LOGCONFIG(TAG, "LAD component"); }

uint8_t LadComponent::crc8_(const uint8_t *data, uint8_t len) {
  uint8_t crc = 0;
  for (uint8_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (uint8_t j = 0; j < 8; j++) {
      if (crc & 0x80) {
        crc = (crc << 1) ^ 0x07;
      } else {
        crc <<= 1;
      }
    }
  }
  return crc;
}

bool LadComponent::read_register_(uint8_t *reg, uint8_t *data, uint8_t len) {
  // All read requests are 5 bytes long with the same 3 byte prefix
  this->write_array(this->read_register_prefix_, 3);
  this->write_array(reg, 2);
  this->flush();

  // Read the response
  bool resp = this->read_array(data, len);

  // Check the message CRC
  uint8_t crc_result = this->crc8_(data, len - 1);
  if (crc_result != data[len - 1]) {
    ESP_LOGE(TAG, "CRC mismatch on read register %02X. Expected %02X, got %02X", reg[0], crc_result, data[len - 1]);
    return false;
  }

  return resp;
}

}  // namespace lad_component
}  // namespace esphome