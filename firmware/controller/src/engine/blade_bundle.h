#pragma once

#include <stdint.h>

#include "blade_image.h"

// The gen-3 blade firmware this controller carries (CMake BLADE_IMAGE: the
// blade build's charger-module.bin, linked in as-is). The engine programs
// it into blades found in their ROM bootloader and, when the setting says
// so, into blades running anything else. A build without one still starts
// blades whose flash holds a good image, and can do nothing for a blank one.

const uint8_t *blade_bundle_data(void);
uint32_t       blade_bundle_size(void); // 0: none bundled
// The image's own header, or NULL when there is no image or it is not one
// (no magic, a length past the part or under the bytes bundled).
const blade_image_header_t *blade_bundle_header(void);
