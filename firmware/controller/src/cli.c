#include "cli.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hardware/watchdog.h"
#include "pico/bootrom.h"
#include "pico/stdlib.h"
#if PWRMAN_NET_WIFI
#include "pico/cyw43_arch.h"
#endif

#include "engine/engine.h"
#include "fault_log.h"
#include "fault_trap.h"
#include "flash_map.h"
#include "ipc.h"
#include "manifold.h"
#include "net/eth.h"
#include "net/improv.h"
#include "net/mqtt.h"
#include "net/net.h"
#include "net/ota_pull.h"
#include "settings.h"
#include "stack_probe.h"
#include "update.h"

#define CLI_LINE_MAX 160

static char line[CLI_LINE_MAX];
static size_t line_len;

static void print_help(void) {
    printf("commands:\n"
           "  status                       port table + chassis power\n"
           "  info                         firmware/network/broker state\n"
           "  wifi <ssid> [pass]           set WiFi credentials\n"
           "  improv [on|off]              BLE provisioning window (Improv Wi-Fi)\n"
           "  mqtt <host> [port user pass] set MQTT broker (empty host disables)\n"
           "  name <device-name>           hostname / topic id\n"
           "  token <t>|clear              REST API bearer token\n"
           "  budget <watts>               chassis power budget\n"
           "  port <1-%d> on|off|reset|srccap\n"
           "  port <1-%d> priority <0-255>    0 = highest; sheds from the bottom\n"
           "  fan on|off|auto [on_w off_w [on_ma]]\n"
           "                               auto: on at total >= on_w or any contract > on_ma\n"
           "  led <0-255>                  status LED brightness\n"
           "  faults [clear]               persistent fault log\n"
           "  stack                        per-core stack high-water marks\n"
           "  update <http-url>            OTA pull into the inactive slot\n"
           "  save | defaults | reboot | bootsel\n",
           NUM_PORTS, NUM_PORTS);
}

static void print_status(void) {
    telemetry_t t;
    ipc_snapshot_read(&t);
    printf("port state      attach pdo    mV     mA     mW  contract prio\n");
    for (int i = 0; i < NUM_PORTS; i++) {
        const port_telemetry_t *p = &t.port[i];
        printf("%4d %-10s %-6s %3u %5u %6ld %6lu %7lumW %4u\n", i + 1,
               port_state_name((port_state_t)p->state), p->attached ? "yes" : "no",
               p->selected_pdo, p->bus_mv, (long)p->current_ma,
               (unsigned long)p->power_mw, (unsigned long)p->contract_mw,
               g_settings.port_priority[i]);
    }
    printf("total %lumW reserved %lumW budget %lumW fan %s%s alert %s\n",
           (unsigned long)t.total_mw, (unsigned long)t.reserved_mw,
           (unsigned long)t.budget_mw, t.fan_on ? "on" : "off",
           t.fan_auto ? " (auto)" : "", t.alert_active ? "ACTIVE" : "clear");
}

static void print_info(void) {
    printf("power-manifold controller %s\n", FW_VERSION);
    printf("boot: slot %s%s\n", flash_map_slot_name(),
           flash_map_update_pending() ? " (TRIAL, uncommitted)" : "");
    printf("device name: %s\n", g_settings.device_name);
#if PWRMAN_NET_WIFI
    printf("wifi: %s (%s)\n",
           g_settings.wifi_ssid[0] ? g_settings.wifi_ssid : "(unset)",
           net_link_status() == CYW43_LINK_UP ? "up" : "down");
#else
    printf("wifi: not fitted\n");
#endif
#if PWRMAN_NET_ETH
    printf("eth: %s\n", eth_status_str());
#endif
    printf("ip: %s\n", net_up() ? net_ip_str() : "none");
    printf("mqtt: %s:%u (%s)\n",
           g_settings.mqtt_host[0] ? g_settings.mqtt_host : "(disabled)",
           g_settings.mqtt_port, mqtt_is_connected() ? "connected" : "down");
    if (improv_available()) {
        uint32_t left = improv_window_left_s(to_ms_since_boot(get_absolute_time()));
        printf("ble: improv %s", improv_state_str());
        if (left) printf(" (closes in %lus)", (unsigned long)left);
        printf("\n");
    } else {
        printf("ble: unavailable\n");
    }
    if (ipc_engine_alive())
        printf("engine: running\n");
    else
        printf("engine: STALLED (reached init stage %u of %u at %lu us)\n", engine_stage,
               ENGINE_STAGE_LOOP, (unsigned long)engine_stage_us);
    for (int core = 0; core < 2; core++) {
        const fault_record_t *f = &g_fault[core];
        if (f->hit)
            printf("HARDFAULT core%d: pc=%08lx lr=%08lx xpsr=%08lx cfsr=%08lx "
                   "hfsr=%08lx bfar=%08lx\n", core, (unsigned long)f->pc,
                   (unsigned long)f->lr, (unsigned long)f->xpsr, (unsigned long)f->cfsr,
                   (unsigned long)f->hfsr, (unsigned long)f->bfar);
    }
    if (update_active())
        printf("ota: receiving, %lu bytes into slot %s so far\n",
               (unsigned long)update_bytes(), update_slot_name());
}

