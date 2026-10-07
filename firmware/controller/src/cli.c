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
#include "button.h"
#include "civil_time.h"
#include "cli_line.h"
#include "engine/blade_bundle.h"
#include "engine/engine.h"
#include "engine/i2c_diag.h"
#include "engine/sim/sim_inject.h"
#include "fault_log.h"
#include "fault_text.h"
#include "fault_trap.h"
#include "flash_map.h"
#include "health.h"
#include "ipc.h"
#include "led_sched.h"
#include "blade_regs.h"
#include "log_sink.h"
#include "manifold.h"
#include "net/eth.h"
#include "net/http.h"
#include "net/http_req.h"
#include "net/https.h"
#include "net/improv.h"
#include "net/mqtt.h"
#include "net/mqtt_tls.h"
#include "net/net.h"
#include "net/ota_pull.h"
#include "net/update_check.h"
#include "settings.h"
#include "settings_json.h"
#include "stack_probe.h"
#include "update.h"
#include "update_latest.h"
#include "ups/lad_proto.h"
#include "ups/ups.h"
#include "bus_cap.h"
#include "vin.h"

#define CLI_LINE_MAX 160

static char line[CLI_LINE_MAX];
static size_t line_len;

// The web console's batch and its transcript. The HTTP handler (an
// interrupt on core 0) writes web_in only while the state is idle or done,
// and the main loop touches both buffers only while it is queued or running,
// so the two never write at once.
static char web_in[CLI_WEB_IN_MAX];
static char web_out[CLI_WEB_OUT_MAX];
static size_t web_out_len;
static bool web_out_cut;
static volatile cli_web_state_t web_state;

#ifdef PWRMAN_FAKE_BLADES
static bool sim_paused; // core 0's view of the demo script (the engine owns the sim)
#endif
static bool port_arg(const char *s, uint8_t *port);

static void print_help(void) {
    printf("commands:\n"
           "  status                       port table + chassis power\n"
           "  info                         firmware/network/broker state\n"
           "  wifi <ssid> [pass]           set WiFi credentials\n"
           "  improv [on|off]              Wi-Fi setup window (Improv Wi-Fi)\n"
           "  mqtt <host> [port user pass] set MQTT broker (empty host disables)\n"
           "  mqtt tls on|off|unverified   TLS to the broker: verified against the installed certificate, else Let's Encrypt\n"
           "  mqtt ca [clear]              the installed broker certificate (install one from the web UI)\n"
           "  ip dhcp | ip static <addr> <mask> <gw>\n"
           "                               addressing (wired link if a W6100 is fitted, else WiFi)\n"
           "  dns <addr>|auto              resolver override (auto: DHCP's, or the gateway when static)\n"
           "  ntp <host>|auto              time server (auto: DHCP's, else " NET_NTP_DEFAULT ")\n"
           "  hostnames <name>...|clear    more names the web server answers to (always: its address, name.local)\n"
           "  https [on|off|remove]        HTTPS with the installed certificate (install it over the API)\n"
           "  syslog <host> [port] | syslog off\n"
           "                               mirror the console to a UDP syslog host (RFC 5424)\n"
           "  name <device-name>           hostname / topic id\n"
           "  token <t>|clear              REST API bearer token\n"
           "  budget <watts>               chassis power budget\n"
           "  port <1-%d> on|off|reset|srccap|update\n"
           "                               update: write the bundled gen-3 blade firmware over what it runs\n"
           "  port <1-%d> priority <0-255>    0 = highest; sheds from the bottom\n"
           "  port <1-%d> name <text>|clear   label for the web UI and Home Assistant\n"
           "  port <1-%d> limit <500-5000>    advertised current ceiling, mA (21 V PPS: 4750 max)\n"
           "  port <1-%d> volt 5|9|12|15|20   voltage cap, V: the highest PDO advertised\n"
           "  port <1-%d> boot on|off|last    state at power-up (last = as switched)\n"
           "  port <1-%d> autooff on|off      switch off once the sink is charged\n"
           "  port <1-%d> sleep <min>|off     switch off this long after a sink attaches\n"
           "  port <1-%d> protect on|off      power sharing never changes a running device's power\n"
           "  charged <mW> <minutes>       charged = draw under mW for minutes (0 mW = off)\n"
           "  blades                       the bundled gen-3 blade firmware and the update policy\n"
           "  blades auto on|off           rewrite blades running another version (default on)\n"
           "  blades bootopt on|off        set blades to boot through their ROM bootloader (default on)\n"
           "  blades watch <s>|off         blades reset into it after this long without the controller\n"
           "  fan on|off|auto [on_w off_w [on_ma]]\n"
           "                               auto: on at total >= on_w or any contract > on_ma\n"
           "  led <0-255>                  status LED brightness (0 = off, faults still show)\n"
           "  led boot white|rainbow       power-up sweep style (next boot)\n"
           "  led dim <0-255>              brightness while dimmed (night window or idle)\n"
           "  led night <HH:MM> <HH:MM>|off  dim between these local times\n"
           "  led idle <minutes>|off       dim after this long without a port event\n"
           "  tz <+HH:MM|-HH:MM>           local time = UTC + this (LED schedule)\n"
           "  faults [clear]               persistent fault log\n"
           "  ups [buzzer on|off]          UPS supply readings; silence its buzzer (until it restarts)\n"
           "  vin [cal <volts>|cal reset]  DC bus voltage; trim it to a meter reading (then 'save')\n"
           "  button [short|long]          front-panel button state; act as if pressed\n"
           "  export                       every setting as JSON (no passwords; POST it to import)\n"
           "  stack                        per-core stack high-water marks\n"
#ifdef PWRMAN_FAKE_BLADES
           "  sim ...                      fault injection on the simulated blades ('sim' for help)\n"
#endif
           "  update [--unsigned] [--downgrade] <http-url>|latest\n"
           "                               OTA pull into the inactive slot ('latest': the newest known\n"
           "                               release); the flags let an unsigned or an older image in\n"
           "                               (this console only)\n"
           "  update check                 ask the update source for the newest release now\n"
           "  update source <http-url>|default|off\n"
           "                               where the daily check asks; 'off' = never ask (then 'save')\n"
           "  update auto on|off|postpone|skip\n"
           "                               install newer releases by themselves (then 'save'); put that\n"
           "                               off for 7 days, or skip the newest release\n"
           "  save | defaults | reboot | bootsel\n",
           NUM_PORTS, NUM_PORTS, NUM_PORTS, NUM_PORTS, NUM_PORTS, NUM_PORTS, NUM_PORTS, NUM_PORTS,
           NUM_PORTS);
}

