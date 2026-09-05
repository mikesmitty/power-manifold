#include "settings_json.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "ip4_text.h"
#include "net/jsonlite.h"
#include "manifold.h"

#define STR_(x) #x
#define STR(x) STR_(x)

// ---- building ---------------------------------------------------------------

static size_t putf(char *out, size_t cap, size_t off, const char *fmt, ...) {
    if (off >= cap) return cap;
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(out + off, cap - off, fmt, ap);
    va_end(ap);
    if (n < 0) return cap;
    return off + (size_t)n >= cap ? cap : off + (size_t)n;
}

// "key":"escaped value",
static size_t put_str(char *out, size_t cap, size_t off, const char *key, const char *val) {
    off = putf(out, cap, off, "\"%s\":\"", key);
    if (off < cap) off += json_escape(out + off, cap - off, val);
    return putf(out, cap, off, "\",");
}

static size_t put_ip(char *out, size_t cap, size_t off, const char *key, uint32_t addr) {
    char t[16];
    ip4_format(t, sizeof(t), addr);
    return put_str(out, cap, off, key, t);
}

size_t settings_json_build(char *out, size_t cap, const settings_t *s,
                           const settings_json_opts_t *o) {
    if (!cap) return 0;
    size_t off = putf(out, cap, 0, "{");
    off = put_str(out, cap, off, "name", s->device_name);
    off = put_str(out, cap, off, "wifi_ssid", s->wifi_ssid);
    off = put_str(out, cap, off, "mqtt_host", s->mqtt_host);
    off = putf(out, cap, off, "\"mqtt_port\":%u,", s->mqtt_port);
    off = put_str(out, cap, off, "mqtt_user", s->mqtt_user);
    off = putf(out, cap, off, "\"mqtt_pass_set\":%s,\"token_set\":%s,\"setup\":%s,",
               s->mqtt_pass[0] ? "true" : "false", s->api_token[0] ? "true" : "false",
               o->setup ? "true" : "false");
    off = putf(out, cap, off, "\"budget_w\":%lu,\"fan_mode\":\"%s\",\"fan_on_w\":%u,"
               "\"fan_off_w\":%u,\"fan_on_ma\":%u,\"led_brightness\":%u,\"led_boot\":\"%s\",",
               (unsigned long)(s->budget_mw / 1000u),
               s->fan_auto ? "auto" : (o->fan_on ? "on" : "off"),
               s->fan_on_w, s->fan_off_w, s->fan_on_ma, s->led_brightness,
               s->led_boot == LED_BOOT_RAINBOW ? "rainbow" : "white");
    off = putf(out, cap, off, "\"ip_mode\":\"%s\",", s->ip_static ? "static" : "dhcp");
    off = put_ip(out, cap, off, "ip", s->ip_addr);
    off = put_ip(out, cap, off, "netmask", s->ip_mask);
    off = put_ip(out, cap, off, "gateway", s->ip_gw);
    off = put_ip(out, cap, off, "dns", s->ip_dns);
    off = put_str(out, cap, off, "syslog_host", s->syslog_host);
    off = putf(out, cap, off, "\"syslog_port\":%u,\"port_names\":[", s->syslog_port);
    for (int i = 0; i < NUM_PORTS; i++) {
        off = putf(out, cap, off, "%s\"", i ? "," : "");
        if (off < cap) off += json_escape(out + off, cap - off, s->port_name[i]);
        off = putf(out, cap, off, "\"");
    }
    off = putf(out, cap, off, "],\"port_limits_ma\":[");
    for (int i = 0; i < NUM_PORTS; i++)
        off = putf(out, cap, off, "%s%lu", i ? "," : "", (unsigned long)s->port_limit_ma[i]);
    off = putf(out, cap, off, "],\"port_boot\":[");
    for (int i = 0; i < NUM_PORTS; i++)
        off = putf(out, cap, off, "%s\"%s\"", i ? "," : "", settings_port_boot_name(s->port_boot[i]));
    off = putf(out, cap, off, "],\"port_priorities\":[");
    for (int i = 0; i < NUM_PORTS; i++)
        off = putf(out, cap, off, "%s%u", i ? "," : "", s->port_priority[i]);
    off = putf(out, cap, off, "]");
    if (o->secrets) {
        off = putf(out, cap, off, ",");
        off = put_str(out, cap, off, "wifi_pass", s->wifi_pass);
        off = put_str(out, cap, off, "mqtt_pass", s->mqtt_pass);
        off = put_str(out, cap, off, "token", s->api_token);
        off--; // the trailing comma
    }
    if (o->export) off = putf(out, cap, off, ",\"format\":1,\"fw\":\"%s\"", FW_VERSION);
    off = putf(out, cap, off, "}");
    if (off >= cap) { // truncated: never hand out a torn object
        out[0] = '\0';
        return 0;
    }
    return off;
}

