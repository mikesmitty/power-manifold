#include "mpq4242.h"

#ifdef USE_BUTTON
#include "esphome/components/button/button.h"
#endif
#ifdef USE_SENSOR
#include "esphome/components/sensor/sensor.h"
#endif
#include "esphome/components/i2c/i2c.h"
#include "esphome/core/component.h"
#include "esphome/core/log.h"

namespace esphome {
namespace mpq4242 {

static const char *TAG = "mpq4242.component";

static const uint8_t MPQ4242_CHIP_ID = 0x58;  // MPQ4242 default device id from part id

float MPQ4242Component::get_setup_priority() const { return setup_priority::DATA; }

void MPQ4242Component::setup() {
  ESP_LOGCONFIG(TAG, "Setting up MPQ4242...");

  // Make sure we're talking to the right chip
  uint8_t chip_id = reg(MPQ4242_REGISTER_DEV_ID).get();
  if (chip_id != MPQ4242_CHIP_ID) {
    ESP_LOGE(TAG, "Wrong chip ID %02X", chip_id);
    this->mark_failed();
    return;
  }

  // CLK_ON must be set to 1 to enable writing to the i2c registers
  i2c::I2CRegister CLK_ON = reg(MPQ4242_REGISTER_CLK_ON);
  CLK_ON = 1;

  // Enabled PDOs register (bit = PDO-2, PDO 1 is always enabled)
  i2c::I2CRegister PDO_SET1 = reg(MPQ4242_REGISTER_PDO_SET1);
  // PDO types, FIXED/PPS (bit = PDO-2, PDO 1 is always FIXED)
  i2c::I2CRegister PDO_SET2 = reg(MPQ4242_REGISTER_PDO_SET2);

  // PDO current limits
  i2c::I2CRegister PDO_I[7] = {reg(MPQ4242_REGISTER_PDO_I1), reg(MPQ4242_REGISTER_PDO_I2), reg(MPQ4242_REGISTER_PDO_I3),
                               reg(MPQ4242_REGISTER_PDO_I4), reg(MPQ4242_REGISTER_PDO_I5), reg(MPQ4242_REGISTER_PDO_I6),
                               reg(MPQ4242_REGISTER_PDO_I7)};

  // PDO 2-7 voltage registers (PDO 1 is locked to 5V)
  i2c::I2CRegister PDO_V_L[6] = {reg(MPQ4242_REGISTER_PDO_V2_L), reg(MPQ4242_REGISTER_PDO_V3_L),
                                 reg(MPQ4242_REGISTER_PDO_V4_L), reg(MPQ4242_REGISTER_PDO_V5_L),
                                 reg(MPQ4242_REGISTER_PDO_V6_L), reg(MPQ4242_REGISTER_PDO_V7_L)};
  i2c::I2CRegister PDO_V_H[6] = {reg(MPQ4242_REGISTER_PDO_V2_H), reg(MPQ4242_REGISTER_PDO_V3_H),
                                 reg(MPQ4242_REGISTER_PDO_V4_H), reg(MPQ4242_REGISTER_PDO_V5_H),
                                 reg(MPQ4242_REGISTER_PDO_V6_H), reg(MPQ4242_REGISTER_PDO_V7_H)};

  if (this->pdo_12v_enabled_) {
    ESP_LOGCONFIG(TAG, "Enabling 12V PDO on PDO%d", this->pdo_12v_);
    // Set 12V PDO (except max current, which gets set next)
    uint8_t pdo_12v_index = this->pdo_12v_ - 2;
    PDO_SET2 &= ~(1 << pdo_12v_index);  // Set the PDO type to FIXED
    PDO_V_H[pdo_12v_index] = 0;         // Voltage high register is unused in fixed PDOs
    PDO_V_L[pdo_12v_index] = 120;       // 12V in 100mV steps
    PDO_SET1 |= (1 << pdo_12v_index);   // Enable the PDO
  }

  // Enable frequency dithering mode to reduce EMI
  i2c::I2CRegister PWR_CTL1 = reg(MPQ4242_REGISTER_PWR_CTL1);
  PWR_CTL1 |= (1 << 3);

  // Configure PDO max current
  uint8_t pdo_types = PDO_SET2.get();
  for (uint8_t i = 0; i < 7; i++) {
    if (this->get_pdo_type_bit(i + 1, pdo_types) == MPQ4242_PDO_TYPE_FIXED) {
      // Set current step for fixed PDOs
      PDO_I[i] = std::round(this->pdo_current_ / 0.020);  // 20mA steps
    } else {
      // Set current step for APDOs (PPS mode)
      PDO_I[i] = std::round(this->pdo_current_ / 0.050);  // 50mA steps
    }
  }

  // Set GPIO functions
  i2c::I2CRegister CTL_SYS2 = reg(MPQ4242_REGISTER_CTL_SYS17);
  // GPIO1 bits[7:5], GPIO2 bits[4:2]
  CTL_SYS2 |= (this->gpio1_function_ << 5) | (this->gpio2_function_ << 2);
}

MPQ4242Pdo MPQ4242Component::get_pdo(uint8_t pdo_num) {
  struct MPQ4242Pdo pdo;

  if (pdo_num == 1) {
    pdo.enabled = true;
  } else {
    i2c::I2CRegister PDO_SET1 = reg(MPQ4242_REGISTER_PDO_SET1);
    pdo.enabled = PDO_SET1.get() & (1 << (pdo_num - 2));
  }

  pdo.pdo_type = this->get_pdo_type(pdo_num);

  if (pdo.pdo_type == MPQ4242_PDO_TYPE_PPS) {
    MPQ4242Pdo pps = this->get_pps_voltage(pdo_num);
    pdo.min_voltage = pps.min_voltage;
    pdo.voltage = pps.voltage;
  } else {
    pdo.voltage = this->get_fixed_voltage(pdo_num);
    pdo.min_voltage = 0;
  }
  pdo.max_current = this->get_pdo_current(pdo_num, pdo.pdo_type);
  return pdo;
}

MPQ4242PdoType MPQ4242Component::get_pdo_type(uint8_t pdo_num) {
  i2c::I2CRegister PDO_SET2 = reg(MPQ4242_REGISTER_PDO_SET2);
  uint8_t pdo_types = PDO_SET2.get();
  return this->get_pdo_type_bit(pdo_num, pdo_types);
}

MPQ4242PdoType MPQ4242Component::get_pdo_type_bit(uint8_t pdo_num, uint8_t pdo_set2) {
  if (pdo_num < 2) {
    return MPQ4242_PDO_TYPE_FIXED;
  }
  // 0 = FIXED, 1 = PPS
  return (pdo_set2 & (1 << (pdo_num - 2))) ? MPQ4242_PDO_TYPE_PPS : MPQ4242_PDO_TYPE_FIXED;
}

float MPQ4242Component::get_fixed_voltage(uint8_t pdo_num) {
  if (pdo_num < 2) {
    // Get VBUS_VOLTAGE from CTL_SYS17 bits [1:0]
    i2c::I2CRegister CTL_SYS17 = reg(MPQ4242_REGISTER_CTL_SYS17);
    return 5.0 + 0.05 * (CTL_SYS17.get() & 0b11);
  }

  i2c::I2CRegister PDO_V_L = reg(MPQ4242_REGISTER_PDO_V2_L + 3 * (pdo_num - 2));
  return PDO_V_L.get() / 10.0;
}

MPQ4242Pdo MPQ4242Component::get_pps_voltage(uint8_t pdo_num) {
  i2c::I2CRegister PDO_V_L = reg(MPQ4242_REGISTER_PDO_V2_L + 3 * (pdo_num - 2));
  i2c::I2CRegister PDO_V_H = reg(MPQ4242_REGISTER_PDO_V2_H + 3 * (pdo_num - 2));
  return MPQ4242Pdo{
    voltage: PDO_V_H.get() / float(10.0),
    min_voltage: PDO_V_L.get() / float(10.0),
  };
}

float MPQ4242Component::get_pdo_current(uint8_t pdo_num, MPQ4242PdoType pdo_type) {
  i2c::I2CRegister PDO_I = reg(MPQ4242_REGISTER_PDO_I1 + 3 * (pdo_num - 1));
  if (pdo_type == MPQ4242_PDO_TYPE_PPS) {
    return PDO_I.get() * 0.050;
  }
  return PDO_I.get() * 0.020;
}

void MPQ4242Component::loop() {
  i2c::I2CRegister STATUS2 = this->reg(MPQ4242_REGISTER_STATUS2);
  uint8_t status2 = STATUS2.get();

  uint8_t selected_pdo = status2 >> 1 & 0b111;
  MPQ4242Pdo pdo = this->get_pdo(selected_pdo);
}

void MPQ4242Component::send_hard_reset() {
  i2c::I2CRegister PD_CTL2 = reg(MPQ4242_REGISTER_PD_CTL2);
  PD_CTL2 |= (1 << 7);
}

void MPQ4242Component::send_src_cap() {
  i2c::I2CRegister CTL_SYS1 = reg(MPQ4242_REGISTER_CTL_SYS1);
  CTL_SYS1 |= (1 << 7);
}

void MPQ4242Component::dump_config() {
  ESP_LOGCONFIG(TAG, "MPQ4242 component:");
  LOG_I2C_DEVICE(this);
  if (this->is_failed()) {
    ESP_LOGE(TAG, "Communication with MPQ4242 failed!");
  }
  ESP_LOGCONFIG(TAG, "  Max current: %.2fA", this->pdo_current_);
  ESP_LOGCONFIG(TAG, "  Enable 12V PDO: %s", YESNO(this->pdo_12v_enabled_));
  if (this->pdo_12v_enabled_) {
    ESP_LOGCONFIG(TAG, "  12V PDO Number: PDO%d", this->pdo_12v_);
  }
  ESP_LOGCONFIG(TAG, "  GPIO1 function: %d", this->gpio1_function_);
  ESP_LOGCONFIG(TAG, "  GPIO2 function: %d", this->gpio2_function_);
#ifdef USE_BUTTON
  LOG_BUTTON("  ", "HardResetButton", this->hard_reset_button_);
  LOG_BUTTON("  ", "SrcCapButton", this->src_cap_button_);
#endif
  for (uint8_t i = 1; i <= 7; i++) {
    MPQ4242Pdo pdo = this->get_pdo(i);
    if (pdo.pdo_type == MPQ4242_PDO_TYPE_PPS) {
      ESP_LOGCONFIG(TAG, "  PDO%d: %0.2fA %0.1fV-%0.1fV (PPS)", i, pdo.max_current, pdo.min_voltage, pdo.voltage);
    } else {
      ESP_LOGCONFIG(TAG, "  PDO%d: %0.2fA %0.2fV", i, pdo.max_current, pdo.voltage);
    }
  }
}

}  // namespace mpq4242
}  // namespace esphome