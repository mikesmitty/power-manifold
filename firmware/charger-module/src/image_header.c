#include "blade_image.h"
#include "blade_regs.h"

// The image header (blade_image.h), placed by the linker script right after
// the vector table. The length is the linker's: everything objcopy puts in
// the binary, rounded up to the flash's double word.
extern uint32_t _image_end[];

__attribute__((section(".image_header"), used))
const blade_image_header_t blade_image_header = {
    .magic = BLADE_IMAGE_MAGIC,
    .major = FW_MAJOR,
    .minor = FW_MINOR,
    .patch = FW_PATCH,
    .proto = BLADE_PROTO_VERSION,
    .length = (uint32_t)_image_end - BLADE_IMAGE_BASE,
    .reserved = 0,
};
