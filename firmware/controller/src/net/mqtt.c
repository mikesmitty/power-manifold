#include "mqtt.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "pico/unique_id.h"

#include "lwip/apps/mqtt.h"
#include "lwip/dns.h"

#include "improv.h"
#include "ipc.h"
#include "manifold.h"
#include "net.h"
#include "ota_pull.h"
#include "settings.h"

#define KEEP_ALIVE_S     30
#define BACKOFF_MS       (5 * 1000)
#define TELEMETRY_MS     1000

// discovery entity table: per-port sensors + switch + buttons + priority
// number, chassis sensors + fan + BLE provisioning button (the last chassis
// step retracts the pre-select fan switch config)
#define PORT_SENSOR_N    5
#define PORT_ENTITIES    (PORT_SENSOR_N + 4)
#define CHASSIS_ENTITIES 8
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
static char payload_buf[768];
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

static void publish(const char *topic, const char *payload, uint8_t qos,
                    uint8_t retain) {
    if (!client) return;
    mqtt_publish(client, topic, payload, (uint16_t)strlen(payload), qos, retain,
                 NULL, NULL);
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
    if (sscanf(sub, "/port/%u/priority/set", &port) == 1 &&
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
        ipc_cmd_push(&c);
    } else if (strcmp(sub, "/budget/set") == 0) {
        int w = atoi(data);
        if (w >= BUDGET_MIN_W && w <= BUDGET_MAX_W) {
            g_settings.budget_mw = (uint32_t)w * 1000u;
            engine_cmd_t c = {.op = CMD_SET_BUDGET, .arg = g_settings.budget_mw};
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

// ---- connection ------------------------------------------------------------

static void connection_cb(mqtt_client_t *c, void *arg,
                          mqtt_connection_status_t status) {
    (void)c; (void)arg;
    if (status == MQTT_CONNECT_ACCEPTED) {
        state = ST_UP;
        discovery_idx = 0;
        publish(will_topic, "online", 1, 1);
        snprintf(topic_buf, sizeof(topic_buf), "%s/port/+/set", base);
        mqtt_sub_unsub(client, topic_buf, 1, NULL, NULL, 1);
        snprintf(topic_buf, sizeof(topic_buf), "%s/port/+/priority/set", base);
        mqtt_sub_unsub(client, topic_buf, 1, NULL, NULL, 1);
        snprintf(topic_buf, sizeof(topic_buf), "%s/fan/set", base);
        mqtt_sub_unsub(client, topic_buf, 1, NULL, NULL, 1);
        snprintf(topic_buf, sizeof(topic_buf), "%s/budget/set", base);
        mqtt_sub_unsub(client, topic_buf, 1, NULL, NULL, 1);
        snprintf(topic_buf, sizeof(topic_buf), "%s/update/latest", base);
        mqtt_sub_unsub(client, topic_buf, 1, NULL, NULL, 1);
        snprintf(topic_buf, sizeof(topic_buf), "%s/update/set", base);
        mqtt_sub_unsub(client, topic_buf, 1, NULL, NULL, 1);
        snprintf(topic_buf, sizeof(topic_buf), "%s/improv/set", base);
        mqtt_sub_unsub(client, topic_buf, 1, NULL, NULL, 1);
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

static void publish_port_sensor(unsigned port, const sensor_spec_t *s) {
    char object[32];
    snprintf(object, sizeof(object), "p%u_%s", port, s->object);
    discovery_config_topic("sensor", object);

    char extras[128] = "";
    if (s->dev_class)
        snprintf(extras, sizeof(extras), "\"dev_cla\":\"%s\",\"unit_of_meas\":\"%s\",%s",
                 s->dev_class, s->unit, s->extra);

    snprintf(payload_buf, sizeof(payload_buf),
             "{\"~\":\"%s\",\"name\":\"Port %u %s\",\"uniq_id\":\"pwrman_%s_%s\","
             "\"stat_t\":\"~/port/%u/telemetry\",\"avail_t\":\"~/availability\","
             "%s\"val_tpl\":\"%s\",\"dev\":%s}",
             base, port, s->name, uid, object, port, extras, s->template,
             device_json);
    publish(topic_buf, payload_buf, 1, 1);
}

static void publish_port_button(unsigned port, const char *action,
                                const char *label) {
    char object[32];
    snprintf(object, sizeof(object), "p%u_%s", port, action);
    discovery_config_topic("button", object);
    snprintf(payload_buf, sizeof(payload_buf),
             "{\"~\":\"%s\",\"name\":\"Port %u %s\",\"uniq_id\":\"pwrman_%s_%s\","
             "\"cmd_t\":\"~/port/%u/set\",\"pl_prs\":\"%s\","
             "\"avail_t\":\"~/availability\",\"ent_cat\":\"config\",\"dev\":%s}",
             base, port, label, uid, object, port, action, device_json);
    publish(topic_buf, payload_buf, 1, 1);
}

static void publish_port_number(unsigned port) {
    char object[32];
    snprintf(object, sizeof(object), "p%u_priority", port);
    discovery_config_topic("number", object);
    snprintf(payload_buf, sizeof(payload_buf),
             "{\"~\":\"%s\",\"name\":\"Port %u priority\","
             "\"uniq_id\":\"pwrman_%s_%s\",\"cmd_t\":\"~/port/%u/priority/set\","
             "\"stat_t\":\"~/port/%u/telemetry\","
             "\"val_tpl\":\"{{ value_json.prio }}\","
             "\"min\":0,\"max\":255,\"step\":1,\"mode\":\"box\","
             "\"ent_cat\":\"config\",\"avail_t\":\"~/availability\",\"dev\":%s}",
             base, port, uid, object, port, port, device_json);
    publish(topic_buf, payload_buf, 1, 1);
}

static void publish_port_switch(unsigned port) {
    char object[32];
    snprintf(object, sizeof(object), "p%u_enable", port);
    discovery_config_topic("switch", object);
    snprintf(payload_buf, sizeof(payload_buf),
             "{\"~\":\"%s\",\"name\":\"Port %u\",\"uniq_id\":\"pwrman_%s_%s\","
             "\"cmd_t\":\"~/port/%u/set\",\"stat_t\":\"~/port/%u/telemetry\","
             "\"avail_t\":\"~/availability\","
             "\"val_tpl\":\"{{ 'OFF' if value_json.state == 'disabled' else 'ON' }}\","
             "\"dev\":%s}",
             base, port, uid, object, port, port, device_json);
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

// one config per poll tick: paces the burst well inside the output ring buffer
static void discovery_step(void) {
    if (discovery_idx >= N_DISCOVERY) return;
    int i = discovery_idx++;
    if (i < NUM_PORTS * PORT_ENTITIES) {
        unsigned port = (unsigned)(i / PORT_ENTITIES) + 1;
        int e = i % PORT_ENTITIES;
        if (e < PORT_SENSOR_N) publish_port_sensor(port, &PORT_SENSORS[e]);
        else if (e == PORT_SENSOR_N) publish_port_switch(port);
        else if (e == PORT_SENSOR_N + 1) publish_port_button(port, "hard_reset", "hard reset");
        else if (e == PORT_SENSOR_N + 2) publish_port_button(port, "src_cap", "re-announce caps");
        else publish_port_number(port);
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
    default:
        // retire the fan switch this select replaced from older firmware
        discovery_config_topic("switch", "fan");
        publish(topic_buf, "", 1, 1);
        break;
    }
}

// ---- telemetry & events ----------------------------------------------------

static void publish_telemetry(void) {
    telemetry_t t;
    ipc_snapshot_read(&t);

    for (unsigned i = 0; i < NUM_PORTS; i++) {
        const port_telemetry_t *p = &t.port[i];
        snprintf(topic_buf, sizeof(topic_buf), "%s/port/%u/telemetry", base, i + 1);
        snprintf(payload_buf, sizeof(payload_buf),
                 "{\"state\":\"%s\",\"v\":%.3f,\"i\":%.3f,\"p\":%.2f,\"e\":%.3f,"
                 "\"pdo\":%u,\"contract_w\":%.1f,\"prio\":%u,\"fault\":%u}",
                 port_state_name((port_state_t)p->state), p->bus_mv / 1000.0,
                 p->current_ma / 1000.0, p->power_mw / 1000.0,
                 p->energy_mwh / 1e6, p->selected_pdo,
                 p->contract_mw / 1000.0, g_settings.port_priority[i],
                 p->fault_bits);
        publish(topic_buf, payload_buf, 0, 0);
    }

    uint32_t headroom = t.budget_mw > t.reserved_mw ? t.budget_mw - t.reserved_mw : 0;
    snprintf(topic_buf, sizeof(topic_buf), "%s/status", base);
    snprintf(payload_buf, sizeof(payload_buf),
             "{\"total_w\":%.2f,\"reserved_w\":%.1f,\"budget_w\":%.1f,"
             "\"headroom_w\":%.1f,\"energy_kwh\":%.3f,\"fan\":\"%s\","
             "\"fan_mode\":\"%s\",\"alert\":%s,\"rssi\":%ld,\"uptime_s\":%lu,"
             "\"fw\":\"%s\"}",
             t.total_mw / 1000.0, t.reserved_mw / 1000.0, t.budget_mw / 1000.0,
             headroom / 1000.0, t.energy_mwh / 1e6, t.fan_on ? "ON" : "OFF",
             t.fan_auto ? "auto" : (t.fan_on ? "on" : "off"),
             t.alert_active ? "true" : "false", (long)net_rssi(),
             (unsigned long)(to_ms_since_boot(get_absolute_time()) / 1000),
             FW_VERSION);
    publish(topic_buf, payload_buf, 0, 1);
}

static const char *evt_name(evt_type_t t) {
    switch (t) {
    case EVT_STATE_CHANGE: return "state";
    case EVT_FAULT:        return "fault";
    case EVT_CONTRACT:     return "contract";
    case EVT_PROBE_FAIL:   return "probe_fail";
    case EVT_THROTTLE:     return "throttle";
    default:               return "?";
    }
}

void mqtt_event(const engine_evt_t *e) {
    if (!mqtt_is_connected()) return; // transient events aren't queued
    net_lock();
    snprintf(topic_buf, sizeof(topic_buf), "%s/event", base);
    snprintf(payload_buf, sizeof(payload_buf),
             "{\"port\":%u,\"event\":\"%s\",\"code\":%u,\"arg\":%lu,\"ts\":%lu}",
             e->port + 1, evt_name((evt_type_t)e->type), e->code,
             (unsigned long)e->arg, (unsigned long)net_epoch());
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
        discovery_step();
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
