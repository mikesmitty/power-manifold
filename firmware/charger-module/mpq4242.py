import time

MPQ4242_CHIP_ID = 0x58

MPQ4242_FAULT_GENERAL = 0
MPQ4242_FAULT_OTW1 = 1
MPQ4242_FAULT_OTW2 = 2
MPQ4242_FAULT_NTC1 = 3
MPQ4242_FAULT_NTC2 = 4
MPQ4242_FAULT_CC = 5
MPQ4242_FAULT_SHORT_VBATT = 6
MPQ4242_FAULT_VBATT_LOW = 7

# When GPIO1 is pulled low, the MPQ4242 enters reduced power output mode
MPQ4242_GPIO1_FN_POWER_SHARE_LOW = 0
# GPIO1 controls a power MOSFET protecting against batteries shorting to GND
MPQ4242_GPIO1_FN_GATE = 1
# GPIO1 is an active low open-drain fault indicator
MPQ4242_GPIO1_FN_FAULT = 2
# GPIO1 is an external NTC thermistor input
MPQ4242_GPIO1_FN_NTC2 = 3
# GPIO1 pulls low for 12µs when a sink plug-in is detected. If a fault occurs, it pulls low
MPQ4242_GPIO1_FN_ATTACH_FLT_ALT = 4
# Behavior not differentiated from previous option in datasheet
MPQ4242_GPIO1_FN_ATTACH_FLT_ALT2 = 5
# GPIO1 is an analog current monitor output
MPQ4242_GPIO1_FN_IMON = 7

# GPIO2 is not used (default)
MPQ4242_GPIO2_FN_DISABLED = 0
# GPIO2 USB Type-C polarity indication. The POL pin is an open drain. When CC1 is
# selected as the CC line, POL is pulled low; when CC2 is selected as the CC line,
# POL is an open drain
MPQ4242_GPIO2_FN_POLARITY = 1
# GPIO2 is an external NTC thermistor input
MPQ4242_GPIO2_FN_NTC = 2
# GPIO2 is a 1W 5V input to power e-marked cables. Without this, VCONN is limited
# to 100mW provided by the chip's internal 5V regulator
MPQ4242_GPIO2_FN_VCONN_IN = 3
# GPIO2 is a 25kHz PWM output for an LED, configurable from 5-100% duty cycle over
# i2c with a max output of 15mA
MPQ4242_GPIO2_FN_LED_PWM = 4
# GPIO2 pulls low when a device is plugged in
MPQ4242_GPIO2_FN_ATTACH = 5
# When GPIO2 is pulled high, the MPQ4242 enters power share (reduced power output) mode
MPQ4242_GPIO2_FN_POWER_SHARE_HIGH = 6

MPQ4242_PDO_TYPE_FIXED = 0
MPQ4242_PDO_TYPE_PPS = 1

MPQ4242_PEAK_INPUT_CURRENT_8A = 0
MPQ4242_PEAK_INPUT_CURRENT_12A = 1
MPQ4242_PEAK_INPUT_CURRENT_16A = 2
MPQ4242_PEAK_INPUT_CURRENT_20A = 3

