import analogio
import asyncio
import board
import busio
import digitalio
import neopixel
import time
import adafruit_logging as logging

from i2ctarget import I2CTarget
from adafruit_bus_device.i2c_device import I2CDevice
from adafruit_led_animation.animation.blink import Blink
from adafruit_led_animation.animation.pulse import Pulse
from adafruit_led_animation.animation.rainbow import Rainbow
from adafruit_led_animation.animation.solid import Solid
from adafruit_led_animation.color import RED, ORANGE, YELLOW, GREEN, JADE, BLUE, WHITE, BLACK
from mpq4242 import MPQ4242, MPQ4242_PDO_TYPE_PPS

DEV_ID = 0x42

ERROR_LEVEL_NONE = 0
ERROR_LEVEL_WARNING = 1
ERROR_LEVEL_ERROR = 2

# Read-write regs
PIXEL_EFFECT_REG = 0x00
PIXEL_R_REG = PIXEL_COLOR_REG = 0x01
PIXEL_G_REG = 0x02
PIXEL_B_REG = 0x03
PIXEL_W_REG = 0x04
ENABLE_REG = 0x05
MAX_CURRENT_REG = 0x06
ENABLE_12V_PDO_REG = 0x07
SEND_SRC_CAP_REG = 0x08
SEND_HARD_RESET_REG = 0x09

# Read-only regs
DEV_ID_REG = 0x10
IOUT_REG_H = 0x11
IOUT_REG_L = 0x12
VOUT_REG_H = 0x13
VOUT_REG_L = 0x14
VIN_REG_H = 0x15
VIN_REG_L = 0x16
CUR_PDO_NUM_REG = 0x17
CUR_PDO_MIN_VOLT_REG = 0x18
CUR_PDO_VOLT_REG = 0x19
CUR_PDO_AMP_REG = 0x1A
FAULT_REG = 0x1B
SINK_ATTACHED = 0x1C
CONTRACT_POWER_REG = 0x1D
MISMATCH_FLAG_REG = 0x1E
GIVEBACK_FLAG_REG = 0x1F
CABLE_5A_FLAG_REG = 0x20
MAX_REQ_CUR_REG = 0x21
OTP_ID_REG = 0x22
OTP_SW_REV_REG = 0x23
LAST_REG = OTP_SW_REV_REG

regs = [0] * (LAST_REG + 1)

adc_pins = {
    "imon": analogio.AnalogIn(board.A0),
    "vin": analogio.AnalogIn(board.A2),
    "vout": analogio.AnalogIn(board.A3),
}
adc_readings = {}
enable_pin = digitalio.DigitalInOut(board.GP22)
enable_pin.direction = digitalio.Direction.OUTPUT
logger = logging.getLogger("i2ctarget")
led = None
mpq4242 = None
pixel_regs = set(range(PIXEL_EFFECT_REG, PIXEL_W_REG + 1))


async def main():
    logger.setLevel(logging.INFO)
    logger.addHandler(logging.StreamHandler())

    scl_in = board.GP5
    sda_in = board.GP4
    i2c_addr = 0x4D

    # Set the initial color to color.JADE
    regs[PIXEL_R_REG] = 0
    regs[PIXEL_G_REG] = 255
    regs[PIXEL_B_REG] = 40

    global led
    led = StatusLed()

    scl_out = board.GP3
    sda_out = board.GP2
    mpq4242_addr = 0x61
    i2c_out = busio.I2C(scl_out, sda_out)
    global mpq4242
    mpq4242 = MPQ4242(I2CDevice(i2c_out, mpq4242_addr), enable_pin)

    for pdo_num in range(1, 8):
        pdo = mpq4242.get_pdo(pdo_num)
        suffix = ""
        if pdo["pdo_type"] == MPQ4242_PDO_TYPE_PPS:
            suffix = f", MinV: {pdo["min_voltage"]} (PPS)"
        logger.info(
            f"PDO #{pdo_num}: Enabled: {pdo["enabled"]}, V: {pdo["voltage"]}, A: {pdo["max_current"]}{suffix}"
        )

    i2c_listener_task = asyncio.create_task(i2c_listener(scl_in, sda_in, i2c_addr))
    animate_led_task = asyncio.create_task(animate_led())
    cache_adc_readings_task = asyncio.create_task(cache_adc_readings())
    poll_mpq4242_task = asyncio.create_task(poll_mpq4242())

    await asyncio.gather(i2c_listener_task, animate_led_task, cache_adc_readings_task, poll_mpq4242_task)

