#include "hard_reset_button.h"

namespace esphome {
namespace pwrman_charger {

void HardResetButton::press_action() { this->parent_->send_hard_reset(); }

}  // namespace pwrman_charger
}  // namespace esphome