// ---- applying ---------------------------------------------------------------

static bool header_safe(const char *s) { // printable ASCII, no spaces
    for (; *s; s++) {
        if ((unsigned char)*s < 0x21 || (unsigned char)*s > 0x7E) return false;
    }
    return true;
}

static bool no_controls(const char *s) {
    for (; *s; s++) {
        if ((unsigned char)*s < 0x20 || (unsigned char)*s == 0x7F) return false;
    }
    return true;
}

static bool valid_name(const char *s) { // one hostname label
    size_t n = strlen(s);
    if (!n || s[0] == '-' || s[n - 1] == '-') return false;
    for (; *s; s++) {
        if (!((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') || (*s >= '0' && *s <= '9') ||
              *s == '-'))
            return false;
    }
    return true;
}

// string field with a length check and a validator; NULL when fine
static const char *take_str(const char *body, const char *key, char *dst, size_t cap,
                            bool (*ok)(const char *), const char *too_long, const char *bad) {
    int r = json_get_str(body, key, dst, cap);
    if (r < 0) return too_long;
    if (r > 0 && ok && !ok(dst)) return bad;
    return NULL;
}

const char *settings_json_apply(const char *body, settings_t *s, bool via_setup,
                                settings_apply_t *out) {
    memset(out, 0, sizeof(*out));
    const char *err;
    long v;

    if ((err = take_str(body, "name", s->device_name, sizeof(s->device_name), valid_name,
                        "name too long", "name: letters, digits and hyphens only")))
        return err;
    if ((err = take_str(body, "wifi_ssid", s->wifi_ssid, sizeof(s->wifi_ssid), no_controls,
                        "wifi_ssid too long", "wifi_ssid: no control characters")))
        return err;
    if ((err = take_str(body, "wifi_pass", s->wifi_pass, sizeof(s->wifi_pass), no_controls,
                        "wifi_pass too long", "wifi_pass: no control characters")))
        return err;
    if ((err = take_str(body, "mqtt_host", s->mqtt_host, sizeof(s->mqtt_host), header_safe,
                        "mqtt_host too long", "mqtt_host: no spaces or control characters")))
        return err;
    if ((err = take_str(body, "mqtt_user", s->mqtt_user, sizeof(s->mqtt_user), no_controls,
                        "mqtt_user too long", "mqtt_user: no control characters")))
        return err;
    if ((err = take_str(body, "mqtt_pass", s->mqtt_pass, sizeof(s->mqtt_pass), no_controls,
                        "mqtt_pass too long", "mqtt_pass: no control characters")))
        return err;
    if ((err = take_str(body, "token", s->api_token, sizeof(s->api_token), header_safe,
                        "token too long", "token: no spaces or control characters")))
        return err;
    if ((err = take_str(body, "syslog_host", s->syslog_host, sizeof(s->syslog_host), header_safe,
                        "syslog_host too long", "syslog_host: no spaces or control characters")))
        return err;
    if (json_get_int(body, "mqtt_port", &v)) {
        if (v < 1 || v > 65535) return "mqtt_port out of range";
        s->mqtt_port = (uint16_t)v;
    }
    if (json_get_int(body, "syslog_port", &v)) {
        if (v < 1 || v > 65535) return "syslog_port out of range";
        s->syslog_port = (uint16_t)v;
    }
    if (via_setup && !s->api_token[0]) return "set an API token to finish setup";

    // operational fields
    if (json_get_int(body, "budget_w", &v)) {
        if (v < BUDGET_MIN_W || v > BUDGET_MAX_W) return "budget_w out of range";
        s->budget_mw = (uint32_t)v * 1000u;
    }
    if (json_get_int(body, "fan_on_w", &v)) {
        if (v < 1 || v > 1000) return "fan_on_w: 1-1000";
        s->fan_on_w = (uint16_t)v;
        out->fan_thresholds = true;
    }
    if (json_get_int(body, "fan_off_w", &v)) {
        if (v < 0 || v > 1000) return "fan_off_w: 0-1000";
        s->fan_off_w = (uint16_t)v;
        out->fan_thresholds = true;
    }
    if (json_get_int(body, "fan_on_ma", &v)) {
        if (v < 0 || v > 10000) return "fan_on_ma: 0-10000 (0 disables)";
        s->fan_on_ma = (uint16_t)v;
        out->fan_thresholds = true;
    }
    char mode[8];
    int m = json_get_str(body, "fan_mode", mode, sizeof(mode));
    if (m != 0) {
        if (m > 0 && !strcmp(mode, "auto")) {
            s->fan_auto = 1;
        } else if (m > 0 && (!strcmp(mode, "on") || !strcmp(mode, "off"))) {
            s->fan_auto = 0;
            out->fan_manual_on = mode[1] == 'n';
        } else {
            return "fan_mode: auto, on or off";
        }
        out->fan_mode_given = true;
    }
    if ((out->fan_thresholds || m != 0) && s->fan_off_w >= s->fan_on_w)
        return "fan_off_w must be below fan_on_w";
    if (json_get_int(body, "led_brightness", &v)) {
        if (v < 0 || v > 255) return "led_brightness: 0-255";
        s->led_brightness = (uint8_t)v;
    }
    char t[20];
    int r = json_get_str(body, "led_boot", t, sizeof(t));
    if (r != 0) {
        if (r > 0 && !strcmp(t, "white")) s->led_boot = LED_BOOT_WHITE;
        else if (r > 0 && !strcmp(t, "rainbow")) s->led_boot = LED_BOOT_RAINBOW;
        else return "led_boot: white or rainbow";
    }

    // addressing: any subset, validated as a whole
    r = json_get_str(body, "ip_mode", t, sizeof(t));
    if (r != 0) {
        if (r > 0 && !strcmp(t, "dhcp")) s->ip_static = 0;
        else if (r > 0 && !strcmp(t, "static")) s->ip_static = 1;
        else return "ip_mode: dhcp or static";
    }
    static const struct { const char *key; size_t off; } IPF[] = {
        {"ip", offsetof(settings_t, ip_addr)}, {"netmask", offsetof(settings_t, ip_mask)},
        {"gateway", offsetof(settings_t, ip_gw)}, {"dns", offsetof(settings_t, ip_dns)},
    };
    for (size_t i = 0; i < sizeof(IPF) / sizeof(IPF[0]); i++) {
        r = json_get_str(body, IPF[i].key, t, sizeof(t));
        if (r == 0) continue;
        uint32_t a = 0;
        if (r < 0 || (t[0] && !ip4_parse(t, &a))) return "ip/netmask/gateway/dns: dotted quad or empty";
        memcpy((uint8_t *)s + IPF[i].off, &a, sizeof(a));
    }
    if (s->ip_static && (!s->ip_addr || !s->ip_gw || !ip4_mask_valid(s->ip_mask)))
        return "static addressing needs ip, a valid netmask and gateway";

    // per-port arrays
    for (int i = 0; i < NUM_PORTS; i++) {
        if (json_get_int_at(body, "port_limits_ma", (unsigned)i, &v)) {
            if (v < PORT_LIMIT_MIN_MA || v > PORT_LIMIT_MAX_MA)
                return "port_limits_ma: " STR(PORT_LIMIT_MIN_MA) "-" STR(PORT_LIMIT_MAX_MA) " mA each";
            s->port_limit_ma[i] = (uint32_t)v;
        }
        if (json_get_int_at(body, "port_priorities", (unsigned)i, &v)) {
            if (v < 0 || v > 255) return "port_priorities: 0-255 each";
            s->port_priority[i] = (uint8_t)v;
        }
        r = json_get_str_at(body, "port_names", (unsigned)i, s->port_name[i], sizeof(s->port_name[i]));
        if (r < 0) return "port_names: at most " STR(PORT_NAME_MAX) " characters each";
        if (r > 0 && !settings_port_name_valid(s->port_name[i]))
            return "port_names: printable text, no leading or trailing spaces";
        r = json_get_str_at(body, "port_boot", (unsigned)i, t, sizeof(t));
        if (r < 0 || (r > 0 && !settings_port_boot_parse(t, &s->port_boot[i])))
            return "port_boot: on, off or last each";
    }
    return NULL;
}
