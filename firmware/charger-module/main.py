import time
import board
import neopixel
import adafruit_logging as logging

from i2ctarget import I2CTarget
from adafruit_led_animation.animation.blink import Blink
from adafruit_led_animation.animation.pulse import Pulse
from adafruit_led_animation.animation.solid import Solid
from adafruit_led_animation.color import RED, ORANGE, YELLOW, GREEN, BLUE, WHITE, JADE


logger = logging.getLogger("i2ctarget")


def main():
    # logger.setLevel(logging.INFO)
    logger.setLevel(logging.DEBUG)
    logger.addHandler(logging.StreamHandler())

    scl = board.GP5
    sda = board.GP4
    i2c_addr = 0x4d

    status_colors = [
        RED,
        ORANGE,
        YELLOW,
        GREEN,
        BLUE,
        WHITE,
    ]

    led = StatusLed(status_colors)

    PIXEL_REG = 0

    while True:
        with I2CTarget(scl, sda, (i2c_addr,)) as device:
            logger.info("i2c listener started")
            while True:
                led.animate()

                # check if there's a pending device request
                i2c_request = device.request()

                if not i2c_request:
                    # no request is pending
                    continue

                with i2c_request:
                    address = i2c_request.address

                    if i2c_request.is_read:
                        logger.info(f"read request to address '0x{address:02x}'")
                        buffer = bytes([0xAA])
                        i2c_request.write(buffer)
                    else:
                        # transaction is a write request
                        data = i2c_request.read(2)
                        if len(data) == 0:
                            logger.error("no data received")
                            continue
                        elif data[0] == PIXEL_REG:
                            logger.info(f"Pixel write request: {data}")
                            # Top 3 bits of the first byte are the pattern
                            pattern = data[1] >> 5
                            # The rest are color
                            color = data[1] & 0b00011111
                            led.set_color(color)
                            led.set_pattern(pattern)


class StatusLed:
    SOLID = 0
    PULSE = 1
    BLINK = 2

    def __init__(self, status_colors, brightness=0.1, pixel_num=1, pin=board.GP1):
        self.pixels = neopixel.NeoPixel(
            pin, pixel_num, brightness=brightness, auto_write=False
        )
        self.color = JADE
        self.pattern = None
        self.status_colors = status_colors
        self.set_pattern(self.SOLID)

    def set_color(self, new_color):
        logger.info(f"Setting color to {new_color}")
        self._pattern.color = self.status_colors[new_color]
        self.color = self.status_colors[new_color]

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
