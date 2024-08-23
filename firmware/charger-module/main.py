import analogio
import board
import digitalio
import neopixel
import time
import adafruit_logging as logging

from i2ctarget import I2CTarget
from adafruit_led_animation.animation.blink import Blink
from adafruit_led_animation.animation.pulse import Pulse
from adafruit_led_animation.animation.solid import Solid
from adafruit_led_animation.color import JADE

DEV_ID = 0x42

# Read-write regs
PIXEL_EFFECT_REG = 0x00
PIXEL_R_REG = PIXEL_COLOR_REG = 0x01
PIXEL_G_REG = 0x02
PIXEL_B_REG = 0x03
PIXEL_W_REG = 0x04
ENABLE_REG = 0x05

# Read-only regs
DEV_ID_REG = 0x10
IOUT_REG_H = 0x11
IOUT_REG_L = 0x12
VOUT_REG_H = 0x13
VOUT_REG_L = 0x14
VIN_REG_H = 0x15
VIN_REG_L = 0x16
LAST_REG = VIN_REG_L

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
pixel_regs = set(range(PIXEL_EFFECT_REG, PIXEL_W_REG + 1))


def main():
    # logger.setLevel(logging.INFO)
    logger.setLevel(logging.DEBUG)
    logger.addHandler(logging.StreamHandler())

    set_enable_pin(1)

    scl = board.GP5
    sda = board.GP4
    i2c_addr = 0x4D

    reg_pointer = None
    reg_pointer_time = None

    # Set the initial color to color.JADE
    regs[PIXEL_R_REG] = 0
    regs[PIXEL_G_REG] = 255
    regs[PIXEL_B_REG] = 40

    # FIXME: get rid of the globals where possible
    global led
    led = StatusLed()

    while True:
        with I2CTarget(scl, sda, (i2c_addr,)) as device:
            logger.debug("i2c listener started")
            while True:
                led.animate()

                # check if there's a pending device request
                i2c_request = device.request()

                if not i2c_request:
                    # no request is pending, so we'll pre-fetch the ADC readings
                    cache_adc_readings()
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
                            logger.info(f"read register: 0x{reg:02x}")
                            reg_pointer = reg
                            reg_pointer_time = time.monotonic_ns()
                            continue

                        count = len(data)

                        # Make sure we wouldn't write past the last register
                        if reg + count > LAST_REG:
                            logger.error(f"invalid data length to register 0x{reg:02x}")
                            continue

                        logger.info(f"writing to reg 0x{reg:02x}: {data}")
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
                        logger.info(f"returning register 0x{reg_pointer:02x}: {data}")
                        i2c_request.write(bytes([data]))

                        # Increment the register index to allow for multi-byte reads
                        assert reg_pointer is not None
                        reg_pointer += 1
                        reg_pointer_time = time.monotonic_ns()


# Cache the ADC readings so we can return the value immediately when requested
def cache_adc_readings():
    for adc_name in adc_pins:
        adc_readings[adc_name] = get_adc_reading(adc_pins[adc_name])


def get_adc_reading(pin):
    return pin.value


def get_adc_voltage(reading):
    return (reading * 3.3) / 65536


# Return the cached ADC reading if available, otherwise read the ADC and return the result
def get_cached_adc_reading(adc_name):
    reading = adc_readings.get(adc_name)
    if reading is None:
        logger.error(f"no cached reading for {adc_name}")
        reading = get_adc_reading(adc_pins[adc_name])
    adc_readings[adc_name] = None
    return reading


def get_voltage_divider_reading(adc_name, r1=22000, r2=2000):
    ratio = r2 / (r1 + r2)
    reading = get_cached_adc_reading(adc_name)
    adc_voltage = get_adc_voltage(reading)
    return adc_voltage / ratio


def get_imon_reading():
    # Vimon = Gain * Iout * Rsens
    # Vimon = 27.5V/V * Iout * 0.01 Ohm
    # Iout = Vimon / 0.275
    reading = get_cached_adc_reading("imon")
    return get_adc_voltage(reading) / 0.275


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
    else:
        return regs[reg]


def handle_reg_writes(first_reg, length):
    changed_regs = set(range(first_reg, first_reg + length))

    if not changed_regs.isdisjoint(pixel_regs):
        logger.info(f"updating neopixel: {regs[PIXEL_EFFECT_REG:PIXEL_W_REG+1]}")
        color = (regs[PIXEL_R_REG], regs[PIXEL_G_REG], regs[PIXEL_B_REG])
        led.set_color(color)
        led.set_pattern(regs[PIXEL_EFFECT_REG])

    if ENABLE_REG in changed_regs:
        set_enable_pin()


def set_enable_pin(value=None):
    if value is None:
        value = regs[ENABLE_REG]
    else:
        regs[ENABLE_REG] = value

    if value > 0:
        logger.info("enabling mpq4242")
        enable_pin.value = True
    else:
        logger.info("disabling mpq4242")
        enable_pin.value = False


class StatusLed:
    SOLID = 0
    PULSE = 1
    BLINK = 2

    def __init__(self, brightness=0.1, pixel_num=1, pin=board.GP1):
        self.pixels = neopixel.NeoPixel(
            pin, pixel_num, brightness=brightness, auto_write=False
        )
        self.color = JADE
        self.pattern = None
        self.set_pattern(self.SOLID)

    def set_color(self, new_color):
        logger.info(f"Setting color to {new_color}")
        self.color = new_color
        self._pattern.color = new_color

    def set_pattern(self, new_pattern):
        logger.info(f"Setting pattern to {new_pattern}")
        if new_pattern == self.pattern:
            return
        if new_pattern == self.SOLID:
            self._pattern = Solid(self.pixels, color=self.color)
        elif new_pattern == self.PULSE:
            self._pattern = Pulse(self.pixels, speed=0.1, color=self.color)
        elif new_pattern == self.BLINK:
            self._pattern = Blink(self.pixels, speed=0.5, color=self.color)
        else:
            raise ValueError("Invalid pattern")
        self.pattern = new_pattern

    def animate(self):
        self._pattern.animate()


if __name__ == "__main__":
    main()
