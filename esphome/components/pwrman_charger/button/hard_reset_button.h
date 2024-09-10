#pragma once

#include "esphome/components/button/button.h"
#include "../pwrman_charger.h"

namespace esphome {
namespace pwrman_charger {

class HardResetButton : public button::Button, public Parented<PwrmanCharger> {
 public:
  HardResetButton() = default;

 protected:
  void press_action() override;
};

}  // namespace pwrman_charger
}  // namespace esphome