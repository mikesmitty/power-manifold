#pragma once

#include "esphome/core/component.h"
#include "esphome/components/uart/uart.h"

namespace esphome {
namespace lad_component {

class LadComponent : public uart::UARTDevice, public Component {
 public:
  void setup() override;
  void dump_config() override;
  float get_setup_priority() const override;
  uint8_t crc8_(const uint8_t *data, uint8_t len);
  bool read_register_(uint8_t *reg, uint8_t *data, uint8_t len);
  // void write_register_(bool state); // FIXME: Implement this function for controlling the LAD

 protected:
  uint8_t read_register_prefix_[3] = {0x55, 0x03, 0x00};
};

}  // namespace lad_component
}  // namespace esphome