static bool port_arg(const char *s, uint8_t *port) {
    int n = atoi(s);
    if (n < 1 || n > NUM_PORTS) {
        printf("port must be 1-%d\n", NUM_PORTS);
        return false;
    }
    *port = (uint8_t)(n - 1);
    return true;
}

static void run_line(char *l) {
    char *save = NULL;
    const char *cmd = strtok_r(l, " \t", &save);
    if (!cmd) return;

    if (!strcmp(cmd, "help")) {
        print_help();
    } else if (!strcmp(cmd, "status")) {
        print_status();
    } else if (!strcmp(cmd, "info")) {
        print_info();
    } else if (!strcmp(cmd, "wifi")) {
        const char *ssid = strtok_r(NULL, " \t", &save);
        const char *pass = strtok_r(NULL, "", &save);
        if (!ssid) { printf("usage: wifi <ssid> [pass]\n"); return; }
        snprintf(g_settings.wifi_ssid, sizeof(g_settings.wifi_ssid), "%s", ssid);
        snprintf(g_settings.wifi_pass, sizeof(g_settings.wifi_pass), "%s", pass ? pass : "");
        printf("wifi set; 'save' then 'reboot' to apply\n");
    } else if (!strcmp(cmd, "improv")) {
        const char *op = strtok_r(NULL, " \t", &save);
        if (!op) {
            printf("improv: %s\n", improv_available() ? improv_state_str() : "unavailable");
        } else if (!strcmp(op, "on")) {
            if (!improv_open(IMPROV_WINDOW_MS, "console")) printf("BLE unavailable\n");
        } else if (!strcmp(op, "off")) {
            improv_close();
            printf("improv: off (automatic windows disabled until 'improv on' or reboot)\n");
        } else {
            printf("usage: improv [on|off]\n");
        }
    } else if (!strcmp(cmd, "mqtt")) {
        const char *host = strtok_r(NULL, " \t", &save);
        const char *port = strtok_r(NULL, " \t", &save);
        const char *user = strtok_r(NULL, " \t", &save);
        const char *pass = strtok_r(NULL, " \t", &save);
        snprintf(g_settings.mqtt_host, sizeof(g_settings.mqtt_host), "%s", host ? host : "");
        g_settings.mqtt_port = port ? (uint16_t)atoi(port) : 1883;
        snprintf(g_settings.mqtt_user, sizeof(g_settings.mqtt_user), "%s", user ? user : "");
        snprintf(g_settings.mqtt_pass, sizeof(g_settings.mqtt_pass), "%s", pass ? pass : "");
        printf("mqtt set; 'save' then 'reboot' to apply\n");
    } else if (!strcmp(cmd, "name")) {
        const char *n = strtok_r(NULL, " \t", &save);
        if (!n) { printf("usage: name <device-name>\n"); return; }
        snprintf(g_settings.device_name, sizeof(g_settings.device_name), "%s", n);
        printf("name set; 'save' then 'reboot' to apply\n");
    } else if (!strcmp(cmd, "token")) {
        const char *t = strtok_r(NULL, " \t", &save);
        if (!t) { printf("usage: token <t>|clear\n"); return; }
        snprintf(g_settings.api_token, sizeof(g_settings.api_token), "%s",
                 strcmp(t, "clear") ? t : "");
        printf("token %s\n", g_settings.api_token[0] ? "set" : "cleared");
    } else if (!strcmp(cmd, "budget")) {
        const char *w = strtok_r(NULL, " \t", &save);
        if (!w) { printf("usage: budget <watts>\n"); return; }
        int watts = atoi(w);
        if (watts < BUDGET_MIN_W || watts > BUDGET_MAX_W) {
            printf("budget must be %d-%dW\n", BUDGET_MIN_W, BUDGET_MAX_W);
            return;
        }
        g_settings.budget_mw = (uint32_t)watts * 1000u;
        engine_cmd_t c = {.op = CMD_SET_BUDGET, .arg = g_settings.budget_mw};
        ipc_cmd_push(&c);
        printf("budget %luW ('save' to persist)\n",
               (unsigned long)(g_settings.budget_mw / 1000));
    } else if (!strcmp(cmd, "port")) {
        const char *n = strtok_r(NULL, " \t", &save);
        const char *op = strtok_r(NULL, " \t", &save);
        uint8_t port;
        if (!n || !op || !port_arg(n, &port)) { printf("usage: port <1-%d> on|off|reset|srccap\n", NUM_PORTS); return; }
        if (!strcmp(op, "priority")) {
            const char *p = strtok_r(NULL, " \t", &save);
            if (!p) { printf("usage: port <1-%d> priority <0-255>\n", NUM_PORTS); return; }
            g_settings.port_priority[port] = (uint8_t)atoi(p);
            printf("port %u priority %u ('save' to persist)\n", port + 1,
                   g_settings.port_priority[port]);
            return;
        }
        engine_cmd_t c = {.port = port};
        if (!strcmp(op, "on")) c.op = CMD_PORT_ENABLE;
        else if (!strcmp(op, "off")) c.op = CMD_PORT_DISABLE;
        else if (!strcmp(op, "reset")) c.op = CMD_PORT_HARD_RESET;
        else if (!strcmp(op, "srccap")) c.op = CMD_PORT_SRC_CAP;
        else { printf("unknown op '%s'\n", op); return; }
        printf(ipc_cmd_push(&c) ? "ok\n" : "queue full\n");
    } else if (!strcmp(cmd, "fan")) {
        const char *op = strtok_r(NULL, " \t", &save);
        if (!op) { printf("usage: fan on|off|auto [on_w off_w [on_ma]]\n"); return; }
        if (!strcmp(op, "auto")) {
            const char *on_w = strtok_r(NULL, " \t", &save);
            const char *off_w = strtok_r(NULL, " \t", &save);
            const char *on_ma = strtok_r(NULL, " \t", &save);
            if (on_w && off_w) {
                int on = atoi(on_w), off = atoi(off_w);
                int ma = on_ma ? atoi(on_ma) : (int)g_settings.fan_on_ma;
                if (on <= 0 || off < 0 || off >= on || on > 1000) {
                    printf("need 0 <= off_w < on_w <= 1000\n");
                    return;
                }
                if (ma < 0 || ma > 10000) {
                    printf("need 0 <= on_ma <= 10000 (0 disables the current rule)\n");
                    return;
                }
                g_settings.fan_on_w = (uint16_t)on;
                g_settings.fan_off_w = (uint16_t)off;
                g_settings.fan_on_ma = (uint16_t)ma;
            }
            g_settings.fan_auto = 1;
            engine_cmd_t c = {.op = CMD_FAN_AUTO};
            ipc_cmd_push(&c);
            printf("fan auto: on >= %uW", g_settings.fan_on_w);
            if (g_settings.fan_on_ma)
                printf(" or any contract > %umA", g_settings.fan_on_ma);
            printf(", off <= %uW ('save' to persist)\n", g_settings.fan_off_w);
        } else {
            g_settings.fan_auto = 0;
            engine_cmd_t c = {.op = CMD_FAN, .arg = !strcmp(op, "on")};
            printf(ipc_cmd_push(&c) ? "fan manual ('save' to persist the mode)\n"
                                    : "queue full\n");
        }
    } else if (!strcmp(cmd, "led")) {
        const char *b = strtok_r(NULL, " \t", &save);
        if (!b) { printf("usage: led <0-255>\n"); return; }
        g_settings.led_brightness = (uint8_t)atoi(b);
        engine_cmd_t c = {.op = CMD_LED_BRIGHTNESS, .arg = g_settings.led_brightness};
        ipc_cmd_push(&c);
        printf("ok\n");
    } else if (!strcmp(cmd, "update")) {
        const char *url = strtok_r(NULL, " \t", &save);
        if (!url) { printf("usage: update <http://host[:port]/controller.uf2>\n"); return; }
        char e[96];
        if (ota_pull_start(url, e, sizeof(e)))
            printf("pulling; progress lands on this console\n");
        else
            printf("update: %s\n", e);
    } else if (!strcmp(cmd, "stack")) {
        for (int core = 0; core < 2; core++) {
            uint32_t free = stack_probe_free_min(core);
            printf("core%d (%s): %lu of %lu bytes never touched%s\n", core,
                   core ? "engine" : "net/ui", (unsigned long)free,
                   (unsigned long)stack_probe_size(core),
                   free == 0 ? "  ** OVERFLOWED into the other core's stack **" : "");
        }
    } else if (!strcmp(cmd, "faults")) {
        const char *op = strtok_r(NULL, " \t", &save);
        if (op && !strcmp(op, "clear")) {
            printf(fault_log_clear() ? "fault log cleared\n"
                                     : "fault log clear failed\n");
            return;
        }
        if (!fault_log_available()) {
            printf("fault log needs the data partition (partition table not flashed?)\n");
            return;
        }
        int n = fault_log_count();
        printf("%d fault record(s)%s\n", n, n > 20 ? ", newest 20:" : "");
        for (int i = 0; i < n && i < 20; i++) {
            fault_rec_t r;
            if (!fault_log_get(i, &r)) break;
            const char *type = r.type == EVT_FAULT ? "fault" : "probe_fail";
            char when[24];
            if (r.epoch)
                snprintf(when, sizeof(when), "epoch %lu", (unsigned long)r.epoch);
            else
                snprintf(when, sizeof(when), "up %lus", (unsigned long)r.uptime_s);
            if (r.port == 0xFF)
                printf("#%-4lu %-10s chassis   code %-3u              (%s)\n",
                       (unsigned long)r.seq, type, r.code, when);
            else
                printf("#%-4lu %-10s port %u    code 0x%02x arg %-3lu %lu/%lumW (%s)\n",
                       (unsigned long)r.seq, type, r.port + 1, r.code,
                       (unsigned long)r.arg, (unsigned long)r.power_mw,
                       (unsigned long)r.contract_mw, when);
        }
    } else if (!strcmp(cmd, "save")) {
        printf(settings_save() ? "saved\n" : "save FAILED\n");
    } else if (!strcmp(cmd, "defaults")) {
        settings_defaults();
        printf("defaults loaded (not saved)\n");
    } else if (!strcmp(cmd, "reboot")) {
        // Immediate: a delayed reboot never fires while the main loop keeps
        // feeding the watchdog (it reloads the countdown every pass).
        printf("rebooting\n");
        sleep_ms(20); // let the console flush
        watchdog_reboot(0, 0, 0);
    } else if (!strcmp(cmd, "bootsel")) {
        reset_usb_boot(0, 0);
    } else {
        printf("unknown command '%s' (try 'help')\n", cmd);
    }
}

void cli_init(void) {
    line_len = 0;
}

void cli_poll(void) {
    for (;;) {
        int c = getchar_timeout_us(0);
        if (c == PICO_ERROR_TIMEOUT) return;
        if (c == '\r' || c == '\n') {
            printf("\n");
            line[line_len] = '\0';
            if (line_len) run_line(line);
            line_len = 0;
            printf("> ");
        } else if (c == 0x7F || c == '\b') {
            if (line_len) {
                line_len--;
                printf("\b \b");
            }
        } else if (line_len < CLI_LINE_MAX - 1 && c >= 0x20 && c < 0x7F) {
            line[line_len++] = (char)c;
            putchar(c);
        }
    }
}
