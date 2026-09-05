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

#include "boot_reason_hw.h"
#include "civil_time.h"
#include "engine/engine.h"
#include "engine/sim/sim_inject.h"
#include "fault_log.h"
#include "fault_text.h"
#include "fault_trap.h"
#include "flash_map.h"
#include "health.h"
#include "ipc.h"
#include "led_sched.h"
#include "log_sink.h"
#include "manifold.h"
#include "net/eth.h"
#include "net/improv.h"
#include "net/mqtt.h"
#include "net/net.h"
#include "net/ota_pull.h"
#include "settings.h"
#include "settings_json.h"
#include "stack_probe.h"
#include "update.h"

#define CLI_LINE_MAX 160

static char line[CLI_LINE_MAX];
static size_t line_len;

#ifdef PWRMAN_FAKE_BLADES
static bool sim_paused; // core 0's view of the demo script (the engine owns the sim)
#endif
static bool port_arg(const char *s, uint8_t *port);

static void print_help(void) {
    printf("commands:\n"
           "  status                       port table + chassis power\n"
           "  info                         firmware/network/broker state\n"
           "  wifi <ssid> [pass]           set WiFi credentials\n"
           "  improv [on|off]              BLE provisioning window (Improv Wi-Fi)\n"
           "  mqtt <host> [port user pass] set MQTT broker (empty host disables)\n"
           "  ip dhcp | ip static <addr> <mask> <gw>\n"
           "                               addressing (wired link if a W6100 is fitted, else WiFi)\n"
           "  dns <addr>|auto              resolver override (auto: DHCP's, or the gateway when static)\n"
           "  syslog <host> [port] | syslog off\n"
           "                               mirror the console to a UDP syslog host (RFC 5424)\n"
           "  name <device-name>           hostname / topic id\n"
           "  token <t>|clear              REST API bearer token\n"
           "  budget <watts>               chassis power budget\n"
           "  port <1-%d> on|off|reset|srccap\n"
           "  port <1-%d> priority <0-255>    0 = highest; sheds from the bottom\n"
           "  port <1-%d> name <text>|clear   label for the web UI and Home Assistant\n"
           "  port <1-%d> limit <500-5000>    advertised current ceiling, mA (all PDOs)\n"
           "  port <1-%d> boot on|off|last    state at power-up (last = as switched)\n"
           "  port <1-%d> autooff on|off      switch off once the sink is charged\n"
           "  port <1-%d> sleep <min>|off     switch off this long after a sink attaches\n"
           "  charged <mW> <minutes>       charged = draw under mW for minutes (0 mW = off)\n"
           "  fan on|off|auto [on_w off_w [on_ma]]\n"
           "                               auto: on at total >= on_w or any contract > on_ma\n"
           "  led <0-255>                  status LED brightness (0 = off, faults still show)\n"
           "  led boot white|rainbow       power-up sweep style (next boot)\n"
           "  led dim <0-255>              brightness while dimmed (night window or idle)\n"
           "  led night <HH:MM> <HH:MM>|off  dim between these local times\n"
           "  led idle <minutes>|off       dim after this long without a port event\n"
           "  tz <+HH:MM|-HH:MM>           local time = UTC + this (LED schedule)\n"
           "  faults [clear]               persistent fault log\n"
           "  export                       every setting as JSON (no passwords; POST it to import)\n"
           "  stack                        per-core stack high-water marks\n"
#ifdef PWRMAN_FAKE_BLADES
           "  sim ...                      fault injection on the simulated blades ('sim' for help)\n"
#endif
           "  update <http-url>            OTA pull into the inactive slot\n"
           "  save | defaults | reboot | bootsel\n",
           NUM_PORTS, NUM_PORTS, NUM_PORTS, NUM_PORTS, NUM_PORTS, NUM_PORTS, NUM_PORTS);
}