MPQ4242_REGISTER_PDO_SET1 = 0x00
MPQ4242_REGISTER_PDO_SET2 = 0x01
MPQ4242_REGISTER_PDO_I1 = 0x03
MPQ4242_REGISTER_PDO_V2_L = 0x04
MPQ4242_REGISTER_PDO_V2_H = 0x05
MPQ4242_REGISTER_PDO_I2 = 0x06
MPQ4242_REGISTER_PDO_V3_L = 0x07
MPQ4242_REGISTER_PDO_V3_H = 0x08
MPQ4242_REGISTER_PDO_I3 = 0x09
MPQ4242_REGISTER_PDO_V4_L = 0x0A
MPQ4242_REGISTER_PDO_V4_H = 0x0B
MPQ4242_REGISTER_PDO_I4 = 0x0C
MPQ4242_REGISTER_PDO_V5_L = 0x0D
MPQ4242_REGISTER_PDO_V5_H = 0x0E
MPQ4242_REGISTER_PDO_I5 = 0x0F
MPQ4242_REGISTER_PDO_V6_L = 0x10
MPQ4242_REGISTER_PDO_V6_H = 0x11
MPQ4242_REGISTER_PDO_I6 = 0x12
MPQ4242_REGISTER_PDO_V7_L = 0x13
MPQ4242_REGISTER_PDO_V7_H = 0x14
MPQ4242_REGISTER_PDO_I7 = 0x15
MPQ4242_REGISTER_PD_CTL2 = 0x17
MPQ4242_REGISTER_PWR_CTL1 = 0x18
MPQ4242_REGISTER_PWR_CTL2 = 0x19
MPQ4242_REGISTER_CTL_SYS1 = 0x1E
MPQ4242_REGISTER_CTL_SYS2 = 0x1F
MPQ4242_REGISTER_CTL_SYS16 = 0x2D
MPQ4242_REGISTER_CTL_SYS17 = 0x2E
MPQ4242_REGISTER_STATUS1 = 0x30
MPQ4242_REGISTER_STATUS2 = 0x31
MPQ4242_REGISTER_STATUS3 = 0x32
MPQ4242_REGISTER_ID1 = 0x33
MPQ4242_REGISTER_ID2 = 0x34
MPQ4242_REGISTER_FW_REV = 0x35
MPQ4242_REGISTER_MAX_REQ_CUR = 0x36
MPQ4242_REGISTER_DEV_ID = 0x38
MPQ4242_REGISTER_CLK_ON = 0x39


