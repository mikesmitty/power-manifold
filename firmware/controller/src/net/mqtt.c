#include "mqtt.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "pico/unique_id.h"

#include "lwip/apps/mqtt.h"
#include "lwip/dns.h"

#include "boot_reason_hw.h"
#include "event_kind.h"
#include "fault_log.h"
#include "fault_text.h"
#include "health.h"
#include "improv.h"
#include "jsonlite.h"
#include "ipc.h"
#include "led_sched.h"
#include "manifold.h"
#include "net.h"
#include "ota_pull.h"
#include "settings.h"
#include "ups/lad_proto.h"
#include "ups/ups.h"

#define KEEP_ALIVE_S     30
#define BACKOFF_MS       (5 * 1000)
#define TELEMETRY_MS     1000
#define STALL_CYCLES     10 // telemetry cycles with every publish refused before reconnecting

// discovery entity table: per-port sensors + switch + buttons + priority,
// current-limit, voltage-cap and boot-policy controls + event entity +
// charge controls; chassis sensors + fan + BLE provisioning button + the
// UPS entities (published while a supply answers, retracted otherwise; the
// last chassis step retracts the pre-select fan switch config)
#define PORT_SENSOR_N    5
#define PORT_ENTITIES    (PORT_SENSOR_N + 11)
#define CHASSIS_ENTITIES 20
#define N_DISCOVERY      (NUM_PORTS * PORT_ENTITIES + CHASSIS_ENTITIES)

typedef enum {
    ST_IDLE,
    ST_RESOLVING,
    ST_CONNECTING,
    ST_UP,
    ST_BACKOFF,
} mqtt_state_t;

static mqtt_client_t *client;
static mqtt_state_t state = ST_IDLE;
static ip_addr_t broker_ip;
static uint32_t backoff_until_ms;
static uint32_t last_telemetry_ms;
static int discovery_idx;
static bool disc_publishing; // inside discovery_step's publish call
static bool disc_inflight;   // a QoS 1 config awaiting its PUBACK
static uint32_t pub_dropped; // publishes lwIP refused (output buffer / request slots)
static char     pub_dropped_topic[64];
static err_t    pub_dropped_err;
static uint8_t  stalled_cycles; // telemetry cycles in a row with every publish refused

static char uid[9];         // short unique board id
static char base[48];       // pwrman/<device_name>
static char client_id[32];
static char will_topic[64];
static char in_topic[96];   // topic of the in-flight incoming publish
static char in_data[192];   // sized for the update/latest JSON pointer
static uint16_t in_len;

static char latest_version[16]; // from the retained update/latest pointer
static char latest_url[160];

static char topic_buf[160];
static char payload_buf[1024]; // largest: a port's event entity config
static char device_json[192];

static void ensure_ids(void) {
    if (uid[0]) return;
    char full[2 * PICO_UNIQUE_BOARD_ID_SIZE_BYTES + 1];
    pico_get_unique_board_id_string(full, sizeof(full));
    snprintf(uid, sizeof(uid), "%s", full + strlen(full) - 8);
    snprintf(base, sizeof(base), "pwrman/%s", g_settings.device_name);
    snprintf(client_id, sizeof(client_id), "pwrman-%s", uid);
    snprintf(will_topic, sizeof(will_topic), "%s/availability", base);
    snprintf(device_json, sizeof(device_json),
             "{\"ids\":[\"pwrman_%s\"],\"name\":\"%s\",\"mf\":\"Power Manifold\","
             "\"mdl\":\"USB-PD chassis\",\"sw\":\"%s\"}",
             uid, g_settings.device_name, FW_VERSION);
}

bool mqtt_is_connected(void) {
    return state == ST_UP && client && mqtt_client_is_connected(client);
}

// PUBACK (or failure) of the discovery config in flight: release the next one.
// Discovery is 60+ QoS 1 configs of ~450 bytes; fired one per poll tick they
// would overrun both the output ring and the in-flight request slots, and
// lwIP refuses the excess, which used to drop entities silently.
static void discovery_cb(void *arg, err_t err) {
    (void)arg;
    disc_inflight = false;
    if (err != ERR_OK && discovery_idx > 0) discovery_idx--; // send it again
}

static void publish(const char *topic, const char *payload, uint8_t qos,
                    uint8_t retain) {
    if (!client) return;
    err_t err = mqtt_publish(client, topic, payload, (uint16_t)strlen(payload), qos, retain,
                             disc_publishing ? discovery_cb : NULL, NULL);
    if (disc_publishing) {
        disc_inflight = err == ERR_OK;
        if (err != ERR_OK && discovery_idx > 0) discovery_idx--; // retry next tick
    }
    if (err != ERR_OK) {
        pub_dropped++;
        pub_dropped_err = err;
        snprintf(pub_dropped_topic, sizeof(pub_dropped_topic), "%s", topic);
    }
}

// ---- incoming commands -----------------------------------------------------

// minimal {"key":"value"} extraction; enough for the update/latest pointer
static void json_str(const char *json, const char *key, char *out, size_t cap) {
    char pat[24];
    snprintf(pat, sizeof(pat), "\"%s\":\"", key);
    out[0] = '\0';
    const char *s = strstr(json, pat);
    if (!s) return;
    s += strlen(pat);
    const char *e = strchr(s, '"');
    if (!e || (size_t)(e - s) >= cap) return;
    memcpy(out, s, (size_t)(e - s));
    out[e - s] = '\0';
}

static void publish_update_state(void) {
    snprintf(topic_buf, sizeof(topic_buf), "%s/update/state", base);
    snprintf(payload_buf, sizeof(payload_buf),
             "{\"installed_version\":\"%s\",\"latest_version\":\"%s\"}",
             FW_VERSION, latest_version[0] ? latest_version : FW_VERSION);
    publish(topic_buf, payload_buf, 1, 1);
}

