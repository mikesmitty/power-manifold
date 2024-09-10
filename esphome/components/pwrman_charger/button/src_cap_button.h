#pragma once

#include "esphome/components/button/button.h"
#include "../pwrman_charger.h"

namespace esphome {
namespace pwrman_charger {

class SrcCapButton : public button::Button, public Parented<PwrmanCharger> {
 public:
  SrcCapButton() = default;

 protected:
  void press_action() override;
};

}  // namespace pwrman_charger
}  // namespace esphome