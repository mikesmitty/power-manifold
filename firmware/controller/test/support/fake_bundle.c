#include "fake_bundle.h"

#include <string.h>

#include "blade_bundle.h"
#include "blade_regs.h"

static uint8_t image[BLADE_IMAGE_MAX];
static uint32_t image_len;

const uint8_t *fake_bundle_make(uint8_t major, uint8_t minor, uint8_t patch, uint32_t length) {
    if (length > sizeof image) length = sizeof image;
    for (uint32_t i = 0; i < length; i++) image[i] = (uint8_t)(i * 7 + major + minor + patch);
    blade_image_header_t h = {
        .magic = BLADE_IMAGE_MAGIC, .major = major, .minor = minor, .patch = patch,
        .proto = BLADE_PROTO_VERSION, .length = length, .reserved = 0,
    };
    memcpy(image + BLADE_IMAGE_HEADER_OFFSET, &h, sizeof h);
    image_len = length;
    return image;
}

void fake_bundle_clear(void) { image_len = 0; }

const uint8_t *blade_bundle_data(void) { return image; }
uint32_t blade_bundle_size(void) { return image_len; }