static void handle_command(const char *topic, const char *data) {
    size_t blen = strlen(base);
    if (strncmp(topic, base, blen) != 0) return;
    const char *sub = topic + blen;
    bool on = strncmp(data, "ON", 2) == 0;

    unsigned port;
    if (sscanf(sub, "/port/%u/limit/set", &port) == 1 &&
        strstr(sub, "/limit/set") != NULL && port >= 1 && port <= NUM_PORTS) {
        int ma = atoi(data); // HA sends the box value, possibly as "3000.0"
        if (data[0] >= '0' && data[0] <= '9' && ma >= PORT_LIMIT_MIN_MA && ma <= PORT_LIMIT_MAX_MA) {
            g_settings.port_limit_ma[port - 1] = (uint32_t)ma;
            engine_cmd_t c = {.op = CMD_PORT_LIMIT, .port = (uint8_t)(port - 1), .arg = (uint32_t)ma};
            ipc_cmd_push(&c);
            settings_save_later();
        }
    } else if (sscanf(sub, "/port/%u/volt/set", &port) == 1 &&
        strstr(sub, "/volt/set") != NULL && port >= 1 && port <= NUM_PORTS) {
        uint16_t mv;
        if (settings_port_volt_parse(data, &mv)) { // "9" or the select's "9 V"
            g_settings.port_max_mv[port - 1] = mv;
            engine_cmd_t c = {.op = CMD_PORT_VOLT, .port = (uint8_t)(port - 1), .arg = mv};
            ipc_cmd_push(&c);
            settings_save_later();
        }
    } else if (sscanf(sub, "/port/%u/boot/set", &port) == 1 &&
        strstr(sub, "/boot/set") != NULL && port >= 1 && port <= NUM_PORTS) {
        if (settings_port_boot_parse(data, &g_settings.port_boot[port - 1]))
            settings_save_later();
    } else if (sscanf(sub, "/port/%u/autooff/set", &port) == 1 &&
        strstr(sub, "/autooff/set") != NULL && port >= 1 && port <= NUM_PORTS) {
        uint8_t bit = (uint8_t)(1u << (port - 1));
        if (on) g_settings.port_auto_off |= bit;
        else g_settings.port_auto_off &= (uint8_t)~bit;
        settings_save_later();
    } else if (sscanf(sub, "/port/%u/sleep/set", &port) == 1 &&
        strstr(sub, "/sleep/set") != NULL && port >= 1 && port <= NUM_PORTS) {
        int min = atoi(data); // HA sends the box value, possibly as "30.0"
        if (data[0] >= '0' && data[0] <= '9' && min >= 0 && min <= PORT_SLEEP_MAX_MIN) {
            g_settings.port_sleep_min[port - 1] = (uint16_t)min;
            settings_save_later();
        }
    } else if (strcmp(sub, "/charged_mw/set") == 0) {
        int mw = atoi(data);
        if (data[0] >= '0' && data[0] <= '9' && mw >= 0 && mw <= 20000) {
            g_settings.charged_mw = (uint16_t)mw;
            settings_save_later();
        }
    } else if (strcmp(sub, "/charged_min/set") == 0) {
        int min = atoi(data);
        if (data[0] >= '0' && data[0] <= '9' && min >= 1 && min <= 255) {
            g_settings.charged_min = (uint8_t)min;
            settings_save_later();
        }
    } else if (sscanf(sub, "/port/%u/priority/set", &port) == 1 &&
        strstr(sub, "/priority/set") != NULL && port >= 1 && port <= NUM_PORTS) {
        int prio = atoi(data);
        if (prio >= 0 && prio <= 255) {
            g_settings.port_priority[port - 1] = (uint8_t)prio;
            settings_save_later();
        }
    } else if (sscanf(sub, "/port/%u/set", &port) == 1 && port >= 1 &&
               port <= NUM_PORTS) {
        engine_cmd_t c = {.port = (uint8_t)(port - 1)};
        if (!strcasecmp(data, "hard_reset")) c.op = CMD_PORT_HARD_RESET;
        else if (!strcasecmp(data, "src_cap")) c.op = CMD_PORT_SRC_CAP;
        else c.op = on ? CMD_PORT_ENABLE : CMD_PORT_DISABLE;
        if (ipc_cmd_push(&c) && (c.op == CMD_PORT_ENABLE || c.op == CMD_PORT_DISABLE) &&
            settings_port_admin_note(port - 1, on))
            settings_save_later(); // the "last" boot policy keeps it
    } else if (strcmp(sub, "/budget/set") == 0) {
        int w = atoi(data);
        if (w >= BUDGET_MIN_W && w <= BUDGET_MAX_W) {
            g_settings.budget_mw = (uint32_t)w * 1000u;
            engine_cmd_t c = {.op = CMD_SET_BUDGET, .arg = g_settings.budget_mw};
            ipc_cmd_push(&c);
            settings_save_later();
        }
    } else if (strcmp(sub, "/led/set") == 0) {
        int b = atoi(data); // HA sends the slider value, possibly as "48.0"
        if (data[0] >= '0' && data[0] <= '9' && b >= 0 && b <= 255) {
            g_settings.led_brightness = (uint8_t)b;
            engine_cmd_t c = {.op = CMD_LED_BRIGHTNESS, .arg = (uint32_t)b};
            ipc_cmd_push(&c);
            settings_save_later();
        }
    } else if (strcmp(sub, "/fan/set") == 0) {
        // fan mode select: "auto"/"on"/"off" (plus legacy switch ON/OFF)
        if (!strcasecmp(data, "auto")) {
            g_settings.fan_auto = 1;
            engine_cmd_t c = {.op = CMD_FAN_AUTO};
            ipc_cmd_push(&c);
        } else {
            g_settings.fan_auto = 0;
            engine_cmd_t c = {.op = CMD_FAN, .arg = !strcasecmp(data, "on")};
            ipc_cmd_push(&c);
        }
        settings_save_later();
    } else if (strcmp(sub, "/improv/set") == 0) {
        if (!strcasecmp(data, "open")) improv_open(IMPROV_WINDOW_MS, "Home Assistant");
    } else if (strcmp(sub, "/update/latest") == 0) {
        // retained release pointer, published by CI or by hand:
        //   {"version":"x.y.z","url":"http://lan-host/controller.uf2"}
        json_str(data, "version", latest_version, sizeof(latest_version));
        json_str(data, "url", latest_url, sizeof(latest_url));
        publish_update_state();
    } else if (strcmp(sub, "/update/set") == 0) {
        if (strcasecmp(data, "install") != 0) return;
        if (!latest_url[0]) {
            printf("update: install requested but no update/latest url is set\n");
            return;
        }
        char e[96];
        if (!ota_pull_start(latest_url, e, sizeof(e)))
            printf("update: %s\n", e);
    }
}

static void inpub_cb(void *arg, const char *topic, u32_t tot_len) {
    (void)arg;
    snprintf(in_topic, sizeof(in_topic), "%s", topic);
    in_len = 0;
    if (tot_len >= sizeof(in_data)) in_topic[0] = '\0'; // oversized: ignore
}