async def i2c_listener(scl_in, sda_in, i2c_addr):
    reg_pointer = None
    reg_pointer_time = None

    with I2CTarget(scl_in, sda_in, (i2c_addr,)) as device:
        logger.debug("i2c listener started")
        while True:
            await asyncio.sleep(0)

            # check if there's a pending device request
            i2c_request = device.request()

            if not i2c_request:
                # no request is pending
                continue

            with i2c_request:
                if (
                    reg_pointer is not None
                    and (time.monotonic_ns() - reg_pointer_time) > 1e8  # 100ms
                ):
                    reg_pointer = None
                    reg_pointer_time = None

                # Write request
                if not i2c_request.is_read:

                    # First byte is the register id
                    reg = i2c_request.read(1)[0]
                    data = i2c_request.read()

                    # Don't allow writing to non-existent registers
                    if reg > LAST_REG:
                        logger.error(f"invalid register 0x{reg:02x}")
                        continue

                    # No further bytes means it's a reg read request, restart loop to send response
                    if not data:
                        logger.debug(f"read register: 0x{reg:02x}")
                        reg_pointer = reg
                        reg_pointer_time = time.monotonic_ns()
                        continue

                    count = len(data)

                    # Make sure we wouldn't write past the last register
                    if reg + count > LAST_REG:
                        logger.error(f"invalid data length to register 0x{reg:02x}")
                        continue

                    logger.debug(f"writing to reg 0x{reg:02x}: {data}")
                    for i in range(count):
                        regs[reg + i] = data[i]
                    handle_reg_writes(reg, count)
                else:
                    # Don't accept plain read requests without specifying a register first
                    if reg_pointer is None:
                        logger.warning("plain read requests are not supported")
                        # Still need to respond, but result data is not defined
                        i2c_request.write(bytes([0xFF]))
                        reg_pointer = None
                        reg_pointer_time = None
                        continue

                    # Plain reads are covered above, so we should always have a valid register
                    assert reg_pointer is not None

                    # The write-then-read to an invalid address is covered above,
                    #   but if this is a restarted read, index might be out of bounds so need to check
                    if reg_pointer > LAST_REG:
                        logger.error(f"read requested beyond the last register")
                        i2c_request.write(bytes([0xFF]))
                        reg_pointer = None
                        reg_pointer_time = None
                        continue

                    # Return the register value
                    data = handle_reg_reads(reg_pointer)
                    logger.debug(f"returning register 0x{reg_pointer:02x}: {data}")
                    i2c_request.write(bytes([data]))

                    # Increment the register index to allow for multi-byte reads
                    assert reg_pointer is not None
                    reg_pointer += 1
                    reg_pointer_time = time.monotonic_ns()


async def animate_led():
    while True:
        led.animate()
        await asyncio.sleep(0)


# Cache the ADC readings so we can return the value immediately when requested
async def cache_adc_readings():
    while True:
        for adc_name in adc_pins:
            adc_readings[adc_name] = get_adc_reading(adc_pins[adc_name])
        await asyncio.sleep(0.1)


def get_adc_reading(pin):
    return pin.value


def get_adc_voltage(reading):
    return (reading * 3.3) / 65536


# Return the cached ADC reading if available, otherwise read the ADC and return the result
def get_cached_adc_reading(adc_name):
    reading = adc_readings.get(adc_name)
    if reading is None:
        logger.warning(f"no cached reading for {adc_name}")
        reading = get_adc_reading(adc_pins[adc_name])
    adc_readings[adc_name] = None
    return reading


def get_voltage_divider_reading(adc_name, r1=22000, r2=2000):
    ratio = r2 / (r1 + r2)
    reading = get_cached_adc_reading(adc_name)
    voltage = get_adc_voltage(reading) / ratio
    if voltage < 1:
        voltage = 0
    return voltage


def get_imon_reading():
    # Vimon = Gain * Iout * Rsens
    # Vimon = 27.5V/V * Iout * 0.01 Ohm
    # Iout = Vimon / 0.275
    reading = get_cached_adc_reading("imon")
    output_current = get_adc_voltage(reading) / 0.275
    # MPQ4242 has a minimum imon current reading of 200mA
    if output_current < 0.2:
        output_current = 0
    return output_current


