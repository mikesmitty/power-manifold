#include "blade_bundle.h"

#include <string.h>

#if defined(PWRMAN_HOST_TEST)
// the tests' support/fake_bundle.c hands out whatever image a test sets
#elif defined(PWRMAN_BLADE_BUNDLE)
extern const uint8_t blade_bundle_start[];
extern const uint8_t blade_bundle_end[];

const uint8_t *blade_bundle_data(void) { return blade_bundle_start; }
uint32_t blade_bundle_size(void) { return (uint32_t)(blade_bundle_end - blade_bundle_start); }
#else
const uint8_t *blade_bundle_data(void) { return NULL; }
uint32_t blade_bundle_size(void) { return 0; }
#endif

const blade_image_header_t *blade_bundle_header(void) {
    uint32_t size = blade_bundle_size();
    if (size < BLADE_IMAGE_HEADER_OFFSET + sizeof(blade_image_header_t)) return NULL;
    static blade_image_header_t h; // the bundle need not be aligned
    memcpy(&h, blade_bundle_data() + BLADE_IMAGE_HEADER_OFFSET, sizeof h);
    if (h.magic != BLADE_IMAGE_MAGIC || h.length > BLADE_IMAGE_MAX || h.length & 7 ||
        h.length < size || h.length - size > 7)
        return NULL; // the binary is the image up to the flash's double word
    return &h;
}