static void indata_cb(void *arg, const u8_t *data, u16_t len, u8_t flags) {
    (void)arg;
    if (!in_topic[0]) return;
    if (in_len + len >= sizeof(in_data)) { in_topic[0] = '\0'; return; }
    memcpy(in_data + in_len, data, len);
    in_len += len;
    if (flags & MQTT_DATA_FLAG_LAST) {
        in_data[in_len] = '\0';
        handle_command(in_topic, in_data);
        in_topic[0] = '\0';
    }
}

// Command topics, subscribed one at a time after connect. Every subscribe
// holds one of lwIP's MQTT_REQ_MAX_IN_FLIGHT request slots until its SUBACK
// arrives; fired all at once they took every slot, so the first telemetry
// burst after each connect was refused (harmless, but it printed a refusal
// line every time). Discovery waits until they are all in.
static const char *const SUBS[] = {
    "port/+/set",     "port/+/priority/set", "port/+/limit/set", "port/+/volt/set",
    "port/+/boot/set", "port/+/autooff/set", "port/+/sleep/set", "charged_mw/set",
    "charged_min/set", "fan/set",            "budget/set",       "led/set",
    "update/latest",   "update/set",         "improv/set",
};
#define N_SUBS (sizeof(SUBS) / sizeof(SUBS[0]))
static unsigned sub_idx;  // next SUBS entry to send
static bool sub_inflight; // one awaiting its SUBACK

static void sub_cb(void *arg, err_t err) {
    (void)arg;
    sub_inflight = false;
    if (err != ERR_OK && sub_idx > 0) sub_idx--; // send it again
}

static void subscribe_step(void) {
    if (sub_inflight || sub_idx >= N_SUBS) return;
    snprintf(topic_buf, sizeof(topic_buf), "%s/%s", base, SUBS[sub_idx++]);
    err_t err = mqtt_sub_unsub(client, topic_buf, 1, sub_cb, NULL, 1);
    sub_inflight = err == ERR_OK;
    if (err != ERR_OK && sub_idx > 0) sub_idx--; // retry next tick
}

static bool subscribed(void) {
    return sub_idx >= N_SUBS && !sub_inflight;
}

// ---- connection ------------------------------------------------------------

static void connection_cb(mqtt_client_t *c, void *arg,
                          mqtt_connection_status_t status) {
    (void)c; (void)arg;
    if (status == MQTT_CONNECT_ACCEPTED) {
        state = ST_UP;
        discovery_idx = 0;
        disc_inflight = false;
        publish(will_topic, "online", 1, 1);
        sub_idx = 0; // command topics follow, one per SUBACK (subscribe_step)
        sub_inflight = false;
        publish_update_state();
        printf("mqtt: connected to %s\n", g_settings.mqtt_host);
    } else {
        state = ST_BACKOFF;
        printf("mqtt: disconnected (%d)\n", status);
    }
}

static void try_connect(void) {
    if (!client) {
        client = mqtt_client_new();
        if (!client) { state = ST_BACKOFF; return; }
        mqtt_set_inpub_callback(client, inpub_cb, indata_cb, NULL);
    }
    struct mqtt_connect_client_info_t ci;
    memset(&ci, 0, sizeof(ci));
    ci.client_id = client_id;
    ci.client_user = g_settings.mqtt_user[0] ? g_settings.mqtt_user : NULL;
    ci.client_pass = g_settings.mqtt_pass[0] ? g_settings.mqtt_pass : NULL;
    ci.keep_alive = KEEP_ALIVE_S;
    ci.will_topic = will_topic;
    ci.will_msg = "offline";
    ci.will_qos = 1;
    ci.will_retain = 1;

    state = ST_CONNECTING;
    if (mqtt_client_connect(client, &broker_ip, g_settings.mqtt_port,
                            connection_cb, NULL, &ci) != ERR_OK)
        state = ST_BACKOFF;
}

static void dns_cb(const char *name, const ip_addr_t *ipaddr, void *arg) {
    (void)name; (void)arg;
    if (state != ST_RESOLVING) return;
    if (ipaddr) {
        broker_ip = *ipaddr;
        try_connect();
    } else {
        state = ST_BACKOFF;
    }
}

// ---- Home Assistant discovery ----------------------------------------------

typedef struct {
    const char *object;   // uniq_id / config-topic suffix
    const char *name;
    const char *dev_class; // NULL = none
    const char *unit;      // NULL = none
    const char *extra;     // raw JSON fragment (state_class etc.), "" = none
    const char *template;
} sensor_spec_t;

#define MEASUREMENT "\"stat_cla\":\"measurement\","
#define TOTALINC    "\"stat_cla\":\"total_increasing\",\"sug_dsp_prc\":3,"

static const sensor_spec_t PORT_SENSORS[PORT_SENSOR_N] = {
    {"power",   "power",   "power",   "W",   MEASUREMENT, "{{ value_json.p }}"},
    {"voltage", "voltage", "voltage", "V",   MEASUREMENT, "{{ value_json.v }}"},
    {"current", "current", "current", "A",   MEASUREMENT, "{{ value_json.i }}"},
    {"energy",  "energy",  "energy",  "kWh", TOTALINC,    "{{ value_json.e }}"},
    {"state",   "state",   NULL,      NULL,  "",          "{{ value_json.state }}"},
};

static void discovery_config_topic(const char *component, const char *object) {
    snprintf(topic_buf, sizeof(topic_buf), "homeassistant/%s/pwrman_%s/%s/config",
             component, uid, object);
}

// The port's label (settings, or "Port N") escaped for a discovery payload.
static const char *port_label(unsigned port) {
    static char buf[PORT_NAME_MAX * 6 + 1];
    json_escape(buf, sizeof(buf), settings_port_name(port - 1));
    return buf;
}