def handle_reg_reads(reg):
    if reg == DEV_ID_REG:
        return DEV_ID
    elif reg in [IOUT_REG_L, IOUT_REG_H]:
        # The Iout reading is returned as the output current in 10mA steps
        if regs[reg] is None:
            current = round(get_imon_reading() * 100)
            regs[IOUT_REG_L] = current & 0xFF
            regs[IOUT_REG_H] = (current >> 8) & 0xFF
        value = regs[reg]
        # Reset the register to None after reading so we get a fresh atomic value next time regardless of read order
        regs[reg] = None
        return value
    elif reg in [VOUT_REG_L, VOUT_REG_H]:
        # The Vout reading is returned as the output voltage in 10mV steps
        if regs[reg] is None:
            voltage = round(get_voltage_divider_reading("vout", r2=3300) * 100)
            regs[VOUT_REG_L] = voltage & 0xFF
            regs[VOUT_REG_H] = (voltage >> 8) & 0xFF
        value = regs[reg]
        # Reset the register to None after reading so we get a fresh atomic value next time regardless of read order
        regs[reg] = None
        return value
    elif reg in [VIN_REG_L, VIN_REG_H]:
        # The Vin reading is returned as the input voltage in 10mV steps
        if regs[reg] is None:
            voltage = round(get_voltage_divider_reading("vin", r2=2000) * 100)
            regs[VIN_REG_L] = voltage & 0xFF
            regs[VIN_REG_H] = (voltage >> 8) & 0xFF
        value = regs[reg]
        # Reset the register to None after reading so we get a fresh atomic value next time regardless of read order
        regs[reg] = None
        return value
    elif reg == CUR_PDO_NUM_REG:
        regs[reg] = mpq4242.get_selected_pdo()
    elif reg == CUR_PDO_MIN_VOLT_REG:
        regs[reg] = round(mpq4242.get_pdo_min_voltage(mpq4242.get_selected_pdo()) * 10)
    elif reg == CUR_PDO_VOLT_REG:
        regs[reg] = round(mpq4242.get_pdo_voltage(mpq4242.get_selected_pdo()) * 10)
    elif reg == CUR_PDO_AMP_REG:
        regs[reg] = round(mpq4242.get_pdo_current(mpq4242.get_selected_pdo()) / 0.05)
    elif reg == SINK_ATTACHED:
        regs[reg] = mpq4242.get_sink_attached()
    elif reg == FAULT_REG:
        regs[reg] = mpq4242.get_faults()
    elif reg == CONTRACT_POWER_REG:
        regs[reg] = round(mpq4242.get_contract_power() / 0.5)
    elif reg == MISMATCH_FLAG_REG:
        regs[reg] = mpq4242.get_mismatch_flag()
    elif reg == GIVEBACK_FLAG_REG:
        regs[reg] = mpq4242.get_giveback_flag()
    elif reg == CABLE_5A_FLAG_REG:
        regs[reg] = mpq4242.get_5a_cable()
    elif reg == MAX_REQ_CUR_REG:
        regs[reg] = mpq4242.get_max_req_cur()
    elif reg == OTP_ID_REG:
        regs[reg] = mpq4242.get_otp_id()
    elif reg == OTP_SW_REV_REG:
        regs[reg] = mpq4242.get_otp_software_rev()
    return regs[reg]


def handle_reg_writes(first_reg, length):
    changed_regs = set(range(first_reg, first_reg + length))

    if not changed_regs.isdisjoint(pixel_regs):
        logger.debug(f"updating neopixel: {regs[PIXEL_EFFECT_REG:PIXEL_W_REG+1]}")
        color = (regs[PIXEL_R_REG], regs[PIXEL_G_REG], regs[PIXEL_B_REG])
        led.set_color(color)
        led.set_pattern(regs[PIXEL_EFFECT_REG])

    if ENABLE_REG in changed_regs:
        set_enable_pin()
    if MAX_CURRENT_REG in changed_regs:
        set_max_current()
    if ENABLE_12V_PDO_REG in changed_regs:
        set_12v_pdo_enabled()
    if SEND_SRC_CAP_REG in changed_regs:
        logger.debug("sending SRC_CAP message")
        mpq4242.send_src_cap()
    if SEND_HARD_RESET_REG in changed_regs:
        logger.debug("sending HARD_RESET message")
        mpq4242.send_hard_reset()


async def poll_mpq4242():
    cur_error_level = ERROR_LEVEL_NONE
    cur_pdo_num = 0
    init_time = time.monotonic()
    first_change = True
    while True:
        await asyncio.sleep(1)
        mpq4242.poll()
        if (error_level := check_mpq4242_faults()) and error_level != cur_error_level:
            led.set_error(cur_error_level)
        elif (pdo_num := mpq4242.get_selected_pdo()) != cur_pdo_num:
            voltage = mpq4242.get_pdo_voltage(pdo_num)
            pdo_type = mpq4242.get_pdo_type(pdo_num)
            led.set_voltage(voltage, pdo_type == MPQ4242_PDO_TYPE_PPS)
            cur_pdo_num = pdo_num
        elif first_change and time.monotonic() - init_time > 5:
            # Update the LED after startup if we have no output
            led.set_color(BLACK)
            led.set_pattern(led.SOLID)
            first_change = False
        cur_error_level = error_level

