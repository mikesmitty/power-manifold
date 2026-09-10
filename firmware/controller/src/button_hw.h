#pragma once

#include <stdbool.h>

// GPIO seam under the front-panel button (button.c): button_hw_pico.c reads
// PIN_BUTTON against the pad's pull-up; the host tests script the level.

bool button_hw_init(void);    // false on a board with no button input
bool button_hw_pressed(void); // debounced by button.c, not here