static void print_status(void) {
    telemetry_t t;
    ipc_snapshot_read(&t);
    printf("port state      attach pdo    mV     mA     mW  contract limit prio boot  name\n");
    for (int i = 0; i < NUM_PORTS; i++) {
        const port_telemetry_t *p = &t.port[i];
        printf("%4d %-10s %-6s %3u %5u %6ld %6lu %7lumW %5lu %4u %-5s %s\n", i + 1,
               port_state_name((port_state_t)p->state),
               p->attached ? (p->charged ? "chg" : "yes") : "no",
               p->selected_pdo, p->bus_mv, (long)p->current_ma,
               (unsigned long)p->power_mw, (unsigned long)p->contract_mw,
               (unsigned long)g_settings.port_limit_ma[i], g_settings.port_priority[i],
               settings_port_boot_name(g_settings.port_boot[i]), settings_port_name(i));
    }
    printf("total %lumW reserved %lumW budget %lumW fan %s%s alert %s\n",
           (unsigned long)t.total_mw, (unsigned long)t.reserved_mw,
           (unsigned long)t.budget_mw, t.fan_on ? "on" : "off",
           t.fan_auto ? " (auto)" : "", t.alert_active ? "ACTIVE" : "clear");
    printf("charged: under %umW for %umin", g_settings.charged_mw, g_settings.charged_min);
    if (!g_settings.charged_mw) printf(" (detection off)");
    for (int i = 0; i < NUM_PORTS; i++) {
        if (!((g_settings.port_auto_off >> i) & 1) && !g_settings.port_sleep_min[i]) continue;
        printf("; port %d:%s", i + 1, (g_settings.port_auto_off >> i) & 1 ? " off when charged" : "");
        if (g_settings.port_sleep_min[i]) printf(" sleep %umin", g_settings.port_sleep_min[i]);
    }
    printf("\n");
}

static void print_info(void) {
    printf("power-manifold controller %s\n", FW_VERSION);
    printf("boot: slot %s%s\n", flash_map_slot_name(),
           flash_map_update_pending() ? " (TRIAL, uncommitted)" : "");
    char boot_text[80];
    boot_reason_text(boot_reason_last(), boot_text, sizeof(boot_text));
    printf("last boot: %s\n", boot_text);
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
    printf("ip: %s (%s)\n", net_up() ? net_ip_str() : "none",
           g_settings.ip_static ? "static" : "dhcp");
    if (net_up()) printf("netmask: %s, gateway: %s\n", net_mask_str(), net_gw_str());
    if (g_settings.ip_static) { // one call per printf: net_ip4_str has a single buffer
        printf("static: %s", net_ip4_str(g_settings.ip_addr));
        printf("/%s", net_ip4_str(g_settings.ip_mask));
        printf(" via %s\n", net_ip4_str(g_settings.ip_gw));
    }
    printf("dns: %s%s\n", net_dns_str(), g_settings.ip_dns ? " (configured)" : "");
    printf("syslog: %s", log_sink_status());
    if (g_settings.syslog_host[0])
        printf(" (%s:%u)", g_settings.syslog_host, g_settings.syslog_port);
    printf("\n");
    printf("mqtt: %s:%u (%s)\n",
           g_settings.mqtt_host[0] ? g_settings.mqtt_host : "(disabled)",
           g_settings.mqtt_port, mqtt_is_connected() ? "connected" : "down");
    char night[16], tz[8];
    night_format(night, sizeof(night), g_settings.led_night_start, g_settings.led_night_end);
    printf("leds: brightness %u, boot %s, dim %u; night %s; idle %s", g_settings.led_brightness,
           g_settings.led_boot == LED_BOOT_RAINBOW ? "rainbow" : "white", g_settings.led_dim,
           night[0] ? night : "off", g_settings.led_idle_min ? "after " : "off");
    if (g_settings.led_idle_min) printf("%u min", g_settings.led_idle_min);
    printf("; now %s\n", led_mode_name(led_sched_current()));
    int off = g_settings.tz_offset_min;
    snprintf(tz, sizeof(tz), "%c%02d:%02d", off < 0 ? '-' : '+', (off < 0 ? -off : off) / 60,
             (off < 0 ? -off : off) % 60);
    if (net_epoch()) {
        char when[24];
        civil_format(when, sizeof(when), net_epoch(), g_settings.tz_offset_min);
        printf("time: %s local (UTC%s)\n", when, tz);
    } else {
        printf("time: not synced yet (UTC%s)\n", tz);
    }
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
    telemetry_t t;
    ipc_snapshot_read(&t);
    char problems[192];
    health_problems(&t, problems, sizeof(problems));
    printf("problems: %s\n", problems[0] ? problems : "none");
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
#ifdef PWRMAN_FAKE_BLADES
    printf("simulator: demo script %s\n", sim_paused ? "paused ('sim run' resumes)" : "running");
#endif
}