static void publish_port_sensor(unsigned port, const sensor_spec_t *s) {
    char object[32];
    snprintf(object, sizeof(object), "p%u_%s", port, s->object);
    discovery_config_topic("sensor", object);

    char extras[128] = "";
    if (s->dev_class)
        snprintf(extras, sizeof(extras), "\"dev_cla\":\"%s\",\"unit_of_meas\":\"%s\",%s",
                 s->dev_class, s->unit, s->extra);
    // the state sensor also carries the port's newest fault as attributes
    char attrs[176] = "";
    if (!strcmp(s->object, "state"))
        snprintf(attrs, sizeof(attrs),
                 "\"json_attr_t\":\"~/port/%u/telemetry\",\"json_attr_tpl\":\"{{ {'last_fault': "
                 "value_json.last_fault, 'last_fault_at': value_json.last_fault_at} | tojson }}\",",
                 port);

    snprintf(payload_buf, sizeof(payload_buf),
             "{\"~\":\"%s\",\"name\":\"%s %s\",\"uniq_id\":\"pwrman_%s_%s\","
             "\"stat_t\":\"~/port/%u/telemetry\",\"avail_t\":\"~/availability\","
             "%s%s\"val_tpl\":\"%s\",\"dev\":%s}",
             base, port_label(port), s->name, uid, object, port, extras, attrs, s->template,
             device_json);
    publish(topic_buf, payload_buf, 1, 1);
}

static void publish_port_button(unsigned port, const char *action,
                                const char *label) {
    char object[32];
    snprintf(object, sizeof(object), "p%u_%s", port, action);
    discovery_config_topic("button", object);
    snprintf(payload_buf, sizeof(payload_buf),
             "{\"~\":\"%s\",\"name\":\"%s %s\",\"uniq_id\":\"pwrman_%s_%s\","
             "\"cmd_t\":\"~/port/%u/set\",\"pl_prs\":\"%s\","
             "\"avail_t\":\"~/availability\",\"ent_cat\":\"config\",\"dev\":%s}",
             base, port_label(port), label, uid, object, port, action, device_json);
    publish(topic_buf, payload_buf, 1, 1);
}

static void publish_port_number(unsigned port) {
    char object[32];
    snprintf(object, sizeof(object), "p%u_priority", port);
    discovery_config_topic("number", object);
    snprintf(payload_buf, sizeof(payload_buf),
             "{\"~\":\"%s\",\"name\":\"%s priority\","
             "\"uniq_id\":\"pwrman_%s_%s\",\"cmd_t\":\"~/port/%u/priority/set\","
             "\"stat_t\":\"~/port/%u/telemetry\","
             "\"val_tpl\":\"{{ value_json.prio }}\","
             "\"min\":0,\"max\":255,\"step\":1,\"mode\":\"box\","
             "\"ent_cat\":\"config\",\"avail_t\":\"~/availability\",\"dev\":%s}",
             base, port_label(port), uid, object, port, port, device_json);
    publish(topic_buf, payload_buf, 1, 1);
}

static void publish_port_limit_number(unsigned port) {
    char object[32];
    snprintf(object, sizeof(object), "p%u_limit", port);
    discovery_config_topic("number", object);
    snprintf(payload_buf, sizeof(payload_buf),
             "{\"~\":\"%s\",\"name\":\"%s current limit\","
             "\"uniq_id\":\"pwrman_%s_%s\",\"cmd_t\":\"~/port/%u/limit/set\","
             "\"stat_t\":\"~/port/%u/telemetry\","
             "\"val_tpl\":\"{{ value_json.limit_ma }}\",\"unit_of_meas\":\"mA\","
             "\"min\":%d,\"max\":%d,\"step\":20,\"mode\":\"box\",\"ic\":\"mdi:current-dc\","
             "\"ent_cat\":\"config\",\"avail_t\":\"~/availability\",\"dev\":%s}",
             base, port_label(port), uid, object, port, port, PORT_LIMIT_MIN_MA, PORT_LIMIT_MAX_MA,
             device_json);
    publish(topic_buf, payload_buf, 1, 1);
}

static void publish_port_volt_select(unsigned port) {
    char object[32];
    snprintf(object, sizeof(object), "p%u_volt", port);
    discovery_config_topic("select", object);
    snprintf(payload_buf, sizeof(payload_buf),
             "{\"~\":\"%s\",\"name\":\"%s voltage cap\",\"uniq_id\":\"pwrman_%s_%s\","
             "\"cmd_t\":\"~/port/%u/volt/set\",\"stat_t\":\"~/port/%u/telemetry\","
             "\"val_tpl\":\"{{ value_json.max_v }} V\","
             "\"ops\":[\"5 V\",\"9 V\",\"12 V\",\"15 V\",\"20 V\"],"
             "\"ic\":\"mdi:flash\",\"ent_cat\":\"config\","
             "\"avail_t\":\"~/availability\",\"dev\":%s}",
             base, port_label(port), uid, object, port, port, device_json);
    publish(topic_buf, payload_buf, 1, 1);
}

static void publish_port_boot_select(unsigned port) {
    char object[32];
    snprintf(object, sizeof(object), "p%u_boot", port);
    discovery_config_topic("select", object);
    snprintf(payload_buf, sizeof(payload_buf),
             "{\"~\":\"%s\",\"name\":\"%s at power-up\",\"uniq_id\":\"pwrman_%s_%s\","
             "\"cmd_t\":\"~/port/%u/boot/set\",\"stat_t\":\"~/port/%u/telemetry\","
             "\"val_tpl\":\"{{ value_json.boot }}\",\"ops\":[\"on\",\"off\",\"last\"],"
             "\"ic\":\"mdi:power-settings\",\"ent_cat\":\"config\","
             "\"avail_t\":\"~/availability\",\"dev\":%s}",
             base, port_label(port), uid, object, port, port, device_json);
    publish(topic_buf, payload_buf, 1, 1);
}

// One HA event entity per port, fed from the shared base/event topic: the
// template keeps this port's events with a kind and drops the rest (an
// empty render is ignored). code/arg/text ride along as attributes.
static void publish_port_event(unsigned port) {
    char object[32];
    snprintf(object, sizeof(object), "p%u_event", port);
    discovery_config_topic("event", object);
    char tpl[224];
    snprintf(tpl, sizeof(tpl),
             "{%% if value_json.port == %u and value_json.kind %%}"
             "{{ {'event_type': value_json.kind, 'code': value_json.code, "
             "'arg': value_json.arg, 'text': value_json.text} | tojson }}{%% endif %%}",
             port);
    snprintf(payload_buf, sizeof(payload_buf),
             "{\"~\":\"%s\",\"name\":\"%s events\",\"uniq_id\":\"pwrman_%s_%s\","
             "\"stat_t\":\"~/event\",\"avail_t\":\"~/availability\","
             "\"event_types\":" EVENT_KINDS_JSON ",\"val_tpl\":\"%s\","
             "\"ic\":\"mdi:usb-port\",\"dev\":%s}",
             base, port_label(port), uid, object, tpl, device_json);
    publish(topic_buf, payload_buf, 1, 1);
}

