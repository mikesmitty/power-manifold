#include "src_cap_button.h"

namespace esphome {
namespace pwrman_charger {

void SrcCapButton::press_action() { this->parent_->send_src_cap(); }

}  // namespace pwrman_charger
}  // namespace esphome