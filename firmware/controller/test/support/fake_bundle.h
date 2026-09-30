#pragma once

#include <stdint.h>

#include "blade_image.h"

// The gen-3 blade firmware a test's controller carries (blade_bundle.h):
// none until a test builds one.

// A synthetic image of `length` bytes (a multiple of 8, at least a page)
// with a good header naming the version, filled with a pattern; the buffer
// is the fake's own and stays until the next call or a reset.
const uint8_t *fake_bundle_make(uint8_t major, uint8_t minor, uint8_t patch, uint32_t length);
void fake_bundle_clear(void);
