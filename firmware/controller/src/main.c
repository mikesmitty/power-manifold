#include <stdio.h>

#include "hardware/watchdog.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"

#include "boot_reason_hw.h"
#include "cli.h"
#include "engine/engine.h"
#include "fault_log.h"
#include "flash_map.h"
#include "ipc.h"
#include "led_sched.h"
#include "log_sink.h"
#include "manifold.h"
#include "net/http.h"
#include "net/improv.h"
#include "net/mqtt.h"
#include "net/net.h"
#include "settings.h"
#include "stack_probe.h"
#include "update.h"

#define WATCHDOG_TIMEOUT_MS 5000

// Try-before-you-buy: a trial image commits itself only after this much
// continuous health (engine heartbeat + network up when one is configured).
// If it never gets there, reboot; the bootrom then falls back to the
// previous image, since an uncommitted trial is skipped on a normal boot.
#define UPDATE_HEALTH_MS   (10 * 1000)
#define UPDATE_DEADLINE_MS (10 * 60 * 1000)

int main(void) {
    stack_probe_paint(); // before anything deepens the stack
    boot_reason_read();  // before anything else can touch the watchdog scratch
    stdio_init_all();
    log_sink_init(); // from here on the console is mirrored for syslog / the API
    flash_map_init();
    settings_load();
    fault_log_init();
    ipc_init();

    // Core 1: charger management engine (sole owner of I2C/expander/LEDs).
    // Launched before any networking so power supervision never waits on it.
    multicore_launch_core1(engine_main);

    cli_init();
    net_init();
    http_init();

    char boot_text[80];
    boot_reason_text(boot_reason_last(), boot_text, sizeof(boot_text));
    printf("power-manifold controller %s (slot %s%s, boot: %s; 'help' for console)\n> ",
           FW_VERSION, flash_map_slot_name(),
           flash_map_update_pending() ? ", TRIAL" : "", boot_text);

    // Arm the watchdog only once the engine has proven alive; afterwards it is
    // fed only while BOTH cores make progress (this loop running + engine
    // heartbeat fresh), so either core stalling reboots the system.
    bool wd_armed = false;
    bool boot_logged = false; // one fault-log record per boot, once flash writes are safe
    bool trial = flash_map_update_pending();
    uint32_t healthy_since = 0;
    uint8_t led_flags_sent = 0; // engine's view starts with no chassis overlay
    uint8_t led_level_sent = g_settings.led_brightness; // what the engine applied at init
    uint8_t led_base_seen = g_settings.led_brightness;

    for (;;) {
        uint32_t now_ms = to_ms_since_boot(get_absolute_time());

        cli_poll();
        improv_poll(now_ms); // before net_poll: sees a join result before the retry
        net_poll(now_ms);
        mqtt_poll(now_ms);
        log_sink_poll(now_ms);

        // chassis conditions the LED chain overlays as a comet (led_pattern.h)
        uint8_t led_flags = (uint8_t)((improv_active() ? LED_CHASSIS_BLE_OPEN : 0) |
                                      (net_up() ? 0 : LED_CHASSIS_NET_DOWN));
        if (led_flags != led_flags_sent) {
            engine_cmd_t c = {.op = CMD_LED_CHASSIS, .arg = led_flags};
            if (ipc_cmd_push(&c)) led_flags_sent = led_flags;
        }

        // LED schedule: night window / idle dimming, re-pushed whenever the
        // effective level or the base brightness moved (a direct `led N`
        // from any surface shows at once, then the schedule has its say)
        led_mode_t lm = led_sched_update(&g_settings, net_epoch(), now_ms);
        uint8_t led_level = led_sched_level(&g_settings, lm);
        if (led_level != led_level_sent || g_settings.led_brightness != led_base_seen) {
            engine_cmd_t c = {.op = CMD_LED_BRIGHTNESS, .arg = led_level};
            if (ipc_cmd_push(&c)) {
                led_level_sent = led_level;
                led_base_seen = g_settings.led_brightness;
            }
        }

        // engine events: log faults durably first, then publish (best-effort)
        engine_evt_t evt;
        while (ipc_evt_pop(&evt)) {
            if (evt.type == EVT_STATE_CHANGE) led_sched_activity(now_ms); // wakes the LEDs
            fault_log_event(&evt);
            mqtt_event(&evt);
            // a port that switched itself off is administratively off now:
            // the "last" boot policy records it like a switch from any surface
            if (evt.type == EVT_CHARGE && evt.code == CHARGE_AUTO_OFF && evt.port < NUM_PORTS &&
                settings_port_admin_note(evt.port, false))
                settings_save_later();
        }

        if (http_reboot_due(now_ms)) {
            // asked for from the web UI after a settings change; the CLI's
            // 'reboot' is immediate for the same watchdog reason
            if (settings_save_pending()) settings_save();
            printf("http: rebooting\n");
            sleep_ms(20);
            boot_reason_mark(BOOT_REQUESTED, 0, 0, 0, 0);
            watchdog_reboot(0, 0, 0);
        }
        if (update_reboot_due()) {
            // scheduled by the OTA endpoint once its 200 response is queued
            printf("update: rebooting into slot %s (trial)\n", update_slot_name());
            sleep_ms(20); // let the CDC console flush
            update_reboot_now();
        }

        if (ipc_engine_alive()) {
            if (!wd_armed) {
                watchdog_enable(WATCHDOG_TIMEOUT_MS, true);
                wd_armed = true;
            }
            watchdog_update();

            if (!boot_logged) {
                boot_logged = true;
                fault_log_boot(boot_reason_last());
            }

            if (settings_migration_pending()) {
                printf(settings_migrate()
                           ? "settings: migrated into the data partition\n"
                           : "settings: migration save failed; still on legacy sectors\n");
            }

            int saved = settings_save_poll(now_ms);
            if (saved) {
                printf(saved > 0 ? "settings: saved (remote change)\n"
                                 : "settings: save failed\n");
            }
        }

        if (trial) {
            bool healthy = ipc_engine_alive() &&
                           (!g_settings.wifi_ssid[0] || net_up());
            if (!healthy) {
                healthy_since = 0;
            } else if (!healthy_since) {
                healthy_since = now_ms ? now_ms : 1;
            } else if (now_ms - healthy_since >= UPDATE_HEALTH_MS) {
                if (flash_map_commit_update()) {
                    printf("update: slot %s committed\n", flash_map_slot_name());
                    trial = false;
                } else {
                    printf("update: commit failed, retrying\n");
                    healthy_since = 0;
                }
            }
            if (trial && now_ms >= UPDATE_DEADLINE_MS) {
                printf("update: never became healthy; reverting to previous image\n");
                sleep_ms(50);
                boot_reason_mark(BOOT_TRIAL_REVERT, 0, 0, 0, 0);
                watchdog_reboot(0, 0, 0);
            }
        }

        sleep_ms(2);
    }
}
