#include <stdio.h>

#include "hardware/watchdog.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"

#include "cli.h"
#include "engine/engine.h"
#include "ipc.h"
#include "manifold.h"
#include "net/http.h"
#include "net/mqtt.h"
#include "net/net.h"
#include "settings.h"

#define WATCHDOG_TIMEOUT_MS 5000

int main(void) {
    stdio_init_all();
    settings_load();
    ipc_init();

    // Core 1: charger management engine (sole owner of I2C/expander/LEDs).
    // Launched before any networking so power supervision never waits on it.
    multicore_launch_core1(engine_main);

    cli_init();
    net_init();
    http_init();

    printf("power-manifold controller %s ('help' for console)\n> ", FW_VERSION);

    // Arm the watchdog only once the engine has proven alive; afterwards it is
    // fed only while BOTH cores make progress (this loop running + engine
    // heartbeat fresh), so either core stalling reboots the system.
    bool wd_armed = false;

    for (;;) {
        uint32_t now_ms = to_ms_since_boot(get_absolute_time());

        cli_poll();
        net_poll(now_ms);
        mqtt_poll(now_ms);

        if (ipc_engine_alive()) {
            if (!wd_armed) {
                watchdog_enable(WATCHDOG_TIMEOUT_MS, true);
                wd_armed = true;
            }
            watchdog_update();
        }

        sleep_ms(2);
    }
}