// Charging (device class battery_charging): a sink is attached and not yet
// charged. "Charged" is the engine's verdict from the draw (settings
// charged_mw / charged_min), so this reads OFF while a full phone trickles.
static void publish_port_charging_sensor(unsigned port) {
    char object[32];
    snprintf(object, sizeof(object), "p%u_charging", port);
    discovery_config_topic("binary_sensor", object);
    snprintf(payload_buf, sizeof(payload_buf),
             "{\"~\":\"%s\",\"name\":\"%s charging\",\"uniq_id\":\"pwrman_%s_%s\","
             "\"stat_t\":\"~/port/%u/telemetry\",\"avail_t\":\"~/availability\","
             "\"dev_cla\":\"battery_charging\",\"val_tpl\":\"{{ 'ON' if value_json.state in "
             "['active', 'throttled'] and not value_json.charged else 'OFF' }}\",\"dev\":%s}",
             base, port_label(port), uid, object, port, device_json);
    publish(topic_buf, payload_buf, 1, 1);
}

static void publish_port_autooff_switch(unsigned port) {
    char object[32];
    snprintf(object, sizeof(object), "p%u_autooff", port);
    discovery_config_topic("switch", object);
    snprintf(payload_buf, sizeof(payload_buf),
             "{\"~\":\"%s\",\"name\":\"%s off when charged\",\"uniq_id\":\"pwrman_%s_%s\","
             "\"cmd_t\":\"~/port/%u/autooff/set\",\"stat_t\":\"~/port/%u/telemetry\","
             "\"val_tpl\":\"{{ 'ON' if value_json.auto_off else 'OFF' }}\","
             "\"ic\":\"mdi:battery-check\",\"ent_cat\":\"config\","
             "\"avail_t\":\"~/availability\",\"dev\":%s}",
             base, port_label(port), uid, object, port, port, device_json);
    publish(topic_buf, payload_buf, 1, 1);
}

static void publish_port_sleep_number(unsigned port) {
    char object[32];
    snprintf(object, sizeof(object), "p%u_sleep", port);
    discovery_config_topic("number", object);
    snprintf(payload_buf, sizeof(payload_buf),
             "{\"~\":\"%s\",\"name\":\"%s sleep timer\",\"uniq_id\":\"pwrman_%s_%s\","
             "\"cmd_t\":\"~/port/%u/sleep/set\",\"stat_t\":\"~/port/%u/telemetry\","
             "\"val_tpl\":\"{{ value_json.sleep_min }}\",\"unit_of_meas\":\"min\","
             "\"min\":0,\"max\":%d,\"step\":5,\"mode\":\"box\",\"ic\":\"mdi:timer-outline\","
             "\"ent_cat\":\"config\",\"avail_t\":\"~/availability\",\"dev\":%s}",
             base, port_label(port), uid, object, port, port, PORT_SLEEP_MAX_MIN, device_json);
    publish(topic_buf, payload_buf, 1, 1);
}

static void publish_charged_numbers(int which) {
    const char *object = which ? "charged_min" : "charged_mw";
    discovery_config_topic("number", object);
    snprintf(payload_buf, sizeof(payload_buf),
             "{\"~\":\"%s\",\"name\":\"%s\",\"uniq_id\":\"pwrman_%s_%s\","
             "\"cmd_t\":\"~/%s/set\",\"stat_t\":\"~/status\",\"val_tpl\":\"{{ value_json.%s }}\","
             "\"unit_of_meas\":\"%s\",\"min\":%d,\"max\":%d,\"step\":%d,\"mode\":\"box\","
             "\"ic\":\"%s\",\"ent_cat\":\"config\",\"avail_t\":\"~/availability\",\"dev\":%s}",
             base, which ? "Charged after" : "Charged below", uid, object, object, object,
             which ? "min" : "mW", which ? 1 : 0, which ? 255 : 20000, which ? 1 : 100,
             which ? "mdi:timer-sand" : "mdi:battery-charging-low", device_json);
    publish(topic_buf, payload_buf, 1, 1);
}

static void publish_port_switch(unsigned port) {
    char object[32];
    snprintf(object, sizeof(object), "p%u_enable", port);
    discovery_config_topic("switch", object);
    snprintf(payload_buf, sizeof(payload_buf),
             "{\"~\":\"%s\",\"name\":\"%s\",\"uniq_id\":\"pwrman_%s_%s\","
             "\"cmd_t\":\"~/port/%u/set\",\"stat_t\":\"~/port/%u/telemetry\","
             "\"avail_t\":\"~/availability\","
             "\"val_tpl\":\"{{ 'OFF' if value_json.state == 'disabled' else 'ON' }}\","
             "\"dev\":%s}",
             base, port_label(port), uid, object, port, port, device_json);
    publish(topic_buf, payload_buf, 1, 1);
}

static void publish_chassis_sensor(const char *object, const char *name,
                                   const char *dev_cla, const char *unit,
                                   const char *extra, const char *tpl) {
    discovery_config_topic("sensor", object);
    snprintf(payload_buf, sizeof(payload_buf),
             "{\"~\":\"%s\",\"name\":\"%s\",\"uniq_id\":\"pwrman_%s_%s\","
             "\"stat_t\":\"~/status\",\"avail_t\":\"~/availability\","
             "\"dev_cla\":\"%s\",\"unit_of_meas\":\"%s\",%s\"val_tpl\":\"%s\","
             "\"dev\":%s}",
             base, name, uid, object, dev_cla, unit, extra, tpl, device_json);
    publish(topic_buf, payload_buf, 1, 1);
}

static void publish_budget_number(void) {
    discovery_config_topic("number", "budget");
    snprintf(payload_buf, sizeof(payload_buf),
             "{\"~\":\"%s\",\"name\":\"Power budget\","
             "\"uniq_id\":\"pwrman_%s_budget\",\"cmd_t\":\"~/budget/set\","
             "\"stat_t\":\"~/status\",\"val_tpl\":\"{{ value_json.budget_w | round(0) }}\","
             "\"min\":%d,\"max\":%d,\"step\":1,\"mode\":\"box\","
             "\"dev_cla\":\"power\",\"unit_of_meas\":\"W\","
             "\"ent_cat\":\"config\",\"avail_t\":\"~/availability\",\"dev\":%s}",
             base, uid, BUDGET_MIN_W, BUDGET_MAX_W, device_json);
    publish(topic_buf, payload_buf, 1, 1);
}

