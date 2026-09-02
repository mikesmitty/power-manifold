#pragma once

#include <stdint.h>

// Core 1 entry point: the charger management engine. Sole owner of the
// backplane I2C bus, the GPIO expander, and the LED chain. Communicates with
// core 0 exclusively through ipc.h.

void engine_main(void);

// Where engine_main has got to; core 0 reports it when the heartbeat is
// missing so a core 1 death can be placed (0 = never started).
extern volatile uint8_t engine_stage;
extern volatile uint32_t engine_stage_us; // time_us_32() when engine_stage last changed
#define ENGINE_STAGE_ENTERED   1
#define ENGINE_STAGE_BUS_INIT  2 // i2c/gpio (or sim) ready
#define ENGINE_STAGE_MUX_EXP   3 // tca9548a + tca9539 init done
#define ENGINE_STAGE_LEDS      4 // leds_init done
#define ENGINE_STAGE_FSM       5 // budget/fsm/fan init done
#define ENGINE_STAGE_IRQS      6 // alert/expander IRQs enabled
#define ENGINE_STAGE_PRESENCE  7 // first presence read done
#define ENGINE_STAGE_LOOP      8 // supervisory loop running
