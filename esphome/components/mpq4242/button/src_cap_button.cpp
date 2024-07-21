#include "src_cap_button.h"

namespace esphome {
namespace mpq4242 {

void SrcCapButton::press_action() { this->parent_->send_src_cap(); }

}  // namespace mpq4242
}  // namespace esphome