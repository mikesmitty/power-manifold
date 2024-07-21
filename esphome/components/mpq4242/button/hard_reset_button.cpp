#include "hard_reset_button.h"

namespace esphome {
namespace mpq4242 {

void HardResetButton::press_action() { this->parent_->send_hard_reset(); }

}  // namespace mpq4242
}  // namespace esphome