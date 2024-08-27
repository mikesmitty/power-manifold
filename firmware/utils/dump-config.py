import busio
import board
from adafruit_bus_device.i2c_device import I2CDevice

with busio.I2C(board.GP5, board.GP4) as i2c:
    device = I2CDevice(i2c, 0x61)
    reg_addr = bytearray(1)
    reg_byte = bytearray(1)

    for register in range(0x00, 0x39):
        reg_addr[0] = register
        with device:
            device.write_then_readinto(reg_addr, reg_byte)
            print(f"Register 0x{register:02X} = {reg_byte[0]:08b} {reg_byte[0]}")
