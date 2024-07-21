#pragma once

#include "esphome/components/button/button.h"
#include "../mpq4242.h"

namespace esphome {
namespace mpq4242 {

class SrcCapButton : public button::Button, public Parented<MPQ4242Component> {
 public:
  SrcCapButton() = default;

 protected:
  void press_action() override;
};

}  // namespace mpq4242
}  // namespace esphome