static void publish_led_number(void) {
    discovery_config_topic("number", "led");
    snprintf(payload_buf, sizeof(payload_buf),
             "{\"~\":\"%s\",\"name\":\"LED brightness\","
             "\"uniq_id\":\"pwrman_%s_led\",\"cmd_t\":\"~/led/set\","
             "\"stat_t\":\"~/status\",\"val_tpl\":\"{{ value_json.led }}\","
             "\"min\":0,\"max\":255,\"step\":1,\"mode\":\"slider\","
             "\"ic\":\"mdi:led-on\","
             "\"ent_cat\":\"config\",\"avail_t\":\"~/availability\",\"dev\":%s}",
             base, uid, device_json);
    publish(topic_buf, payload_buf, 1, 1);
}

static void publish_update_entity(void) {
    discovery_config_topic("update", "fw");
    snprintf(payload_buf, sizeof(payload_buf),
             "{\"~\":\"%s\",\"name\":\"Firmware\",\"uniq_id\":\"pwrman_%s_fw\","
             "\"stat_t\":\"~/update/state\",\"cmd_t\":\"~/update/set\","
             "\"pl_inst\":\"install\",\"dev_cla\":\"firmware\","
             "\"ent_cat\":\"config\",\"avail_t\":\"~/availability\",\"dev\":%s}",
             base, uid, device_json);
    publish(topic_buf, payload_buf, 1, 1);
}

static void publish_improv_button(void) {
    discovery_config_topic("button", "improv");
    snprintf(payload_buf, sizeof(payload_buf),
             "{\"~\":\"%s\",\"name\":\"Open BLE provisioning\","
             "\"uniq_id\":\"pwrman_%s_improv\",\"cmd_t\":\"~/improv/set\","
             "\"pl_prs\":\"open\",\"ic\":\"mdi:bluetooth-settings\","
             "\"ent_cat\":\"config\",\"avail_t\":\"~/availability\",\"dev\":%s}",
             base, uid, device_json);
    publish(topic_buf, payload_buf, 1, 1);
}

static void publish_boot_sensor(void) {
    discovery_config_topic("sensor", "boot");
    snprintf(payload_buf, sizeof(payload_buf),
             "{\"~\":\"%s\",\"name\":\"Last boot reason\",\"uniq_id\":\"pwrman_%s_boot\","
             "\"stat_t\":\"~/status\",\"avail_t\":\"~/availability\","
             "\"val_tpl\":\"{{ value_json.boot }}\",\"ic\":\"mdi:restart\","
             "\"ent_cat\":\"diagnostic\",\"dev\":%s}",
             base, uid, device_json);
    publish(topic_buf, payload_buf, 1, 1);
}

static void publish_problem_sensor(void) {
    discovery_config_topic("binary_sensor", "problem");
    snprintf(payload_buf, sizeof(payload_buf),
             "{\"~\":\"%s\",\"name\":\"Problem\",\"uniq_id\":\"pwrman_%s_problem\","
             "\"stat_t\":\"~/status\",\"avail_t\":\"~/availability\",\"dev_cla\":\"problem\","
             "\"val_tpl\":\"{{ value_json.problem }}\",\"json_attr_t\":\"~/status\","
             "\"json_attr_tpl\":\"{{ {'detail': value_json.problems} | tojson }}\","
             "\"ent_cat\":\"diagnostic\",\"dev\":%s}",
             base, uid, device_json);
    publish(topic_buf, payload_buf, 1, 1);
}

static void publish_led_mode_sensor(void) {
    discovery_config_topic("sensor", "led_mode");
    snprintf(payload_buf, sizeof(payload_buf),
             "{\"~\":\"%s\",\"name\":\"LED mode\",\"uniq_id\":\"pwrman_%s_led_mode\","
             "\"stat_t\":\"~/status\",\"avail_t\":\"~/availability\","
             "\"val_tpl\":\"{{ value_json.led_mode }}\",\"ic\":\"mdi:brightness-6\","
             "\"ent_cat\":\"diagnostic\",\"dev\":%s}",
             base, uid, device_json);
    publish(topic_buf, payload_buf, 1, 1);
}

static void publish_fan_select(void) {
    discovery_config_topic("select", "fan_mode");
    snprintf(payload_buf, sizeof(payload_buf),
             "{\"~\":\"%s\",\"name\":\"Fan\",\"uniq_id\":\"pwrman_%s_fan_mode\","
             "\"cmd_t\":\"~/fan/set\",\"stat_t\":\"~/status\","
             "\"avail_t\":\"~/availability\",\"ops\":[\"auto\",\"on\",\"off\"],"
             "\"val_tpl\":\"{{ value_json.fan_mode }}\",\"dev\":%s}",
             base, uid, device_json);
    publish(topic_buf, payload_buf, 1, 1);
}

// UPS entities exist only while a supply answers on the UPS header; without
// one the config is retracted so a board that never had a UPS shows none.
static void publish_ups_entity(const char *component, const char *object, const char *name,
                               const char *extra, const char *tpl) {
    discovery_config_topic(component, object);
    if (!ups_present()) {
        publish(topic_buf, "", 1, 1);
        return;
    }
    snprintf(payload_buf, sizeof(payload_buf),
             "{\"~\":\"%s\",\"name\":\"%s\",\"uniq_id\":\"pwrman_%s_%s\","
             "\"stat_t\":\"~/status\",\"avail_t\":\"~/availability\",%s"
             "\"val_tpl\":\"%s\",\"dev\":%s}",
             base, name, uid, object, extra, tpl, device_json);
    publish(topic_buf, payload_buf, 1, 1);
}

// one config per poll tick: paces the burst well inside the output ring buffer
static void discovery_publish(int i);

static void discovery_step(void) {
    if (disc_inflight || discovery_idx >= N_DISCOVERY) return;
    int i = discovery_idx++;
    disc_publishing = true;
    discovery_publish(i);
    disc_publishing = false;
}