static void print_status(void) {
    telemetry_t t;
    ipc_snapshot_read(&t);
    printf("port state      gen attach pdo    mV     mA     mW  contract limit cap prio boot  conv  plug  name\n");
    for (int i = 0; i < NUM_PORTS; i++) {
        const port_telemetry_t *p = &t.port[i];
        char conv[8], plug[8]; // a gen-3 blade's thermometers, degC
        port_temp_text(conv, sizeof(conv), p->temp_conv_dc, "-");
        port_temp_text(plug, sizeof(plug), p->temp_plug_dc, "-");
        char state[12];
        if (p->state == PORT_STATE_UPDATE) snprintf(state, sizeof state, "upd %3u%%", p->update_pct);
        else snprintf(state, sizeof state, "%s", port_state_name((port_state_t)p->state));
        printf("%4d %-10s %3s %-6s %3u %5u %6ld %6lu %7lumW %5lu %2uV %4u %-5s %5s %5s  %s%s%s\n", i + 1,
               state,
               p->gen == 3 ? "3" : p->gen == 2 ? "2" : "-",
               p->attached ? (p->charged ? "chg" : "yes") : "no",
               p->selected_pdo, p->bus_mv, (long)p->current_ma,
               (unsigned long)p->power_mw, (unsigned long)p->contract_mw,
               (unsigned long)g_settings.port_limit_ma[i], g_settings.port_max_mv[i] / 1000,
               g_settings.port_priority[i],
               settings_port_boot_name(g_settings.port_boot[i]), conv, plug, settings_port_name(i),
               p->silent ? "  [silent: last answer shown]" : "",
               p->update_due ? "  [blade update due when idle]" : "");
    }
    printf("total %lumW reserved %lumW budget %lumW fan %s%s alert %s",
           (unsigned long)t.total_mw, (unsigned long)t.reserved_mw,
           (unsigned long)t.budget_mw, t.fan_on ? "on" : "off",
           t.fan_auto ? " (auto)" : "", t.alert_active ? "ACTIVE" : "clear");
    if (vin_fitted()) printf(" bus %s%s", vin_status_str(), bus_cap_on() ? " (ports capped at 3 A)" : "");
    printf("\n");
    printf("charged: under %umW for %umin", g_settings.charged_mw, g_settings.charged_min);
    if (!g_settings.charged_mw) printf(" (detection off)");
    for (int i = 0; i < NUM_PORTS; i++) {
        bool off = (g_settings.port_auto_off >> i) & 1, prot = (g_settings.port_protect >> i) & 1;
        if (!off && !prot && !g_settings.port_sleep_min[i]) continue;
        printf("; port %d:%s%s", i + 1, prot ? " protected" : "", off ? " off when charged" : "");
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
    if (update_key_count())
        printf("updates: signed images only (%u key%s built in); unsigned or older ones from this console\n",
               update_key_count(), update_key_count() == 1 ? "" : "s");
    else
        printf("updates: NOT checked for a signature (no keys in this build)\n");
    char check[128];
    update_check_status(to_ms_since_boot(get_absolute_time()), check, sizeof(check));
    printf("update check: %s\n", check);
    net_lock(); // the MQTT pointer is offered from lwIP callbacks
    if (update_latest_newer_than(FW_VERSION))
        printf("update available: %s ('update latest' installs it)\n", update_latest_version());
    uint32_t wait_s;
    const char *autos = update_auto_state(to_ms_since_boot(get_absolute_time()), &wait_s);
    net_unlock();
    if (!g_settings.update_auto) printf("automatic installs: off\n");
    else if (wait_s) printf("automatic installs: on, %s for %lu h more\n", autos, (unsigned long)((wait_s + 3599) / 3600));
    else printf("automatic installs: on, %s%s%s\n", autos, g_settings.update_skip[0] ? ", skipping " : "", g_settings.update_skip);
    printf("device name: %s\n", g_settings.device_name);
#if PWRMAN_NET_WIFI
    printf("wifi: %s (%s)\n",
           g_settings.wifi_ssid[0] ? g_settings.wifi_ssid : "(unset)",
           net_link_status() == CYW43_LINK_UP ? "up" : "down");
#else
    printf("wifi: not present\n");
#endif
#if PWRMAN_NET_ETH
    printf("eth: %s\n", eth_status_str());
#endif
    printf("ups: %s\n", ups_status_str());
    printf("bus: %s", vin_status_str());
    if (vin_fitted()) printf(" (cal %u.%03u)", g_settings.vin_cal / 1000, g_settings.vin_cal % 1000);
    if (bus_cap_on()) printf(", ports capped at %u A until it holds %u.%u V for %u s", BUS_CAP_MA / 1000,
                             BUS_CAP_OFF_MV / 1000, (BUS_CAP_OFF_MV % 1000) / 100, BUS_CAP_HOLD_MS / 1000);
    printf("\n");
    printf("ip: %s (%s)\n", net_up() ? net_ip_str() : "none",
           g_settings.ip_static ? "static" : "dhcp");
    if (net_up()) printf("netmask: %s, gateway: %s\n", net_mask_str(), net_gw_str());
    if (g_settings.ip_static) { // one call per printf: net_ip4_str has a single buffer
        printf("static: %s", net_ip4_str(g_settings.ip_addr));
        printf("/%s", net_ip4_str(g_settings.ip_mask));
        printf(" via %s\n", net_ip4_str(g_settings.ip_gw));
    }
    printf("dns: %s%s\n", net_dns_str(), g_settings.ip_dns ? " (configured)" : "");
    printf("ntp: %s\n", net_ntp_str());
    printf("hostnames: %s.local%s%s\n", g_settings.device_name,
           g_settings.hostnames[0] ? " " : "", g_settings.hostnames);
    {
        char d[224];
        printf("https: %s", g_settings.https ? (https_enforced() ? "on" : "on, but no certificate") : "off");
        if (https_describe(d, sizeof(d))) printf(" (%s)", d);
        printf("\n");
    }
    printf("syslog: %s", log_sink_status());
    if (g_settings.syslog_host[0])
        printf(" (%s:%u)", g_settings.syslog_host, g_settings.syslog_port);
    printf("\n");
    printf("mqtt: %s:%u (%s, %s%s)\n",
           g_settings.mqtt_host[0] ? g_settings.mqtt_host : "(disabled)",
           g_settings.mqtt_port, mqtt_is_connected() ? "connected" : "down", mqtt_tls_mode_str(),
           mqtt_waiting_for_clock() ? ", waiting for the clock" : "");
    if (mqtt_problem()) printf("  not connecting: %s\n", mqtt_problem());
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
        printf("improv: %s", improv_state_str());
        if (left) printf(" (closes in %lus)", (unsigned long)left);
        printf("\n");
    } else {
        printf("improv: unavailable\n");
    }
    telemetry_t t;
    ipc_snapshot_read(&t);
    if (ipc_engine_alive()) {
        char start[64];
        health_start_text(&t, start, sizeof(start));
        printf("engine: running; %s\n", start);
    } else {
        printf("engine: STALLED (reached init stage %u of %u at %lu us)\n", engine_stage,
               ENGINE_STAGE_LOOP, (unsigned long)engine_stage_us);
    }
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
           "  sim gen <n> 2|3              which blade generation the slot holds, before seating it\n"
           "  sim attach <n> <mV> <mA>     sink plugs in (e.g. 20000 5000 = 100 W laptop)\n"
           "  sim detach <n>\n"
           "  sim load <n> <pct>           measured draw as %% of the contract current\n"
           "  sim fault <n> ocp            over-current trip (gen 2: INA226 alert; gen 3: the VBUS switch)\n"
           "  sim fault <n> general|otw1|otw2|ntc1|ntc2|cc|short|vbatt|clear\n"
           "                               gen 2: MPQ4242 fault bit, sticky until 'clear'\n"
           "  sim fault <n> ovp|vconn|cc-ovp|port-otp|conv-scp|conv-ocp|conv-ovp|conv-hot|plug-hot|bus|vbus|pd|clear\n"
           "                               gen 3: blade fault, held as a condition until 'clear'\n"
           "  sim restart <n>              gen 3: the blade's MCU restarts (configuration gone)\n"
           "  sim temp <n> <conv> <plug>   gen 3: pin the converter and receptacle thermometers (degC)\n"
           "  sim temp <n> auto            ...back to the load-driven model\n"
           "  sim probe <n> ina|mpq|blade|ok  the chip goes silent (a powered port stays powered) or answers\n"
           "  sim mux fail|ok              I2C mux select fails until the engine resets it\n"
           "  sim expander fail|ok         GPIO expander I/O fails until the engine resets it (after 5 s)\n",
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
        else if (!strcmp(what, "restart")) c.arg = sim_inject_pack(SIM_RESTART, 0, 0);
        else if (!strcmp(what, "temp")) {
            const char *conv = strtok_r(NULL, " \t", save);
            const char *plug = strtok_r(NULL, " \t", save);
            if (conv && !strcmp(conv, "auto")) c.arg = sim_inject_pack(SIM_TEMP, 0, 0xFFFF);
            else {
                int tc = conv ? atoi(conv) : -1, tp = plug ? atoi(plug) : -1;
                if (tc < 0 || tc > 150 || tp < 0 || tp > 150) { printf("temp: 0-150 degC each\n"); return; }
                c.arg = sim_inject_pack(SIM_TEMP, (unsigned)tc * 10, (unsigned)tp * 10);
            }
        }
        else if (!strcmp(what, "gen")) {
            const char *g = strtok_r(NULL, " \t", save);
            int gen = g ? atoi(g) : 0;
            if (gen != 2 && gen != 3) { print_sim_help(); return; }
            c.arg = sim_inject_pack(SIM_GEN, 0, (unsigned)gen);
        }
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
            // gen-2 names carry the MPQ4242's bits, gen-3 names the blade's own
            static const struct { const char *name; unsigned bit; } BITS[] = {
                {"general", PORT_FAULT_GENERAL}, {"otw1", PORT_FAULT_OTW1}, {"otw2", PORT_FAULT_OTW2},
                {"ntc1", PORT_FAULT_NTC1}, {"ntc2", PORT_FAULT_NTC2}, {"cc", PORT_FAULT_CC},
                {"short", PORT_FAULT_SHORT_VBATT}, {"vbatt", PORT_FAULT_VBATT_LOW},
            };
            static const struct { const char *name; unsigned bit; } BLADE[] = {
                {"ovp", BLADE_FAULT_OVP}, {"vconn", BLADE_FAULT_OCP_VCONN}, {"cc-ovp", BLADE_FAULT_OVP_CC},
                {"port-otp", BLADE_FAULT_OTP_PORT}, {"conv-scp", BLADE_FAULT_CONV_SCP},
                {"conv-ocp", BLADE_FAULT_CONV_OCP}, {"conv-ovp", BLADE_FAULT_CONV_OVP},
                {"conv-hot", BLADE_FAULT_OT_CONV}, {"plug-hot", BLADE_FAULT_OT_PLUG},
                {"bus", BLADE_FAULT_BUS}, {"vbus", BLADE_FAULT_VBUS}, {"pd", BLADE_FAULT_PD},
            };
            unsigned bit = 0xFFFF, blade_bit = 0xFFFF;
            for (size_t i = 0; f && i < sizeof(BITS) / sizeof(BITS[0]); i++)
                if (!strcmp(f, BITS[i].name)) bit = BITS[i].bit;
            for (size_t i = 0; f && i < sizeof(BLADE) / sizeof(BLADE[0]); i++)
                if (!strcmp(f, BLADE[i].name)) blade_bit = BLADE[i].bit;
            if (f && !strcmp(f, "ocp")) {
                c.arg = sim_inject_pack(SIM_OCP, 0, 0);
            } else if (f && !strcmp(f, "clear")) {
                // both generations' latches, whichever the slot holds
                engine_cmd_t c2 = c;
                c2.arg = sim_inject_pack(SIM_BLADE_FAULT, 0, 0);
                ipc_cmd_push(&c2);
                c.arg = sim_inject_pack(SIM_MPQ_FAULT, 0, 0);
            } else if (bit != 0xFFFF) {
                c.arg = sim_inject_pack(SIM_MPQ_FAULT, 0, bit);
            } else if (blade_bit != 0xFFFF) {
                c.arg = sim_inject_pack(SIM_BLADE_FAULT, 0, blade_bit);
            } else {
                print_sim_help();
                return;
            }
        } else if (!strcmp(what, "probe")) {
            const char *v = strtok_r(NULL, " \t", save);
            unsigned mode = v && !strcmp(v, "ina") ? 1 : v && !strcmp(v, "mpq") ? 2
                          : v && !strcmp(v, "blade") ? 3 : v && !strcmp(v, "ok") ? 0 : 9;
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

static void print_ups(void) {
    const ups_state_t *s = ups_state();
    printf("ups: %s\n", ups_status_str());
    if (!s->present) {
        printf("  no Mean Well LAD answering on the UPS header (probed every 5 s; %lu timeouts)\n",
               (unsigned long)s->timeouts);
        return;
    }
    char fault[96];
    ups_fault_text(fault, sizeof(fault));
    printf("  status %04x/%04x: AC %s, %s, %s%s%s%s%s\n", s->status_h, s->status_l,
           (s->status_l & LAD_ST_AC_OK) ? "ok" : "ABNORMAL",
           (s->status_l & LAD_ST_ON_BATTERY) ? "on battery" : "on AC power",
           (s->status_l & LAD_ST_CHG_FULL) ? "battery full" :
           (s->status_l & LAD_ST_CHARGING) ? "charging" : "not charging",
           (s->status_l & LAD_ST_FORCED) ? ", forced start" : "",
           (s->status_l & LAD_ST_LINK_CTRL) ? ", remote UPS" : "",
           (s->status_h & LAD_STH_BAT_SW_OFF) ? ", battery switch OFF" : "",
           fault[0] ? ", FAULT" : "");
    if (fault[0]) printf("  fault: %s\n", fault);
    printf("  cells:");
    for (int i = 0; i < 4; i++) {
        if (s->cell_cv[i] == 0xFFFF) printf(" -");
        else printf(" %u.%02u V", s->cell_cv[i] / 100, s->cell_cv[i] % 100);
    }
    printf("; undervoltage cutoff %u.%02u V\n", s->uvp_cv / 100, s->uvp_cv % 100);
    printf("  link: %lu replies, %lu timeouts, %lu bad frames\n", (unsigned long)s->replies,
           (unsigned long)s->timeouts, (unsigned long)s->bad_frames);
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

// Bench: raw backplane bus access, run on the engine core (see engine/i2c_diag.h)
static void run_i2c_diag(char **save) {
#ifdef PWRMAN_FAKE_BLADES
    (void)save;
    printf("real builds only\n");
#else
    const char *what = strtok_r(NULL, " \t", save);
    const char *a1 = strtok_r(NULL, " \t", save);
    const char *a2 = strtok_r(NULL, " \t", save);
    const char *a3 = strtok_r(NULL, " \t", save);
    const char *a4 = strtok_r(NULL, " \t", save);
    uint8_t op = 0, ch = 0xF;
    uint32_t arg = 0;
    if (a1 && strcmp(a1, "none")) ch = (uint8_t)strtoul(a1, NULL, 0);
    if (what && !strcmp(what, "scan")) {
        op = I2C_DIAG_SCAN;
    } else if (what && !strcmp(what, "read") && a2 && a3) {
        op = I2C_DIAG_READ;
        arg = (strtoul(a2, NULL, 0) & 0xFF) << 16 | (strtoul(a3, NULL, 0) & 0xFF) << 8 |
              (a4 ? strtoul(a4, NULL, 0) & 0xFF : 1);
    } else if (what && !strcmp(what, "write") && a2 && a3 && a4) {
        op = I2C_DIAG_WRITE;
        arg = (strtoul(a2, NULL, 0) & 0xFF) << 16 | (strtoul(a3, NULL, 0) & 0xFF) << 8 |
              (strtoul(a4, NULL, 0) & 0xFF);
    } else if (what && !strcmp(what, "en") && a1 && a2) {
        op = I2C_DIAG_EN;
        ch = (uint8_t)(strtoul(a1, NULL, 0) - 1); // 1-based port
        arg = !strcmp(a2, "on");
    } else {
        printf("i2c scan <ch|none> | read <ch> <addr> <reg> [n] | write <ch> <addr> <reg> <val> | en <port> on|off\n");
        return;
    }
    if (ch != 0xF && ch > 5) { printf("channel 0-5 or none\n"); return; }
    arg |= (uint32_t)op << 28 | (uint32_t)ch << 24;
    uint32_t s0 = g_i2c_diag_seq;
    engine_cmd_t c = {.op = CMD_I2C_DIAG, .arg = arg};
    if (!ipc_cmd_push(&c)) { printf("queue full\n"); return; }
    for (int i = 0; i < 300 && g_i2c_diag_seq == s0; i++) sleep_ms(5);
    if (g_i2c_diag_seq == s0) { printf("engine did not answer\n"); return; }
    const i2c_diag_result_t *r = &g_i2c_diag;
    if (op == I2C_DIAG_EN) { printf("en %s\n", r->ok ? "ok" : "expander write failed"); return; }
    printf("mux %s: ", ch == 0xF ? "no channel" : "channel selected");
    if (ch != 0xF) printf("%s; ", r->selected ? "ack" : "NO ACK");
    if (op == I2C_DIAG_SCAN) {
        int found = 0;
        for (int a = 0x08; a < 0x78; a++)
            if (r->found[a >> 5] & (1u << (a & 31))) { printf("0x%02x ", a); found++; }
        printf("%s\n", found ? "" : "nothing answers");
    } else if (op == I2C_DIAG_READ) {
        if (!r->ok) { printf("no ack\n"); return; }
        for (int i = 0; i < r->count; i++) printf("%02x ", r->data[i]);
        printf("\n");
    } else {
        printf("%s\n", r->ok ? "written" : "no ack");
    }
#endif
}

// Every command reaches the web console too (run_web_batch), behind only the
// API token. A command that must stay behind the serial console - one that
// sets or clears the token, gets past the update signing or downgrade rules,
// wipes the settings, stops the firmware so only someone at the box can
// bring it back, trims a reading a safety cut-off acts on, or is a bench or
// test tool - goes on the refusal list in cli_line_web_refusal (cli_line.c) and in
// test_web_refusal in the same change. A command with a password or token
// argument gets its masking in cli_line_mask.
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
    } else if (!strcmp(cmd, "ups")) {
        const char *what = strtok_r(NULL, " \t", &save);
        const char *v = strtok_r(NULL, " \t", &save);
        if (!what) {
            print_ups();
        } else if (!strcmp(what, "buzzer") && v && (!strcmp(v, "on") || !strcmp(v, "off"))) {
            if (!ups_present()) printf("no UPS answering\n");
            else printf(ups_set_buzzer(!strcmp(v, "on")) ? "ok\n" : "busy, try again\n");
        } else {
            printf("usage: ups [buzzer on|off]\n");
        }
    } else if (!strcmp(cmd, "vin")) {
        const char *what = strtok_r(NULL, " \t", &save);
        const char *v = strtok_r(NULL, " \t", &save);
        if (!what) {
            printf("bus: %s", vin_status_str());
            if (vin_fitted()) printf(" (raw %lu, cal %u.%03u)", (unsigned long)vin_raw(),
                                     g_settings.vin_cal / 1000, g_settings.vin_cal % 1000);
            printf("\n");
        } else if (!vin_fitted()) {
            printf("no bus-voltage divider on this board\n");
        } else if (!strcmp(what, "cal") && v && !strcmp(v, "reset")) {
            g_settings.vin_cal = VIN_CAL_DEFAULT;
            printf("cal 1.000: %s; 'save' to keep\n", vin_status_str());
        } else if (!strcmp(what, "cal") && v && atof(v) > 0) {
            uint16_t c = vin_cal_for((uint32_t)(atof(v) * 1000.0 + 0.5));
            if (!c) { printf("no reading yet\n"); return; }
            g_settings.vin_cal = c;
            printf("cal %u.%03u: %s; 'save' to keep\n", c / 1000, c % 1000, vin_status_str());
        } else {
            printf("usage: vin [cal <volts>|cal reset]\n");
        }
    } else if (!strcmp(cmd, "button")) {
        const char *what = strtok_r(NULL, " \t", &save);
        if (!what) {
            printf("button: %s%s\n", button_fitted() ? "input on GP22, " : "no input on this board, ",
                   button_held() ? "held" : "released");
        } else if (!strcmp(what, "short")) {
            button_inject(BUTTON_SHORT); // wake the chain, from the main loop
        } else if (!strcmp(what, "long")) {
            button_inject(BUTTON_LONG); // open the BLE window
        } else {
            printf("usage: button [short|long]\n");
        }
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
            if (!improv_open(IMPROV_WINDOW_MS, "console")) printf("Wi-Fi setup unavailable\n");
        } else if (!strcmp(op, "off")) {
            improv_close();
            printf("improv: off (automatic windows disabled until 'improv on' or reboot)\n");
        } else {
            printf("usage: improv [on|off]\n");
        }
    } else if (!strcmp(cmd, "mqtt")) {
        const char *host = strtok_r(NULL, " \t", &save);
        if (host && !strcmp(host, "tls")) {
            const char *v = strtok_r(NULL, " \t", &save);
            if (v && !strcmp(v, "on")) g_settings.mqtt_tls = MQTT_TLS_VERIFIED;
            else if (v && !strcmp(v, "off")) g_settings.mqtt_tls = MQTT_TLS_OFF;
            else if (v && !strcmp(v, "unverified")) g_settings.mqtt_tls = MQTT_TLS_UNVERIFIED;
            else { printf("usage: mqtt tls on|off|unverified\n"); return; }
            mqtt_reconnect();
            printf("mqtt: link %s ('save' to persist; reconnects now)\n", mqtt_tls_mode_str());
            if (mqtt_tls_blocker()) printf("mqtt: %s\n", mqtt_tls_blocker());
            return;
        }
        if (host && !strcmp(host, "ca")) {
            const char *v = strtok_r(NULL, " \t", &save);
            if (v && !strcmp(v, "clear")) {
                g_settings.mqtt_ca_len = 0;
                memset(g_settings.mqtt_ca, 0, sizeof(g_settings.mqtt_ca));
                mqtt_reconnect();
                printf("mqtt: certificate removed, link %s ('save' to persist)\n", mqtt_tls_mode_str());
            } else if (v) {
                printf("usage: mqtt ca [clear]  (a certificate is installed from the web UI or POST /api/v1/settings)\n");
            } else {
                char d[224];
                if (mqtt_ca_describe(d, sizeof(d))) printf("mqtt ca: %s\n", d);
                else printf("mqtt ca: none installed (a verified link trusts the built-in Let's Encrypt roots; a broker by address needs one)\n");
            }
            return;
        }
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
    } else if (!strcmp(cmd, "ntp")) {
        const char *h = strtok_r(NULL, " \t", &save);
        if (!h || strlen(h) >= sizeof(g_settings.ntp_server)) {
            printf("usage: ntp <host>|auto\n");
            return;
        }
        if (!strcmp(h, "auto")) h = "";
        snprintf(g_settings.ntp_server, sizeof(g_settings.ntp_server), "%s", h);
        printf("ntp: %s ('save' to persist; applies at once)\n", h[0] ? h : "auto");
    } else if (!strcmp(cmd, "hostnames")) {
        const char *rest = save ? save : "";
        while (*rest == ' ' || *rest == '\t') rest++;
        if (!*rest) {
            printf("usage: hostnames <name> [<name>...] | hostnames clear\n");
            return;
        }
        if (!strcmp(rest, "clear")) rest = "";
        char names[HOSTNAMES_MAX];
        const char *err = http_req_hostnames_parse(rest, names, sizeof(names));
        if (err) {
            printf("%s\n", err);
            return;
        }
        strcpy(g_settings.hostnames, names);
        printf("hostnames: %s ('save' to persist; applies at once)\n", names[0] ? names : "none");
    } else if (!strcmp(cmd, "https")) {
        const char *op = strtok_r(NULL, " \t", &save);
        char d[224];
        if (!op) {
            printf("https: %s\n", g_settings.https ? "on" : "off");
            if (https_describe(d, sizeof(d))) printf("certificate: %s\n", d);
            else printf("certificate: none (install one with POST /api/v1/tls, see the docs)\n");
        } else if (!strcmp(op, "on")) {
            if (!https_config()) {
                printf("https: install a certificate first\n");
                return;
            }
            g_settings.https = 1;
            http_tls_sync();
            printf("https: on, port 80 redirects ('save' to persist)\n");
        } else if (!strcmp(op, "off")) {
            g_settings.https = 0;
            http_tls_sync();
            printf("https: off, plain HTTP on port 80 ('save' to persist)\n");
        } else if (!strcmp(op, "remove")) {
            const char *err = https_remove();
            printf("https: %s\n", err ? err : "certificate removed");
        } else {
            printf("usage: https [on|off|remove]\n");
        }
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
        printf("token %s\n", g_settings.api_token[0]
                                  ? "set"
                                  : "cleared: the network API refuses changes until a token is "
                                    "set again (console, Improv, or the first hour on Ethernet)");
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
        if (!n || !op || !port_arg(n, &port)) { printf("usage: port <1-%d> on|off|reset|srccap|update|priority|name|limit|volt|boot|autooff|sleep|protect\n", NUM_PORTS); return; }
        if (!strcmp(op, "protect")) {
            const char *v = strtok_r(NULL, " \t", &save);
            if (!v || (strcmp(v, "on") && strcmp(v, "off"))) { printf("usage: port <1-%d> protect on|off\n", NUM_PORTS); return; }
            uint8_t bit = (uint8_t)(1u << port);
            if (!strcmp(v, "on")) g_settings.port_protect |= bit;
            else g_settings.port_protect &= (uint8_t)~bit;
            printf("port %u %s from the next device plugged in ('save' to persist)\n", port + 1,
                   !strcmp(v, "on") ? "protected" : "not protected");
            return;
        }
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
        if (!strcmp(op, "volt")) {
            const char *v = strtok_r(NULL, " \t", &save);
            uint16_t mv;
            if (!v || !settings_port_volt_parse(v, &mv)) {
                printf("usage: port <1-%d> volt 5|9|12|15|20 (V; 20 = the whole PDO table)\n", NUM_PORTS);
                return;
            }
            g_settings.port_max_mv[port] = mv;
            engine_cmd_t c = {.op = CMD_PORT_VOLT, .port = port, .arg = mv};
            printf(ipc_cmd_push(&c) ? "port %u voltage cap %u V ('save' to persist)\n" : "queue full\n", port + 1, mv / 1000);
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
        else if (!strcmp(op, "update")) {
            if (!blade_bundle_header()) { printf("no blade firmware bundled in this build\n"); return; }
            c.op = CMD_PORT_UPDATE;
        }
        else { printf("unknown op '%s'\n", op); return; }
        if (!ipc_cmd_push(&c)) { printf("queue full\n"); return; }
        bool remembered = (c.op == CMD_PORT_ENABLE || c.op == CMD_PORT_DISABLE) &&
                          settings_port_admin_note(port, c.op == CMD_PORT_ENABLE);
        if (remembered) settings_save_later(); // the "last" policy keeps it across reboots
        printf(remembered ? "ok (kept for the next boot)\n" : "ok\n");
    } else if (!strcmp(cmd, "blades")) {
        const char *what = strtok_r(NULL, " \t", &save);
        const char *v = strtok_r(NULL, " \t", &save);
        if (!what) {
            const blade_image_header_t *h = blade_bundle_header();
            if (h) printf("bundled blade firmware %u.%u.%u (protocol %u, %lu bytes)\n", h->major, h->minor,
                          h->patch, h->proto, (unsigned long)h->length);
            else printf("no blade firmware bundled in this build\n");
            printf("auto update %s, boot through the bootloader %s, watch ",
                   g_settings.blade_auto_update ? "on" : "off", g_settings.blade_boot_via_loader ? "on" : "off");
            if (g_settings.blade_watch_s) printf("%u s\n", g_settings.blade_watch_s);
            else printf("off\n");
            printf("a port with something plugged in is left alone: its blade is updated once the port has been empty for a while,\n"
                   "or now with 'port <n> update'%s\n",
                   flash_map_update_pending() ? "; nothing is changed while this controller image is on trial" : "");
            return;
        }
        bool on = v && !strcmp(v, "on");
        if (!strcmp(what, "auto") && v && (on || !strcmp(v, "off"))) {
            g_settings.blade_auto_update = on;
        } else if (!strcmp(what, "bootopt") && v && (on || !strcmp(v, "off"))) {
            g_settings.blade_boot_via_loader = on;
        } else if (!strcmp(what, "watch") && v) {
            int sec = !strcmp(v, "off") ? 0 : atoi(v);
            if (sec < 0 || sec > 255) { printf("watch: 1-255 s, or off\n"); return; }
            g_settings.blade_watch_s = (uint8_t)sec; // blades pick it up at their next probe
        } else {
            printf("usage: blades [auto on|off | bootopt on|off | watch <1-255>|off]\n");
            return;
        }
        printf("ok ('save' to persist)\n");
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
        // This console is the one place the signing and no-downgrade rules
        // can be waived: getting here takes the USB header or a debug probe.
        unsigned allow = 0;
        const char *url = NULL;
        bool bad = false;
        char known[UPDATE_LATEST_URL_MAX];
        const char *first = strtok_r(NULL, " \t", &save);
        if (first && !strcmp(first, "check")) {
            char e[96];
            if (update_check_now(e, sizeof(e))) printf("asking %s; the answer lands on this console and in 'info'\n", g_settings.update_url);
            else printf("update: %s\n", e);
            return;
        }
        if (first && !strcmp(first, "auto")) {
            const char *a = strtok_r(NULL, " \t", &save);
            char e[96];
            if (a && (!strcmp(a, "on") || !strcmp(a, "off"))) {
                g_settings.update_auto = !strcmp(a, "on");
                printf("automatic installs %s ('save' to persist)\n", a);
            } else if (a && !strcmp(a, "postpone")) {
                if (!update_auto_postpone(e, sizeof(e))) printf("update: %s\n", e);
            } else if (a && !strcmp(a, "skip")) {
                if (!update_auto_skip(e, sizeof(e))) printf("update: %s\n", e);
            } else {
                printf("usage: update auto on|off|postpone|skip\n");
            }
            return;
        }
        if (first && !strcmp(first, "source")) {
            const char *base = strtok_r(NULL, " \t", &save);
            if (base && !strcmp(base, "off")) base = "";
            else if (base && !strcmp(base, "default")) base = UPDATE_SOURCE_DEFAULT;
            if (!base || !update_source_valid(base, sizeof(g_settings.update_url))) {
                printf("usage: update source <http://host[:port][/path]>|default|off\n");
                return;
            }
            strcpy(g_settings.update_url, base);
            printf(base[0] ? "update source %s ('save' to persist; 'update check' asks now)\n"
                           : "update check off%s ('save' to persist)\n", base);
            return;
        }
        for (const char *a = first; a; a = strtok_r(NULL, " \t", &save)) {
            if (!strcmp(a, "--unsigned")) allow |= UPDATE_ALLOW_UNSIGNED;
            else if (!strcmp(a, "--downgrade")) allow |= UPDATE_ALLOW_DOWNGRADE;
            else if (a[0] == '-' || url) bad = true;
            else url = a;
        }
        if (url && !strcmp(url, "latest")) { // the release the check or the MQTT pointer named
            net_lock();
            snprintf(known, sizeof(known), "%s", update_latest_url());
            net_unlock();
            if (!known[0]) { printf("no release is known yet ('update check' asks the update source)\n"); return; }
            url = known;
            printf("pulling %s\n", url);
        }
        if (bad || !url) {
            printf("usage: update [--unsigned] [--downgrade] <http://host[:port]/controller.signed.bin>|latest\n"
                   "       update check | update source <http://host[:port][/path]>|default|off\n"
                   "       update auto on|off|postpone|skip\n");
            return;
        }
        char e[96];
        if (ota_pull_start(url, allow, e, sizeof(e)))
            printf("pulling; progress lands on this console\n");
        else
            printf("update: %s\n", e);
    } else if (!strcmp(cmd, "i2c")) {
        run_i2c_diag(&save);
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
        static char json[SETTINGS_JSON_MAX]; // static: the console runs on core 0's small stack
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

bool cli_web_submit(const char *text, size_t len) {
    if (web_state == CLI_WEB_QUEUED || web_state == CLI_WEB_RUNNING || len >= sizeof(web_in))
        return false;
    memcpy(web_in, text, len);
    web_in[len] = '\0';
    __compiler_memory_barrier();
    web_state = CLI_WEB_QUEUED;
    return true;
}

cli_web_state_t cli_web_state(void) {
    return web_state;
}

const char *cli_web_output(size_t *len) {
    *len = web_out_len;
    return web_out;
}

// The queued batch, line by line. Each line is printed first, masked, so the
// transcript and the log show what ran; blank lines and lines starting with
// '#' are skipped, which lets a pasted script carry comments.
static void run_web_batch(void) {
    static const char cut_note[] = "[output cut: it did not fit in 8 KB]\n";
    static char cmd[CLI_LINE_MAX]; // static: the console runs on core 0's small stack
    static char masked[CLI_LINE_MAX + sizeof(CLI_LINE_MASK)];
    web_state = CLI_WEB_RUNNING;
    log_sink_capture(web_out, sizeof(web_out) - sizeof(cut_note), &web_out_len, &web_out_cut);
    for (char *p = web_in, *end; p; p = end ? end + 1 : NULL) {
        end = strchr(p, '\n');
        if (end) *end = '\0';
        size_t n = strlen(p);
        if (n && p[n - 1] == '\r') p[--n] = '\0';
        while (*p == ' ' || *p == '\t') p++, n--;
        if (!n || *p == '#') continue;
        if (n >= sizeof(cmd)) {
            printf("web> (a line of %u characters: %u at most)\n", (unsigned)n,
                   (unsigned)sizeof(cmd) - 1);
            continue;
        }
        cli_line_mask(p, masked, sizeof(masked));
        printf("web> %s\n", masked);
        const char *refused = cli_line_web_refusal(p);
        if (refused) {
            printf("'%s' works only on the serial console\n", refused);
            continue;
        }
        memcpy(cmd, p, n + 1);
        run_line(cmd);
    }
    log_sink_capture(NULL, 0, NULL, NULL);
    if (web_out_cut) {
        memcpy(web_out + web_out_len, cut_note, sizeof(cut_note));
        web_out_len += sizeof(cut_note) - 1;
    }
    __compiler_memory_barrier();
    web_state = CLI_WEB_DONE;
}

void cli_poll(void) {
    if (web_state == CLI_WEB_QUEUED) run_web_batch();
    for (;;) {
        int c = getchar_timeout_us(0);
        if (c == PICO_ERROR_TIMEOUT) return;
        // the echo of what is typed (and the prompt) stays out of the log
        // sink; the finished line goes in once, its secrets masked
        if (c == '\r' || c == '\n') {
            log_sink_pause(true);
            printf("\n");
            log_sink_pause(false);
            line[line_len] = '\0';
            if (line_len) {
                static char note[sizeof("console> ") + CLI_LINE_MAX + sizeof(CLI_LINE_MASK)];
                memcpy(note, "console> ", 9);
                cli_line_mask(line, note + 9, sizeof(note) - 9);
                if (note[9]) log_sink_note(note);
                run_line(line);
            }
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