def check_mpq4242_faults():
    error_level = 0
    general_fault = mpq4242.faults.get("FAULT")
    if mpq4242.faults.get("CC"):
        general_fault = False
        error_level = 1
        logger.warn("Over-current detected, CC mode enabled")
    if mpq4242.faults.get("VBATT_LOW1") or mpq4242.faults.get("VBATT_LOW2"):
        general_fault = False
        logger.info("VBATT low state reached")
    if mpq4242.faults.get("OTW1"):
        general_fault = False
        error_level = 1
        otw1 = mpq4242.temps.get("otw1")
        otp = mpq4242.temps.get("otp")
        logger.warn(f"Over-temp warning - OTW1 {otw1}°C reached, OTP shutdown at {otp}°C")
    if mpq4242.faults.get("OTW2"):
        general_fault = False
        error_level = 2
        otw2 = mpq4242.temps.get("otw2")
        otp = mpq4242.temps.get("otp")
        logger.error(f"Over-temp warning - OTW2 {otw2}°C reached, OTP shutdown at {otp}°C")
    if general_fault:
        error_level = 1
        logger.error("General fault detected")
    return error_level


def set_enable_pin(value=None):
    if _get_set_reg(ENABLE_REG, value):
        logger.debug("enabling mpq4242")
        mpq4242.enable()
    else:
        logger.debug("disabling mpq4242")
        mpq4242.disable()


def set_max_current(value=None):
    mpq4242.set_max_current(_get_set_reg(MAX_CURRENT_REG, value))


def set_12v_pdo_enabled(value=None):
    if value := _get_set_reg(ENABLE_12V_PDO_REG, value):
        logger.debug("enabling 12V PDO")
    else:
        logger.debug("disabling 12V PDO")
    mpq4242.enable_12v_pdo(value)


def _get_set_reg(reg, value):
    if value is None:
        value = regs[reg]
    else:
        regs[reg] = value
    return value


class StatusLed:
    SOLID = 0
    PULSE = 1
    BLINK = 2
    RAINBOW = 3

    def __init__(self, brightness=0.2, pixel_num=1, pin=board.GP1):
        self.pixels = neopixel.NeoPixel(
            pin, pixel_num, brightness=brightness, auto_write=False
        )
        self.color = BLACK
        self.pattern = None
        self.set_pattern(self.RAINBOW)

    def set_error(self, error_level):
        if error_level == ERROR_LEVEL_WARNING:
            self.set_color(ORANGE)
            self.set_pattern(self.BLINK)
        elif error_level == ERROR_LEVEL_ERROR:
            self.set_color(RED)
            self.set_pattern(self.BLINK)

    def set_voltage(self, voltage, pps=False):
        pattern = self.SOLID
        if pps:
            pattern = self.PULSE
        self.set_pattern(pattern)
        if voltage <= 1:
            self.set_color(BLACK)
        elif voltage <= 6:
            self.set_color(ORANGE)
        elif voltage <= 11:
            self.set_color(YELLOW)
        elif voltage <= 12:
            self.set_color(GREEN)
        elif voltage <= 16:
            self.set_color(BLUE)
        elif voltage > 16:
            self.set_color(WHITE)
        else:
            self.set_color(RED)

    def set_color(self, new_color):
        logger.debug(f"Setting color to {new_color}")
        self.color = new_color
        self._pattern.color = new_color
        regs[PIXEL_R_REG] = new_color[0]
        regs[PIXEL_G_REG] = new_color[1]
        regs[PIXEL_B_REG] = new_color[2]

    def set_pattern(self, new_pattern):
        logger.debug(f"Setting pattern to {new_pattern}")
        if new_pattern == self.pattern:
            return
        if new_pattern == self.SOLID:
            self._pattern = Solid(self.pixels, color=self.color)
        elif new_pattern == self.PULSE:
            self._pattern = Pulse(self.pixels, speed=0.01, color=self.color)
        elif new_pattern == self.BLINK:
            self._pattern = Blink(self.pixels, speed=0.5, color=self.color)
        elif new_pattern == self.RAINBOW:
            self._pattern = Rainbow(self.pixels, speed=0.1, period=5)
        else:
            raise ValueError("Invalid pattern")
        self.pattern = new_pattern
        regs[PIXEL_EFFECT_REG] = new_pattern

    def animate(self):
        self._pattern.animate()


if __name__ == "__main__":
    asyncio.run(main())