static void discovery_publish(int i) {
    if (i < NUM_PORTS * PORT_ENTITIES) {
        unsigned port = (unsigned)(i / PORT_ENTITIES) + 1;
        int e = i % PORT_ENTITIES;
        if (e < PORT_SENSOR_N) publish_port_sensor(port, &PORT_SENSORS[e]);
        else if (e == PORT_SENSOR_N) publish_port_switch(port);
        else if (e == PORT_SENSOR_N + 1) publish_port_button(port, "hard_reset", "hard reset");
        else if (e == PORT_SENSOR_N + 2) publish_port_button(port, "src_cap", "re-announce caps");
        else if (e == PORT_SENSOR_N + 3) publish_port_number(port);
        else if (e == PORT_SENSOR_N + 4) publish_port_limit_number(port);
        else if (e == PORT_SENSOR_N + 5) publish_port_boot_select(port);
        else if (e == PORT_SENSOR_N + 6) publish_port_event(port);
        else if (e == PORT_SENSOR_N + 7) publish_port_charging_sensor(port);
        else if (e == PORT_SENSOR_N + 8) publish_port_autooff_switch(port);
        else if (e == PORT_SENSOR_N + 9) publish_port_sleep_number(port);
        else publish_port_volt_select(port);
        return;
    }
    switch (i - NUM_PORTS * PORT_ENTITIES) {
    case 0:
        publish_chassis_sensor("total_power", "Total power", "power", "W",
                               MEASUREMENT, "{{ value_json.total_w }}");
        break;
    case 1:
        publish_chassis_sensor("headroom", "Budget headroom", "power", "W",
                               MEASUREMENT, "{{ value_json.headroom_w }}");
        break;
    case 2:
        publish_chassis_sensor("energy", "Total energy", "energy", "kWh",
                               TOTALINC, "{{ value_json.energy_kwh }}");
        break;
    case 3:
        publish_fan_select();
        break;
    case 4:
        publish_budget_number();
        break;
    case 5:
        publish_update_entity();
        break;
    case 6:
        publish_improv_button();
        break;
    case 7:
        publish_led_number();
        break;
    case 8:
        publish_boot_sensor();
        break;
    case 9:
        publish_problem_sensor();
        break;
    case 10:
        publish_charged_numbers(0);
        break;
    case 11:
        publish_charged_numbers(1);
        break;
    case 12:
        publish_led_mode_sensor();
        break;
    case 13:
        publish_ups_entity("binary_sensor", "ups_ac", "UPS AC input", "\"dev_cla\":\"plug\",",
                           "{{ value_json.ups_ac }}");
        break;
    case 14:
        publish_ups_entity("binary_sensor", "ups_on_battery", "UPS on battery",
                           "\"ic\":\"mdi:battery-arrow-down\",", "{{ value_json.ups_on_battery }}");
        break;
    case 15:
        publish_ups_entity("binary_sensor", "ups_charging", "UPS charging",
                           "\"dev_cla\":\"battery_charging\",", "{{ value_json.ups_charging }}");
        break;
    case 16:
        publish_ups_entity("sensor", "ups_batt_v", "UPS battery voltage",
                           "\"dev_cla\":\"voltage\",\"unit_of_meas\":\"V\"," MEASUREMENT,
                           "{{ value_json.ups_batt_v }}");
        break;
    case 17:
        publish_ups_entity("sensor", "ups_mains_v", "UPS mains voltage",
                           "\"dev_cla\":\"voltage\",\"unit_of_meas\":\"V\"," MEASUREMENT,
                           "{{ value_json.ups_mains_v }}");
        break;
    case 18:
        publish_ups_entity("sensor", "ups_load_a", "UPS load current",
                           "\"dev_cla\":\"current\",\"unit_of_meas\":\"A\"," MEASUREMENT,
                           "{{ value_json.ups_load_a }}");
        break;
    default:
        // retire the fan switch this select replaced from older firmware
        discovery_config_topic("switch", "fan");
        publish(topic_buf, "", 1, 1);
        break;
    }
}

void mqtt_names_changed(void) {
    if (state == ST_UP) discovery_idx = 0; // otherwise the next connect does it
}

// ---- telemetry & events ----------------------------------------------------