#ifdef PWRMAN_FAKE_BLADES
static void print_sim_help(void) {
    printf("simulated backplane (FAKE_BLADES build); demo script %s\n"
           "  sim run | sim pause          the 60 s demo script (pause it before injecting)\n"
           "  sim seat <n> | sim unseat <n>\n"
           "  sim attach <n> <mV> <mA>     sink plugs in (e.g. 20000 5000 = 100 W laptop)\n"
           "  sim detach <n>\n"
           "  sim load <n> <pct>           measured draw as %% of the contract current\n"
           "  sim fault <n> ocp            INA226 over-current trip (latched alert)\n"
           "  sim fault <n> general|otw1|otw2|ntc1|ntc2|cc|short|vbatt|clear\n"
           "                               MPQ4242 fault bit, sticky until 'clear'\n"
           "  sim probe <n> ina|mpq|ok     the next probes fail (chip silent) or succeed\n"
           "  sim mux fail|ok              I2C mux select fails until the engine resets it\n"
           "  sim expander fail|ok         GPIO expander I/O fails until the engine resets it\n",
           sim_paused ? "paused" : "running");
}

static void run_sim(char **save) {
    const char *what = strtok_r(NULL, " \t", save);
    if (!what) { print_sim_help(); return; }
    engine_cmd_t c = {.op = CMD_SIM, .port = 0xFF};
    if (!strcmp(what, "run") || !strcmp(what, "pause")) {
        sim_paused = !strcmp(what, "pause");
        c.arg = sim_inject_pack(SIM_SCENARIO, 0, sim_paused ? 0 : 1);
    } else if (!strcmp(what, "mux") || !strcmp(what, "expander")) {
        const char *v = strtok_r(NULL, " \t", save);
        if (!v || (strcmp(v, "fail") && strcmp(v, "ok"))) { print_sim_help(); return; }
        c.arg = sim_inject_pack(!strcmp(what, "mux") ? SIM_MUX : SIM_EXPANDER, 0, !strcmp(v, "fail"));
    } else {
        const char *n = strtok_r(NULL, " \t", save);
        if (!n || !port_arg(n, &c.port)) { print_sim_help(); return; }
        if (!strcmp(what, "seat")) c.arg = sim_inject_pack(SIM_SEAT, 0, 0);
        else if (!strcmp(what, "unseat")) c.arg = sim_inject_pack(SIM_UNSEAT, 0, 0);
        else if (!strcmp(what, "detach")) c.arg = sim_inject_pack(SIM_DETACH, 0, 0);
        else if (!strcmp(what, "attach")) {
            const char *mv = strtok_r(NULL, " \t", save);
            const char *ma = strtok_r(NULL, " \t", save);
            int v = mv ? atoi(mv) : 0, a = ma ? atoi(ma) : 0;
            if (v < 3000 || v > 21000 || a < 100 || a > 5000) { printf("attach: 3000-21000 mV, 100-5000 mA\n"); return; }
            c.arg = sim_inject_pack(SIM_ATTACH, (unsigned)v, (unsigned)a);
        } else if (!strcmp(what, "load")) {
            const char *p = strtok_r(NULL, " \t", save);
            int pct = p ? atoi(p) : -1;
            if (pct < 0 || pct > 100) { printf("load: 0-100 %%\n"); return; }
            c.arg = sim_inject_pack(SIM_LOAD, 0, (unsigned)pct);
        } else if (!strcmp(what, "fault")) {
            const char *f = strtok_r(NULL, " \t", save);
            static const struct { const char *name; unsigned bit; } BITS[] = {
                {"general", MPQ_FAULT_GENERAL}, {"otw1", MPQ_FAULT_OTW1}, {"otw2", MPQ_FAULT_OTW2},
                {"ntc1", MPQ_FAULT_NTC1}, {"ntc2", MPQ_FAULT_NTC2}, {"cc", MPQ_FAULT_CC},
                {"short", MPQ_FAULT_SHORT_VBATT}, {"vbatt", MPQ_FAULT_VBATT_LOW}, {"clear", 0},
            };
            unsigned bit = 0xFFFF;
            if (f && !strcmp(f, "ocp")) {
                c.arg = sim_inject_pack(SIM_OCP, 0, 0);
            } else {
                for (size_t i = 0; f && i < sizeof(BITS) / sizeof(BITS[0]); i++)
                    if (!strcmp(f, BITS[i].name)) bit = BITS[i].bit;
                if (bit == 0xFFFF) { print_sim_help(); return; }
                c.arg = sim_inject_pack(SIM_MPQ_FAULT, 0, bit);
            }
        } else if (!strcmp(what, "probe")) {
            const char *v = strtok_r(NULL, " \t", save);
            unsigned mode = v && !strcmp(v, "ina") ? 1 : v && !strcmp(v, "mpq") ? 2 : v && !strcmp(v, "ok") ? 0 : 9;
            if (mode == 9) { print_sim_help(); return; }
            c.arg = sim_inject_pack(SIM_PROBE, 0, mode);
        } else {
            print_sim_help();
            return;
        }
    }
    printf(ipc_cmd_push(&c) ? "ok\n" : "queue full\n");
}
#endif

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
    } else if (!strcmp(cmd, "ip")) {
        const char *mode = strtok_r(NULL, " \t", &save);
        if (mode && !strcmp(mode, "dhcp")) {
            g_settings.ip_static = 0;
            printf("ip: dhcp; 'save' then 'reboot' to apply\n");
            return;
        }
        const char *a = strtok_r(NULL, " \t", &save);
        const char *m = strtok_r(NULL, " \t", &save);
        const char *g = strtok_r(NULL, " \t", &save);
        uint32_t addr, mask, gw;
        if (!mode || strcmp(mode, "static") || !a || !m || !g || !net_ip4_parse(a, &addr) ||
            !net_ip4_parse(m, &mask) || !net_ip4_parse(g, &gw) || !addr || !gw ||
            !net_ip4_mask_valid(mask)) {
            printf("usage: ip dhcp | ip static <addr> <netmask> <gateway>\n");
            return;
        }
        g_settings.ip_static = 1;
        g_settings.ip_addr = addr;
        g_settings.ip_mask = mask;
        g_settings.ip_gw = gw;
        printf("ip: static %s/%s via %s; 'save' then 'reboot' to apply\n", a, m, g);
    } else if (!strcmp(cmd, "dns")) {
        const char *a = strtok_r(NULL, " \t", &save);
        uint32_t addr = 0;
        if (!a || (strcmp(a, "auto") && (!net_ip4_parse(a, &addr) || !addr))) {
            printf("usage: dns <addr>|auto\n");
            return;
        }
        g_settings.ip_dns = addr;
        printf("dns: %s ('save' to persist; applies at once)\n", addr ? a : "auto");
    } else if (!strcmp(cmd, "syslog")) {
        const char *host = strtok_r(NULL, " \t", &save);
        const char *port = strtok_r(NULL, " \t", &save);
        int p = port ? atoi(port) : 514;
        if (!host || (port && (p < 1 || p > 65535)) || strlen(host) >= sizeof(g_settings.syslog_host)) {
            printf("usage: syslog <host> [port] | syslog off\n");
            return;
        }
        if (!strcmp(host, "off")) {
            g_settings.syslog_host[0] = '\0';
            printf("syslog off ('save' to persist)\n");
            return;
        }
        snprintf(g_settings.syslog_host, sizeof(g_settings.syslog_host), "%s", host);
        g_settings.syslog_port = (uint16_t)p;
        printf("syslog -> %s:%d; the backlog ships once it resolves ('save' to persist)\n", host, p);
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
        if (!n || !op || !port_arg(n, &port)) { printf("usage: port <1-%d> on|off|reset|srccap|priority|name|limit|boot|autooff|sleep\n", NUM_PORTS); return; }
        if (!strcmp(op, "autooff")) {
            const char *v = strtok_r(NULL, " \t", &save);
            if (!v || (strcmp(v, "on") && strcmp(v, "off"))) { printf("usage: port <1-%d> autooff on|off\n", NUM_PORTS); return; }
            uint8_t bit = (uint8_t)(1u << port);
            if (!strcmp(v, "on")) g_settings.port_auto_off |= bit;
            else g_settings.port_auto_off &= (uint8_t)~bit;
            printf("port %u %s once charged ('save' to persist)\n", port + 1,
                   !strcmp(v, "on") ? "switches off" : "stays on");
            return;
        }
        if (!strcmp(op, "sleep")) {
            const char *v = strtok_r(NULL, " \t", &save);
            int min = v && !strcmp(v, "off") ? 0 : v ? atoi(v) : -1;
            if (min < 0 || min > PORT_SLEEP_MAX_MIN || (v && !strcmp(v, "off") ? false : !v || (v[0] < '0' || v[0] > '9'))) {
                printf("usage: port <1-%d> sleep <1-%d>|off (minutes after a sink attaches)\n", NUM_PORTS, PORT_SLEEP_MAX_MIN);
                return;
            }
            g_settings.port_sleep_min[port] = (uint16_t)min;
            if (min) printf("port %u switches off %d min after a sink attaches ('save' to persist)\n", port + 1, min);
            else printf("port %u sleep timer off ('save' to persist)\n", port + 1);
            return;
        }
        if (!strcmp(op, "boot")) {
            const char *v = strtok_r(NULL, " \t", &save);
            if (!v || !settings_port_boot_parse(v, &g_settings.port_boot[port])) {
                printf("usage: port <1-%d> boot on|off|last\n", NUM_PORTS);
                return;
            }
            printf("port %u boots %s ('save' to persist)\n", port + 1, v);
            return;
        }
        if (!strcmp(op, "limit")) {
            const char *v = strtok_r(NULL, " \t", &save);
            int ma = v ? atoi(v) : 0;
            if (!v || ma < PORT_LIMIT_MIN_MA || ma > PORT_LIMIT_MAX_MA) {
                printf("usage: port <1-%d> limit <%d-%d> (mA)\n", NUM_PORTS, PORT_LIMIT_MIN_MA, PORT_LIMIT_MAX_MA);
                return;
            }
            g_settings.port_limit_ma[port] = (uint32_t)ma;
            engine_cmd_t c = {.op = CMD_PORT_LIMIT, .port = port, .arg = (uint32_t)ma};
            printf(ipc_cmd_push(&c) ? "port %u limit %d mA ('save' to persist)\n" : "queue full\n", port + 1, ma);
            return;
        }
        if (!strcmp(op, "name")) {
            char *text = strtok_r(NULL, "", &save); // the rest of the line, spaces included
            while (text && *text == ' ') text++;
            for (size_t len = text ? strlen(text) : 0; len && text[len - 1] == ' '; len--) text[len - 1] = '\0';
            if (!text || !*text) { printf("usage: port <1-%d> name <text>|clear\n", NUM_PORTS); return; }
            if (!strcmp(text, "clear")) text[0] = '\0';
            if (!settings_port_name_valid(text)) {
                printf("name: up to %d printable characters\n", PORT_NAME_MAX);
                return;
            }
            snprintf(g_settings.port_name[port], sizeof(g_settings.port_name[port]), "%s", text);
            mqtt_names_changed();
            printf("port %u name '%s' ('save' to persist)\n", port + 1, settings_port_name(port));
            return;
        }
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
        if (!ipc_cmd_push(&c)) { printf("queue full\n"); return; }
        bool remembered = (c.op == CMD_PORT_ENABLE || c.op == CMD_PORT_DISABLE) &&
                          settings_port_admin_note(port, c.op == CMD_PORT_ENABLE);
        if (remembered) settings_save_later(); // the "last" policy keeps it across reboots
        printf(remembered ? "ok (kept for the next boot)\n" : "ok\n");
    } else if (!strcmp(cmd, "charged")) {
        const char *mw = strtok_r(NULL, " \t", &save);
        const char *min = strtok_r(NULL, " \t", &save);
        int w = mw ? atoi(mw) : -1, m = min ? atoi(min) : (int)g_settings.charged_min;
        if (!mw || w < 0 || w > 20000 || m < 1 || m > 255) {
            printf("usage: charged <0-20000 mW> [1-255 minutes]\n");
            return;
        }
        g_settings.charged_mw = (uint16_t)w;
        g_settings.charged_min = (uint8_t)m;
        if (w) printf("charged = under %dmW for %dmin ('save' to persist)\n", w, m);
        else printf("charge detection off ('save' to persist)\n");
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
    } else if (!strcmp(cmd, "tz")) {
        const char *v = strtok_r(NULL, " \t", &save);
        uint16_t hm;
        if (!v || (v[0] != '+' && v[0] != '-') || !hhmm_parse(v + 1, &hm) || hm > 14 * 60) {
            printf("usage: tz <+HH:MM|-HH:MM> (e.g. -04:00)\n");
            return;
        }
        g_settings.tz_offset_min = (int16_t)(v[0] == '-' ? -(int)hm : (int)hm);
        printf("tz: UTC%s ('save' to persist)\n", v);
    } else if (!strcmp(cmd, "led")) {
        const char *b = strtok_r(NULL, " \t", &save);
        if (!b) { printf("usage: led <0-255> | led boot white|rainbow | led dim|night|idle ...\n"); return; }
        if (!strcmp(b, "dim")) {
            const char *v = strtok_r(NULL, " \t", &save);
            if (!v || v[0] < '0' || v[0] > '9' || atoi(v) > 255) { printf("usage: led dim <0-255>\n"); return; }
            g_settings.led_dim = (uint8_t)atoi(v);
            printf("led dim %u ('save' to persist)\n", g_settings.led_dim);
            return;
        }
        if (!strcmp(b, "night")) {
            const char *a = strtok_r(NULL, " \t", &save);
            const char *e = strtok_r(NULL, " \t", &save);
            uint16_t s, en;
            if (a && !strcmp(a, "off")) {
                g_settings.led_night_start = g_settings.led_night_end = 0;
                printf("led night off ('save' to persist)\n");
                return;
            }
            if (!a || !e || !hhmm_parse(a, &s) || !hhmm_parse(e, &en) || s == en) {
                printf("usage: led night <HH:MM> <HH:MM>|off (local time, may wrap midnight)\n");
                return;
            }
            g_settings.led_night_start = s;
            g_settings.led_night_end = en;
            printf("led night %s-%s ('save' to persist)\n", a, e);
            return;
        }
        if (!strcmp(b, "idle")) {
            const char *v = strtok_r(NULL, " \t", &save);
            int min = v && !strcmp(v, "off") ? 0 : v && v[0] >= '0' && v[0] <= '9' ? atoi(v) : -1;
            if (min < 0 || min > 1440) { printf("usage: led idle <1-1440>|off (minutes)\n"); return; }
            g_settings.led_idle_min = (uint16_t)min;
            if (min) printf("led idle: dim after %d min without a port event ('save' to persist)\n", min);
            else printf("led idle off ('save' to persist)\n");
            return;
        }
        if (!strcmp(b, "boot")) {
            const char *s = strtok_r(NULL, " \t", &save);
            if (s && !strcmp(s, "white")) g_settings.led_boot = LED_BOOT_WHITE;
            else if (s && !strcmp(s, "rainbow")) g_settings.led_boot = LED_BOOT_RAINBOW;
            else { printf("usage: led boot white|rainbow\n"); return; }
            printf("ok (shown at the next boot; 'save' to keep)\n");
            return;
        }
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
    } else if (!strcmp(cmd, "sim")) {
#ifdef PWRMAN_FAKE_BLADES
        run_sim(&save);
#else
        printf("no simulator in this build (FAKE_BLADES=OFF)\n");
#endif
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
            char text[64], when[24];
            fault_text(&r, text, sizeof(text));
            if (r.epoch)
                snprintf(when, sizeof(when), "epoch %lu", (unsigned long)r.epoch);
            else
                snprintf(when, sizeof(when), "up %lus", (unsigned long)r.uptime_s);
            if (r.port == 0xFF)
                printf("#%-4lu chassis %-40s (%s)\n", (unsigned long)r.seq, text, when);
            else
                printf("#%-4lu port %u  %-40s %lu/%lumW (%s)\n",
                       (unsigned long)r.seq, r.port + 1, text, (unsigned long)r.power_mw,
                       (unsigned long)r.contract_mw, when);
        }
    } else if (!strcmp(cmd, "export")) {
        static char json[2560]; // static: the console runs on core 0's small stack
        telemetry_t t;
        ipc_snapshot_read(&t);
        settings_json_opts_t o = {.fan_on = t.fan_on, .export = true};
        if (settings_json_build(json, sizeof(json), &g_settings, &o)) printf("%s\n", json);
        else printf("export: does not fit\n");
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
        boot_reason_mark(BOOT_REQUESTED, 0, 0, 0, 0);
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
        // the echo of what is typed (and the prompt) stays out of the log
        // sink: a `wifi` or `mqtt` line carries a password
        if (c == '\r' || c == '\n') {
            log_sink_pause(true);
            printf("\n");
            log_sink_pause(false);
            line[line_len] = '\0';
            if (line_len) run_line(line);
            line_len = 0;
            log_sink_pause(true);
            printf("> ");
            log_sink_pause(false);
        } else if (c == 0x7F || c == '\b') {
            if (line_len) {
                line_len--;
                log_sink_pause(true);
                printf("\b \b");
                log_sink_pause(false);
            }
        } else if (line_len < CLI_LINE_MAX - 1 && c >= 0x20 && c < 0x7F) {
            line[line_len++] = (char)c;
            log_sink_pause(true);
            putchar(c);
            log_sink_pause(false);
        }
    }
}