class MPQ4242:
    def __init__(
        self,
        i2c_dev,
        enable_pin,
        max_current=3.0,
        gpio1_fn=MPQ4242_GPIO1_FN_IMON,
        gpio2_fn=MPQ4242_GPIO2_FN_POWER_SHARE_HIGH,
        peak_input_current=MPQ4242_PEAK_INPUT_CURRENT_8A,
    ):
        self.i2c_dev = i2c_dev
        self.enable_pin = enable_pin
        self._config = {}
        self._config["12v_pdo"] = None
        self._config["gpio1_fn"] = gpio1_fn
        self._config["gpio2_fn"] = gpio2_fn
        self._config["max_current"] = max_current
        self._config["peak_cl"] = peak_input_current
        self.faults = {}
        self.temps = {}

        with self.i2c_dev:
            buf_read_reg = bytearray(1)
            buf_resp = bytearray(1)
            buf_read_reg[0] = MPQ4242_REGISTER_DEV_ID
            self.i2c_dev.write_then_readinto(buf_read_reg, buf_resp)
            if buf_resp[0] != MPQ4242_CHIP_ID:
                raise ValueError("MPQ4242 chip not found")

        # CLK_ON must be set to 1 to enable writing to the i2c registers
        self.set_clk_on()
        self.set_max_current(self._config["max_current"])
        self.set_gpio_functions(gpio1_fn, gpio2_fn)
        self.set_peak_input_current(peak_input_current)
        self._get_temps()
        self._get_pdos()

    def disable(self):
        # Logic is inverted through a schmitt trigger
        self.enable_pin.value = True

    def enable(self):
        # Logic is inverted through a schmitt trigger
        self.enable_pin.value = False
        time.sleep(0.1)
        self.set_config()

    def enable_12v_pdo(self, pdo_num=3):
        if pdo_num:
            self._config["12v_pdo"] = pdo_num
            self._config["12v_pdo_restore"] = self._get_pdo(pdo_num)
            self.set_pdo(
                pdo_num,
                {
                    "enabled": True,
                    "pdo_type": MPQ4242_PDO_TYPE_FIXED,
                    "max_current": self._config["max_current"],
                    "voltage": 12,
                },
            )
        elif restore_pdo := self._config.get("12v_pdo_restore"):
            self.set_pdo(pdo_num, restore_pdo)
            self._config["12v_pdo"] = None
            self._config["12v_pdo_restore"] = None
        else:
            raise ValueError("12V PDO restore config not available")
        self.send_src_cap()

    def get_5a_cable(self):
        return self._reg_get_bit(MPQ4242_REGISTER_FW_REV, 5)

    def get_contract_power(self):
        return self._reg_read(MPQ4242_REGISTER_STATUS3) * 0.5

    def get_faults(self, keep=False):
        faults = 0
        if self.faults.get("FAULT"):
            faults |= 1 << MPQ4242_FAULT_GENERAL
            self.faults["FAULT"] = keep
        if self.faults.get("OTW1"):
            faults |= 1 << MPQ4242_FAULT_OTW1
            self.faults["OTW1"] = keep
        if self.faults.get("OTW2"):
            faults |= 1 << MPQ4242_FAULT_OTW2
            self.faults["OTW2"] = keep
        if self.faults.get("NTC1"):
            faults |= 1 << MPQ4242_FAULT_NTC1
            self.faults["NTC1"] = keep
        if self.faults.get("NTC2"):
            faults |= 1 << MPQ4242_FAULT_NTC2
            self.faults["NTC2"] = keep
        if self.faults.get("CC"):
            faults |= 1 << MPQ4242_FAULT_CC
            self.faults["CC"] = keep
        if self.faults.get("SHORT_VBATT"):
            faults |= 1 << MPQ4242_FAULT_SHORT_VBATT
            self.faults["SHORT_VBATT"] = keep
        if self.faults.get("VBATT_LOW1"):
            faults |= 1 << MPQ4242_FAULT_VBATT_LOW
            self.faults["VBATT_LOW1"] = keep
        if self.faults.get("VBATT_LOW2"):
            faults |= 1 << MPQ4242_FAULT_VBATT_LOW
            self.faults["VBATT_LOW2"] = keep
        return faults

    def get_fixed_voltage(self, pdo_num):
        return self._get_pdo_field(pdo_num, "voltage")

    def get_giveback_flag(self):
        return self._reg_get_bit(MPQ4242_REGISTER_FW_REV, 6)

    def get_max_current(self):
        return self._config["max_current"]

    def get_max_req_cur(self):
        return self._reg_read(MPQ4242_REGISTER_MAX_REQ_CUR)

    def get_mismatch_flag(self):
        return self._reg_get_bit(MPQ4242_REGISTER_FW_REV, 7)

    def get_otp_id(self):
        return self._reg_read(MPQ4242_REGISTER_ID1)

    def get_otp_software_rev(self):
        return self._reg_read(MPQ4242_REGISTER_ID2)

    def get_pdo(self, pdo_num):
        return self._config.get(f"pdo{pdo_num}")

    def get_pdo_current(self, pdo_num):
        return self._get_pdo_field(pdo_num, "max_current")

    def get_pdo_enabled(self, pdo_num):
        return self._get_pdo_field(pdo_num, "enabled")

    def get_pdo_min_voltage(self, pdo_num):
        return self._get_pdo_field(pdo_num, "min_voltage")

    def get_pdo_type(self, pdo_num):
        return self._get_pdo_field(pdo_num, "pdo_type")

    def get_pdo_voltage(self, pdo_num):
        return self._get_pdo_field(pdo_num, "voltage")

    def get_polarity(self):
        return self._get_bit(self.get_status1(), 5)

    def get_pps_voltage(self, pdo_num):
        return {
            "voltage": self._get_pdo_field(pdo_num, "voltage"),
            "min_voltage": self._get_pdo_field(pdo_num, "min_voltage"),
        }

    def get_selected_pdo(self):
        # Get bits [3:1]
        return self.get_status2() >> 1 & 0b111

    def get_sink_attached(self):
        return self._get_bit(self.get_status1(), 7)

    def get_status1(self):
        status1 = self._reg_read(MPQ4242_REGISTER_STATUS1)
        if self._get_bit(status1, 6):
            self.faults["NTC2"] = True
        if self._get_bit(status1, 4):
            self.faults["SHORT_VBATT"] = True
        if self._get_bit(status1, 3):
            self.faults["FAULT"] = True
        if self._get_bit(status1, 1):
            self.faults["CC"] = True
        if self._get_bit(status1, 0):
            self.faults["NTC1"] = True
        return status1

    def get_status2(self):
        status2 = self._reg_read(MPQ4242_REGISTER_STATUS2)
        if self._get_bit(status2, 7):
            self.faults["VBATT_LOW1"] = True
        if self._get_bit(status2, 6):
            self.faults["VBATT_LOW2"] = True
        if self._get_bit(status2, 5):
            self.faults["OTW1"] = True
        if self._get_bit(status2, 4):
            self.faults["OTW2"] = True
        return status2

    def poll(self):
        self.get_status1()
        self.get_status2()

    def send_hard_reset(self):
        self._reg_set_bit(MPQ4242_REGISTER_PD_CTL2, 7, True)

    def send_src_cap(self):
        self._reg_set_bit(MPQ4242_REGISTER_CTL_SYS1, 7, True)

    def set_clk_on(self):
        self._reg_write(MPQ4242_REGISTER_CLK_ON, 1)

    def set_config(self):
        self.set_clk_on()
        if self._config.get("12v_pdo"):
            self.enable_12v_pdo(self._config["12v_pdo"])
        if self._config.get("max_current") is not None:
            self.set_max_current(self._config["max_current"])
        if self._config.get("frequency_dithering") is not None:
            self.set_frequency_dithering(self._config["frequency_dithering"])
        if self._config.get("gpio1_fn") is not None:
            self.set_gpio_functions(self._config["gpio1_fn"], self._config["gpio2_fn"])
        if self._config.get("peak_cl") is not None:
            self.set_peak_input_current(self._config["peak_cl"])

    def set_frequency_dithering(self, enable):
        self._config["frequency_dithering"] = enable
        self._reg_set_bit(MPQ4242_REGISTER_PWR_CTL1, 3, enable)

    def set_gpio_functions(self, gpio1_function, gpio2_function):
        self._config["gpio1_fn"] = gpio1_function
        self._config["gpio2_fn"] = gpio2_function
        ctl_sys2 = self._reg_read(MPQ4242_REGISTER_CTL_SYS2)
        # GPIO1 bits[7:5], GPIO2 bits[4:2]
        ctl_sys2 = gpio1_function << 5 | gpio2_function << 2 | (ctl_sys2 & 0b11)
        self._reg_write(MPQ4242_REGISTER_CTL_SYS2, ctl_sys2)

    def set_max_current(self, current):
        self._config["max_current"] = current
        pdo_types = self._reg_read(MPQ4242_REGISTER_PDO_SET2)
        for pdo_num in range(1, 8):
            pdo_type = MPQ4242_PDO_TYPE_FIXED
            if pdo_num > 1 and self._get_bit(pdo_types, pdo_num - 2):
                pdo_type = MPQ4242_PDO_TYPE_PPS
            self.set_pdo_current(pdo_num, current, pdo_type)

    def set_pdo(self, pdo_num, pdo):
        if pdo_num < 1 or pdo_num > 7:
            raise ValueError("Requested PDO not configurable")
        self.set_pdo_type(pdo_num, pdo["pdo_type"])
        self.set_pdo_current(pdo_num, pdo["max_current"])
        if pdo["pdo_type"] == MPQ4242_PDO_TYPE_PPS:
            self.set_pdo_pps_voltage(pdo_num, pdo["voltage"], pdo["min_voltage"])
        else:
            self.set_pdo_fixed_voltage(pdo_num, pdo["voltage"])
        self.set_pdo_enabled(pdo_num, pdo["enabled"])
        self._config[f"pdo{pdo_num}"] = pdo

    def set_pdo_current(self, pdo_num, current, pdo_type=None):
        if pdo_num < 1 or pdo_num > 7:
            raise ValueError("Requested PDO not configurable")
        if pdo_type is None:
            pdo_type = self._get_pdo_type(pdo_num)
        step = 0.020  # 20mA steps
        if pdo_type == MPQ4242_PDO_TYPE_PPS:
            step = 0.050  # 50mA steps
        with self.i2c_dev:
            buf_write_reg = bytearray(2)
            buf_write_reg[0] = MPQ4242_REGISTER_PDO_I1 + 3 * (pdo_num - 1)
            buf_write_reg[1] = round(current / step)
            self.i2c_dev.write(buf_write_reg)

    def set_pdo_enabled(self, pdo_num, enable):
        if pdo_num < 2 or pdo_num > 7:
            raise ValueError("Requested PDO not configurable")
        self._reg_set_bit(MPQ4242_REGISTER_PDO_SET1, pdo_num - 2, enable)

    def set_pdo_fixed_voltage(self, pdo_num, voltage):
        if pdo_num < 2 or pdo_num > 7:
            raise ValueError("Requested PDO not configurable")
        with self.i2c_dev as i2c:
            buf = bytearray(3)
            buf[0] = MPQ4242_REGISTER_PDO_V2_L + 3 * (pdo_num - 2)
            buf[1] = int(voltage * 10)
            buf[2] = 0
            i2c.write(buf)

    def set_pdo_pps_voltage(self, pdo_num, voltage, min_voltage):
        if pdo_num < 2 or pdo_num > 7:
            raise ValueError("Requested PDO not configurable")
        with self.i2c_dev as i2c:
            buf = bytearray(3)
            buf[0] = MPQ4242_REGISTER_PDO_V2_L + 3 * (pdo_num - 2)
            buf[1] = int(min_voltage * 10)
            buf[2] = int(voltage * 10)
            i2c.write(buf)

    def set_pdo_type(self, pdo_num, pdo_type):
        self._reg_set_bit(MPQ4242_REGISTER_PDO_SET2, pdo_num - 2, pdo_type)

    def set_peak_input_current(self, current):
        ctl_sys17 = self._reg_read(MPQ4242_REGISTER_CTL_SYS17)
        ctl_sys17 &= 0b00111111
        ctl_sys17 |= current << 6
        self._reg_write(MPQ4242_REGISTER_CTL_SYS17, ctl_sys17)

    def _get_bit(self, value, bit):
        return value >> bit & 1

    def _get_fixed_voltage(self, pdo_num):
        if pdo_num < 1 or pdo_num > 7:
            raise ValueError("Requested PDO not available")
        if pdo_num == 1:
            # Get VBUS_VOLTAGE from CTL_SYS17 bits [1:0]
            ctl_sys17 = self._reg_read(MPQ4242_REGISTER_CTL_SYS17)
            return 5.0 + 0.05 * (ctl_sys17 & 0b11)
        return self._reg_read(MPQ4242_REGISTER_PDO_V2_L + 3 * (pdo_num - 2)) / 10.0

    def _get_pdo(self, pdo_num):
        pdo = {
            "enabled": self._get_pdo_enabled(pdo_num),
            "pdo_type": self._get_pdo_type(pdo_num),
            "max_current": self._get_pdo_current(pdo_num),
        }
        if pdo["pdo_type"] == MPQ4242_PDO_TYPE_PPS:
            pps = self._get_pps_voltage(pdo_num)
            pdo["min_voltage"] = pps["min_voltage"]
            pdo["voltage"] = pps["voltage"]
        else:
            pdo["voltage"] = self._get_fixed_voltage(pdo_num)
        return pdo

    def _get_pdo_current(self, pdo_num):
        if pdo_num < 1 or pdo_num > 7:
            raise ValueError("Requested PDO not available")
        if pdo_num == 0:
            return 0
        pdo_i = self._reg_read(MPQ4242_REGISTER_PDO_I1 + 3 * (pdo_num - 1))
        if self._get_pdo_type(pdo_num) == MPQ4242_PDO_TYPE_PPS:
            return pdo_i * 0.050
        else:
            return pdo_i * 0.020

    def _get_pdo_enabled(self, pdo_num):
        if pdo_num < 1 or pdo_num > 7:
            raise ValueError("Requested PDO not available")
        if pdo_num == 1:
            return True
        return bool(self._reg_get_bit(MPQ4242_REGISTER_PDO_SET1, pdo_num - 2))

    def _get_pdo_field(self, pdo_num, field):
        return self._config.get(f"pdo{pdo_num}", {}).get(field, 0)

    def _get_pdo_type(self, pdo_num):
        if pdo_num < 1 or pdo_num > 7:
            raise ValueError("Requested PDO not available")
        elif pdo_num == 1:
            return MPQ4242_PDO_TYPE_FIXED
        return self._reg_get_bit(MPQ4242_REGISTER_PDO_SET2, pdo_num - 2)

    def _get_pdo_min_voltage(self, pdo_num):
        if pdo_num < 0 or pdo_num > 7:
            raise ValueError("Requested PDO not available")
        if pdo_num == 0:
            return 0
        if pdo_num == 1 or self._get_pdo_type(pdo_num) == MPQ4242_PDO_TYPE_FIXED:
            return 0
        return self._reg_read(MPQ4242_REGISTER_PDO_V2_L + 3 * (pdo_num - 2)) / 10.0

    def _get_pdo_voltage(self, pdo_num):
        if pdo_num < 0 or pdo_num > 7:
            raise ValueError("Requested PDO not available")
        if pdo_num == 0:
            return 0
        if pdo_num == 1 or self._get_pdo_type(pdo_num) == MPQ4242_PDO_TYPE_FIXED:
            return self._get_fixed_voltage(pdo_num)
        return self._reg_read(MPQ4242_REGISTER_PDO_V2_H + 3 * (pdo_num - 2)) / 10.0

    def _get_pdos(self):
        for pdo_num in range(1, 8):
            self._config[f"pdo{pdo_num}"] = self._get_pdo(pdo_num)

    def _get_pps_voltage(self, pdo_num):
        if pdo_num < 2 or pdo_num > 7:
            raise ValueError("Requested PDO not available")
        pdo_v_l = self._reg_read(MPQ4242_REGISTER_PDO_V2_L + 3 * (pdo_num - 2))
        pdo_v_h = self._reg_read(MPQ4242_REGISTER_PDO_V2_H + 3 * (pdo_num - 2))
        return {
            "voltage": pdo_v_h / 10.0,
            "min_voltage": pdo_v_l / 10.0,
        }

    def _get_temps(self):
        pwr_ctl2 = self._reg_read(MPQ4242_REGISTER_PWR_CTL2)
        ctl_sys16 = self._reg_read(MPQ4242_REGISTER_CTL_SYS16)

        # OTP is 155°C + 10°C per bit, up to 185°C
        otp = pwr_ctl2 & 0b111
        self.temps["OTP"] = 155 + (otp * 10)

        # OTW1 is 95°C + 10°C per bit (0 is disabled)
        if otw1 := pwr_ctl2 >> 3 & 0b111:
            self.temps["OTW1"] = 95 + (otw1 * 10)

        # OTW2 is 95°C + 10°C per bit (0 is disabled)
        if otw2 := ctl_sys16 >> 1 & 0b111:
            self.temps["OTW2"] = 95 + (otw2 * 10)

    def _reg_get_bit(self, reg, bit):
        return self._reg_read(reg) >> bit & 1

    def _reg_set_bit(self, reg, bit, value):
        with self.i2c_dev:
            buf_read_reg = bytearray(1)
            buf_resp = bytearray(1)
            buf_write_reg = bytearray(2)
            buf_read_reg[0] = reg
            buf_write_reg[0] = reg
            self.i2c_dev.write_then_readinto(buf_read_reg, buf_resp)
            if value:
                buf_write_reg[1] = buf_resp[0] | 1 << bit
            else:
                buf_write_reg[1] = buf_resp[0] & ~(1 << bit)
            self.i2c_dev.write(buf_write_reg)

    def _reg_read(self, reg):
        with self.i2c_dev:
            buf_read_reg = bytearray(1)
            buf_read_reg[0] = reg
            buf_resp = bytearray(1)
            self.i2c_dev.write_then_readinto(buf_read_reg, buf_resp)
        return buf_resp[0]

    def _reg_write(self, reg, value):
        with self.i2c_dev:
            buf_write_reg = bytearray(2)
            buf_write_reg[0] = reg
            buf_write_reg[1] = value
            self.i2c_dev.write(buf_write_reg)