static void publish_telemetry(void) {
    telemetry_t t;
    ipc_snapshot_read(&t);

    // chassis status first: if the output buffer is tight, the tail of the
    // burst is what gets refused, and a port sample is the cheaper loss
    uint32_t headroom = t.budget_mw > t.reserved_mw ? t.budget_mw - t.reserved_mw : 0;
    char boot_text[80];
    boot_reason_text(boot_reason_last(), boot_text, sizeof(boot_text));
    char problems[192];
    static char problems_json[256]; // labels are user text
    unsigned n_problems = health_problems(&t, problems, sizeof(problems));
    json_escape(problems_json, sizeof(problems_json), problems);
    const ups_state_t *u = ups_state(); // zeros while absent: the entities are retracted then
    snprintf(topic_buf, sizeof(topic_buf), "%s/status", base);
    snprintf(payload_buf, sizeof(payload_buf),
             "{\"total_w\":%.2f,\"reserved_w\":%.1f,\"budget_w\":%.1f,"
             "\"headroom_w\":%.1f,\"energy_kwh\":%.3f,\"fan\":\"%s\","
             "\"fan_mode\":\"%s\",\"alert\":%s,\"rssi\":%ld,\"uptime_s\":%lu,"
             "\"led\":%u,\"fw\":\"%s\",\"boot\":\"%s\",\"problem\":\"%s\",\"problems\":\"%s\","
             "\"charged_mw\":%u,\"charged_min\":%u,\"led_mode\":\"%s\","
             "\"ups\":%s,\"ups_ac\":\"%s\",\"ups_on_battery\":\"%s\",\"ups_charging\":\"%s\","
             "\"ups_batt_v\":%.2f,\"ups_mains_v\":%.1f,\"ups_load_a\":%.2f}",
             t.total_mw / 1000.0, t.reserved_mw / 1000.0, t.budget_mw / 1000.0,
             headroom / 1000.0, t.energy_mwh / 1e6, t.fan_on ? "ON" : "OFF",
             t.fan_auto ? "auto" : (t.fan_on ? "on" : "off"),
             t.alert_active ? "true" : "false", (long)net_rssi(),
             (unsigned long)(to_ms_since_boot(get_absolute_time()) / 1000),
             g_settings.led_brightness, FW_VERSION, boot_text,
             n_problems ? "ON" : "OFF", problems_json, g_settings.charged_mw,
             g_settings.charged_min, led_mode_name(led_sched_current()),
             u->present ? "true" : "false", (u->status_l & LAD_ST_AC_OK) ? "ON" : "OFF",
             (u->status_l & LAD_ST_ON_BATTERY) ? "ON" : "OFF",
             (u->status_l & LAD_ST_CHARGING) ? "ON" : "OFF", u->batt_cv / 100.0,
             u->mains_dv / 10.0, u->load_ca / 100.0);
    publish(topic_buf, payload_buf, 0, 1);

    for (unsigned i = 0; i < NUM_PORTS; i++) {
        const port_telemetry_t *p = &t.port[i];
        snprintf(topic_buf, sizeof(topic_buf), "%s/port/%u/telemetry", base, i + 1);
        fault_rec_t lf;
        char lf_text[48] = "";
        uint32_t lf_at = 0;
        if (fault_log_last(i, &lf)) {
            fault_text(&lf, lf_text, sizeof(lf_text));
            lf_at = lf.epoch; // 0 until SNTP had synced at the time
        }
        snprintf(payload_buf, sizeof(payload_buf),
                 "{\"state\":\"%s\",\"v\":%.3f,\"i\":%.3f,\"p\":%.2f,\"e\":%.3f,"
                 "\"pdo\":%u,\"contract_w\":%.1f,\"prio\":%u,\"limit_ma\":%lu,\"max_v\":%u,"
                 "\"boot\":\"%s\",\"charged\":%s,\"auto_off\":%s,\"sleep_min\":%u,"
                 "\"fault\":%u,\"last_fault\":\"%s\",\"last_fault_at\":%lu}",
                 port_state_name((port_state_t)p->state), p->bus_mv / 1000.0,
                 p->current_ma / 1000.0, p->power_mw / 1000.0,
                 p->energy_mwh / 1e6, p->selected_pdo,
                 p->contract_mw / 1000.0, g_settings.port_priority[i],
                 (unsigned long)g_settings.port_limit_ma[i], g_settings.port_max_mv[i] / 1000,
                 settings_port_boot_name(g_settings.port_boot[i]),
                 p->charged ? "true" : "false",
                 (g_settings.port_auto_off >> i) & 1 ? "true" : "false",
                 g_settings.port_sleep_min[i], p->fault_bits, lf_text, (unsigned long)lf_at);
        publish(topic_buf, payload_buf, 0, 0);
    }

    if (pub_dropped) {
        printf("mqtt: %lu publish(es) refused by lwIP (last %s, err %d)\n",
               (unsigned long)pub_dropped, pub_dropped_topic, (int)pub_dropped_err);
        // A link that acknowledges too slowly keeps the client "connected"
        // (lwIP's watchdog only wants some traffic back) while its output
        // ring and request slots stay full and nothing gets out. Once
        // nothing at all has left for a while, drop the connection: the
        // stuck send buffer goes with it and the reconnect starts clean.
        stalled_cycles = pub_dropped >= 1 + NUM_PORTS ? stalled_cycles + 1 : 0;
        pub_dropped = 0;
        if (stalled_cycles >= STALL_CYCLES) {
            printf("mqtt: nothing published for %us, reconnecting\n", STALL_CYCLES);
            stalled_cycles = 0;
            mqtt_disconnect(client);
            state = ST_BACKOFF;
        }
    } else {
        stalled_cycles = 0;
    }
}

static const char *evt_name(evt_type_t t) {
    switch (t) {
    case EVT_STATE_CHANGE: return "state";
    case EVT_FAULT:        return "fault";
    case EVT_CONTRACT:     return "contract";
    case EVT_PROBE_FAIL:   return "probe_fail";
    case EVT_THROTTLE:     return "throttle";
    case EVT_BOOT:         return "boot";
    default:               return "?";
    }
}

void mqtt_event(const engine_evt_t *e) {
    if (!mqtt_is_connected()) return; // transient events aren't queued
    char text[48] = "";
    if (e->type == EVT_FAULT || e->type == EVT_PROBE_FAIL) {
        fault_rec_t r = {.port = e->port, .type = e->type, .code = e->code, .arg = e->arg};
        fault_text(&r, text, sizeof(text)); // plain words: no escaping needed
    } else if (e->type == EVT_CHARGE && e->code == CHARGE_AUTO_OFF) {
        snprintf(text, sizeof(text), "%s", e->arg == AUTO_OFF_SLEEP ? "sleep timer" : "charged");
    }
    net_lock();
    snprintf(topic_buf, sizeof(topic_buf), "%s/event", base);
    snprintf(payload_buf, sizeof(payload_buf),
             "{\"port\":%u,\"event\":\"%s\",\"kind\":\"%s\",\"code\":%u,\"arg\":%lu,"
             "\"text\":\"%s\",\"ts\":%lu}",
             e->port + 1, evt_name((evt_type_t)e->type), event_kind(e), e->code,
             (unsigned long)e->arg, text, (unsigned long)net_epoch());
    publish(topic_buf, payload_buf, 1, 0);
    net_unlock();
}

// ---- driver ----------------------------------------------------------------

void mqtt_poll(uint32_t now_ms) {
    if (!net_available() || !g_settings.mqtt_host[0]) return;
    ensure_ids();

    net_lock();

    switch (state) {
    case ST_IDLE:
        if (net_up()) {
            err_t err = dns_gethostbyname(g_settings.mqtt_host, &broker_ip,
                                          dns_cb, NULL);
            if (err == ERR_OK) try_connect();
            else if (err == ERR_INPROGRESS) state = ST_RESOLVING;
            else state = ST_BACKOFF;
            if (state == ST_BACKOFF) backoff_until_ms = now_ms + BACKOFF_MS;
        }
        break;

    case ST_RESOLVING:
    case ST_CONNECTING:
        break; // waiting on callbacks

    case ST_UP:
        if (!mqtt_client_is_connected(client)) {
            state = ST_BACKOFF;
            backoff_until_ms = now_ms + BACKOFF_MS;
            break;
        }
        subscribe_step();
        if (subscribed()) discovery_step();
        if (now_ms - last_telemetry_ms >= TELEMETRY_MS) {
            last_telemetry_ms = now_ms;
            publish_telemetry();
        }
        break;

    case ST_BACKOFF:
        if (backoff_until_ms == 0) backoff_until_ms = now_ms + BACKOFF_MS;
        if (now_ms >= backoff_until_ms) {
            backoff_until_ms = 0;
            state = ST_IDLE;
        }
        break;
    }

    net_unlock();
}
