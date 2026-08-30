#include "cli.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hardware/watchdog.h"
#include "pico/bootrom.h"
#include "pico/stdlib.h"

#include "ipc.h"
#include "manifold.h"
#include "net/mqtt.h"
#include "net/net.h"
#include "settings.h"

#define CLI_LINE_MAX 160

static char line[CLI_LINE_MAX];
static size_t line_len;

static void print_help(void) {
    printf("commands:\n"
           "  status                       port table + chassis power\n"
           "  info                         firmware/network/broker state\n"
           "  wifi <ssid> [pass]           set WiFi credentials\n"
           "  mqtt <host> [port user pass] set MQTT broker (empty host disables)\n"
           "  name <device-name>           hostname / topic id\n"
           "  token <t>|clear              REST API bearer token\n"
           "  budget <watts>               chassis power budget\n"
           "  port <1-%d> on|off|reset|srccap\n"
           "  fan on|off\n"
           "  led <0-255>                  status LED brightness\n"
           "  save | defaults | reboot | bootsel\n",
           NUM_PORTS);
}

static void print_status(void) {
    telemetry_t t;
    ipc_snapshot_read(&t);
    printf("port state      attach pdo    mV     mA     mW  contract\n");
    for (int i = 0; i < NUM_PORTS; i++) {
        const port_telemetry_t *p = &t.port[i];
        printf("%4d %-10s %-6s %3u %5u %6ld %6lu %7lumW\n", i + 1,
               port_state_name((port_state_t)p->state), p->attached ? "yes" : "no",
               p->selected_pdo, p->bus_mv, (long)p->current_ma,
               (unsigned long)p->power_mw, (unsigned long)p->contract_mw);
    }
    printf("total %lumW reserved %lumW budget %lumW fan %s alert %s\n",
           (unsigned long)t.total_mw, (unsigned long)t.reserved_mw,
           (unsigned long)t.budget_mw, t.fan_on ? "on" : "off",
           t.alert_active ? "ACTIVE" : "clear");
}

static void print_info(void) {
    printf("power-manifold controller %s\n", FW_VERSION);
    printf("device name: %s\n", g_settings.device_name);
    printf("wifi: %s (%s)\n",
           g_settings.wifi_ssid[0] ? g_settings.wifi_ssid : "(unset)",
           net_up() ? net_ip_str() : "down");
    printf("mqtt: %s:%u (%s)\n",
           g_settings.mqtt_host[0] ? g_settings.mqtt_host : "(disabled)",
           g_settings.mqtt_port, mqtt_is_connected() ? "connected" : "down");
    printf("engine: %s\n", ipc_engine_alive() ? "running" : "STALLED");
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
        g_settings.budget_mw = (uint32_t)atoi(w) * 1000u;
        engine_cmd_t c = {.op = CMD_SET_BUDGET, .arg = g_settings.budget_mw};
        ipc_cmd_push(&c);
        printf("budget %luW\n", (unsigned long)(g_settings.budget_mw / 1000));
    } else if (!strcmp(cmd, "port")) {
        const char *n = strtok_r(NULL, " \t", &save);
        const char *op = strtok_r(NULL, " \t", &save);
        uint8_t port;
        if (!n || !op || !port_arg(n, &port)) { printf("usage: port <1-%d> on|off|reset|srccap\n", NUM_PORTS); return; }
        engine_cmd_t c = {.port = port};
        if (!strcmp(op, "on")) c.op = CMD_PORT_ENABLE;
        else if (!strcmp(op, "off")) c.op = CMD_PORT_DISABLE;
        else if (!strcmp(op, "reset")) c.op = CMD_PORT_HARD_RESET;
        else if (!strcmp(op, "srccap")) c.op = CMD_PORT_SRC_CAP;
        else { printf("unknown op '%s'\n", op); return; }
        printf(ipc_cmd_push(&c) ? "ok\n" : "queue full\n");
    } else if (!strcmp(cmd, "fan")) {
        const char *op = strtok_r(NULL, " \t", &save);
        if (!op) { printf("usage: fan on|off\n"); return; }
        engine_cmd_t c = {.op = CMD_FAN, .arg = !strcmp(op, "on")};
        printf(ipc_cmd_push(&c) ? "ok\n" : "queue full\n");
    } else if (!strcmp(cmd, "led")) {
        const char *b = strtok_r(NULL, " \t", &save);
        if (!b) { printf("usage: led <0-255>\n"); return; }
        g_settings.led_brightness = (uint8_t)atoi(b);
        engine_cmd_t c = {.op = CMD_LED_BRIGHTNESS, .arg = g_settings.led_brightness};
        ipc_cmd_push(&c);
        printf("ok\n");
    } else if (!strcmp(cmd, "save")) {
        printf(settings_save() ? "saved\n" : "save FAILED\n");
    } else if (!strcmp(cmd, "defaults")) {
        settings_defaults();
        printf("defaults loaded (not saved)\n");
    } else if (!strcmp(cmd, "reboot")) {
        watchdog_reboot(0, 0, 100);
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
