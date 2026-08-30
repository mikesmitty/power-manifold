#pragma once

// Core 1 entry point: the charger management engine. Sole owner of the
// backplane I2C bus, the GPIO expander, and the LED chain. Communicates with
// core 0 exclusively through ipc.h.

void engine_main(void);
