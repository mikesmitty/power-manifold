#include "http.h"
#include "http_req.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>


#include "lwip/altcp.h"
#include "lwip/altcp_tcp.h"
#include "lwip/altcp_tls.h"
#include "lwip/netif.h"
#include "lwip/tcp.h" // SOF_REUSEADDR on the TCP pcb under the 443 listener
#include "mbedtls/ssl.h"
#include "pico/rand.h"

#include "boot_reason_hw.h"
#include "bus_cap.h"
#include "cli.h"
#include "engine/blade_bundle.h"
#include "fault_log.h"
#include "fault_text.h"
#include "https.h"
#include "flash_map.h"
#include "health.h"
#include "ipc.h"
#include "manifold.h"
#include "eth.h"
#include "improv.h"
#include "jsonlite.h"
#include "led_sched.h"
#include "log_ring.h"
#include "log_sink.h"
#include "mqtt.h"
#include "mqtt_tls.h"
#include "net.h"
#include "ota_pull.h"
#include "update_check.h"
#include "settings.h"
#include "settings_json.h"
#include "update.h"
#include "update_latest.h"
#include "ups/lad_proto.h"
#include "ups/ups.h"
#include "vin.h"

#define HTTP_PORT       80
#define HTTPS_PORT      443
#define MAX_CONNS       4
#define REQ_MAX         6144 // browser headers + a full settings export posted back, broker certificate included
#define STATUS_JSON_MAX 4192 // six ports with escaped labels, generation, thermometers, update progress and flags, the problem text, the UPS block, the bus voltage, the chassis light, and the update and net blocks, worst case
#define HDR_MAX         128  // the status line + our three headers
#define RESP_MAX        (STATUS_JSON_MAX + HDR_MAX)
#define POLL_INTERVAL   1    // tcp_poll units of 500ms
#define IDLE_POLLS      20   // drop a connection that sends no request in ~10s, the first or the next
#define REBOOT_DELAY_MS 300  // API reboot: let the response leave first
#define SETUP_WINDOW_MS (60 * 60 * 1000) // first-time setup, after Improv provisioning or power-up on Ethernet
#define HOST_NAMES_DOCS "https://docs.powermanifold.io/integrations/network/#host-names"
#define STR_(x) #x
#define STR(x) STR_(x)

typedef struct {
    struct altcp_pcb *pcb;
    char req[REQ_MAX];
    uint16_t req_len;
    char resp[RESP_MAX]; // headers, plus any small dynamic body
    uint16_t resp_len;
    uint16_t resp_sent;
    // Large constant bodies (the embedded page) never land in resp[]: lwIP
    // references them in flash and they stream after the headers.
    const char *static_body;
    uint16_t static_len;
    uint16_t static_sent;
    bool static_copy;   // body is a reusable RAM buffer: lwIP must copy it
    bool updating;      // headers done, body streams into update_write()
    bool tls_upload;    // headers done, body streams into the certificate buffer
    uint32_t body_left; // update or certificate body bytes still expected
    uint32_t body_got;  // certificate body bytes taken so far
    uint32_t discard;   // body bytes of a refused upload still to read before closing
    bool tls;           // came in on port 443
    struct altcp_tls_config *tls_conf; // the configuration it was accepted with
    bool keep_alive;    // another request may follow the response (http_req_keep_alive)
    bool served;        // has answered a request and is waiting for the next
    uint8_t idle_polls; // poll ticks with no request yet (browser preconnects)
} conn_t;

static conn_t conns[MAX_CONNS];
static conn_t *update_conn; // the one connection allowed to stream an update
static conn_t *tls_upload_conn; // the one connection allowed to send a certificate
// Port 443 while HTTPS is on: a plain TCP listener, each connection wrapped
// in TLS as it is accepted. lwIP's own TLS listener cannot be closed (its
// close sets a poll callback on the listening socket, which lwIP asserts
// against), and wrapping at accept time hands each connection the
// certificate installed at that moment.
static struct altcp_pcb *tls_listener;
static uint32_t refused_total; // requests refused since boot (log_refusal)

// The page itself lives in web/index.html; the build turns it into this
// literal (tools/web_page.py), with the {{...}} limits taken from the headers.
static const char INDEX_HTML[] =
#include "web_index.h"
    ;
_Static_assert(sizeof(INDEX_HTML) - 1 <= UINT16_MAX, "conn_t.static_len is 16-bit");

// Ready for the next request on the same connection
static void conn_reset_request(conn_t *c) {
    c->served = true;
    c->req_len = 0;
    c->req[0] = '\0';
    c->resp_len = 0;
    c->resp_sent = 0;
    c->static_body = NULL;
    c->static_len = 0;
    c->static_sent = 0;
    c->static_copy = false;
    c->body_left = 0;
    c->body_got = 0;
    c->discard = 0;
    c->idle_polls = 0;
}

static void conn_free(conn_t *c) {
    if (c->updating) update_abort(); // transfer died with its connection
    if (update_conn == c) update_conn = NULL;
    if (tls_upload_conn == c) tls_upload_conn = NULL;
    if (c->tls_conf) https_conn_closed(c->tls_conf);
    c->tls_conf = NULL;
    c->tls = false;
    c->tls_upload = false;
    c->keep_alive = false;
    c->served = false;
    c->body_got = 0;
    c->pcb = NULL;
    c->req_len = 0;
    c->resp_len = 0;
    c->resp_sent = 0;
    c->static_body = NULL;
    c->static_len = 0;
    c->static_sent = 0;
    c->static_copy = false;
    c->updating = false;
    c->body_left = 0;
    c->discard = 0;
    c->idle_polls = 0;
}

static void conn_close(conn_t *c) {
    if (c->pcb) {
        altcp_arg(c->pcb, NULL);
        altcp_recv(c->pcb, NULL);
        altcp_sent(c->pcb, NULL);
        altcp_poll(c->pcb, NULL, 0);
        altcp_err(c->pcb, NULL);
        if (altcp_close(c->pcb) != ERR_OK) altcp_abort(c->pcb);
    }
    conn_free(c);
}

// Queue as much of one span as the send buffer takes; true once it is all
// queued. sent_cb resumes a partial span when the window opens again.
//
// One tcp_write per segment: the CYW43 netif needs single-pbuf frames
// (LWIP_NETIF_TX_SINGLE_PBUF), so lwIP copies every write into its heap,
// flash bodies included, and a write is all-or-nothing. Offering the whole
// send window at once (eight segments, ~12 KB of a 16 KB heap) fails for
// good while MQTT holds a few KB of it; a segment at a time streams the
// page as heap frees up, the rest following from sent_cb / poll_cb.
static bool send_span(conn_t *c, const char *data, uint16_t len, uint16_t *sent,
                      uint8_t flags) {
    while (*sent < len) {
        uint16_t chunk = len - *sent;
        uint16_t room = altcp_sndbuf(c->pcb); // less the record overhead on TLS
        if (room == 0) return false;
        if (chunk > room) chunk = room;
        if (chunk > TCP_MSS) chunk = TCP_MSS;
        err_t err = altcp_write(c->pcb, data + *sent, chunk, flags);
        if (err != ERR_OK) {
            printf("http: tcp_write %d at %u/%u, retrying on poll\n", (int)err,
                   (unsigned)*sent, (unsigned)len);
            return false;
        }
        *sent += chunk;
    }
    return true;
}

// mbedTLS holds an encrypted record it could not hand to TCP yet. lwIP's
// TLS layer pushes it out from its sent and poll callbacks, so a connection
// waits for that before it closes, or the end of the response is lost.
static bool tls_unsent(conn_t *c) {
    if (!c->tls) return false;
    mbedtls_ssl_context *ssl = (mbedtls_ssl_context *)altcp_tls_context(c->pcb);
    return ssl && ssl->out_left;
}

static void send_more(conn_t *c) {
    // resp[] is reused per request, so lwIP must copy it; a static body may
    // be referenced in place where the netif allows it (see send_span)
    bool done = send_span(c, c->resp, c->resp_len, &c->resp_sent, TCP_WRITE_FLAG_COPY) &&
                send_span(c, c->static_body, c->static_len, &c->static_sent,
                          c->static_copy ? TCP_WRITE_FLAG_COPY : 0);
    altcp_output(c->pcb);
    if (!done) return;
    if (c->keep_alive) conn_reset_request(c);
    else if (c->discard) c->resp_len = 0; // recv_cb closes once the refused body is in
    else if (!tls_unsent(c)) conn_close(c); // else sent_cb or poll_cb comes back here
}

#define HDR_FMT "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %u\r\n" \
                "Connection: %s\r\n\r\n"
#define CONN_HDR(c) ((c)->keep_alive ? "keep-alive" : "close")

// Small dynamic body: headers and body together in resp[]. Bodies are the
// status JSON (STATUS_JSON_MAX) or short error/result objects, so this fits
// by construction; a body that somehow doesn't becomes a 500 rather than a
// truncated payload the client would wait on forever.
static void respond(conn_t *c, int code, const char *status,
                    const char *content_type, const char *body) {
    int n = snprintf(c->resp, RESP_MAX, HDR_FMT "%s", code, status, content_type,
                     (unsigned)strlen(body), CONN_HDR(c), body);
    if (n >= RESP_MAX) {
        n = snprintf(c->resp, RESP_MAX, HDR_FMT "%s", 500, "Internal Server Error",
                     "text/plain", 18u, CONN_HDR(c), "response too large");
    }
    c->resp_len = (uint16_t)n;
    c->resp_sent = 0;
    send_more(c);
}

// Constant body (flash-resident, any size): only the headers use resp[].
// copy: body is a RAM buffer that will be rewritten later (lwIP copies it as
// it goes, so the buffer is free once the whole body is queued); otherwise
// it lives in flash for good and lwIP may reference it in place (on the
// CYW43 netif it copies regardless, see send_span).
static void respond_static(conn_t *c, int code, const char *status,
                           const char *content_type, const char *body, size_t len,
                           bool copy) {
    int n = snprintf(c->resp, RESP_MAX, HDR_FMT, code, status, content_type,
                     (unsigned)len, CONN_HDR(c));
    c->resp_len = (uint16_t)n;
    c->resp_sent = 0;
    c->static_body = body;
    c->static_len = (uint16_t)len;
    c->static_sent = 0;
    c->static_copy = copy;
    send_more(c);
}

// "ups":{...}, — the supply's readings; just present:false without one
static void build_ups_json(char *out, size_t cap) {
    const ups_state_t *s = ups_state();
    if (!s->present) {
        snprintf(out, cap, "\"ups\":{\"present\":false},");
        return;
    }
    char fault[96];
    ups_fault_text(fault, sizeof(fault)); // fixed words, nothing to escape
    size_t off = (size_t)snprintf(out, cap,
        "\"ups\":{\"present\":true,\"ac\":%s,\"on_battery\":%s,\"charging\":%s,\"full\":%s,"
        "\"fault\":\"%s\",\"mains_v\":%.1f,\"batt_v\":%.2f,\"load_a\":%.2f,\"uvp_v\":%.2f,"
        "\"cells\":[",
        (s->status_l & LAD_ST_AC_OK) ? "true" : "false",
        (s->status_l & LAD_ST_ON_BATTERY) ? "true" : "false",
        (s->status_l & LAD_ST_CHARGING) ? "true" : "false",
        (s->status_l & LAD_ST_CHG_FULL) ? "true" : "false", fault, s->mains_dv / 10.0,
        s->batt_cv / 100.0, s->load_ca / 100.0, s->uvp_cv / 100.0);
    for (int i = 0; i < 4 && off < cap; i++) {
        if (s->cell_cv[i] == 0xFFFF)
            off += (size_t)snprintf(out + off, cap - off, "%snull", i ? "," : "");
        else
            off += (size_t)snprintf(out + off, cap - off, "%s%.2f", i ? "," : "", s->cell_cv[i] / 100.0);
    }
    if (off < cap) snprintf(out + off, cap - off, "],\"status\":\"%s\"},", ups_status_str());
}

// What the chassis light is showing, so the page can draw it: the order
// render_chassis (led_pattern.c) uses on the flags main.c sends it. Having no
// network is left out, because a browser that loaded the page has one.
static const char *chassis_light_name(void) {
    if (vin_low() || vin_high()) return "bus_fault";
    if (improv_active()) return "wifi_setup";
    if (http_setup_open(to_ms_since_boot(get_absolute_time()))) return "setup";
    return "ok";
}

// "update":{...}, — the newest release known, the last check, an install
// in progress (a pull still connecting counts), an installed image waiting
// for its restart, and what automatic installs will do
static void build_update_json(char *out, size_t cap) {
    uint32_t now_ms = to_ms_since_boot(get_absolute_time());
    uint32_t age, wait_s;
    const char *check = update_check_state(now_ms, &age);
    const char *autos = update_auto_state(now_ms, &wait_s);
    const char *latest = update_latest_version(); // x.y.z, checked when it was taken
    char latestf[UPDATE_LATEST_VERSION_MAX + 2], skipf[UPDATE_VERSION_MAX + 2];
    if (latest[0]) snprintf(latestf, sizeof(latestf), "\"%s\"", latest);
    else snprintf(latestf, sizeof(latestf), "null");
    if (g_settings.update_skip[0]) snprintf(skipf, sizeof(skipf), "\"%s\"", g_settings.update_skip);
    else snprintf(skipf, sizeof(skipf), "null");
    uint32_t epoch = net_epoch();
    uint32_t postponed = g_settings.update_postpone > epoch ? g_settings.update_postpone : 0;
    snprintf(out, cap,
             "\"update\":{\"available\":%s,\"latest\":%s,\"check\":\"%s\",\"check_age_s\":%lu,"
             "\"installing\":%s,\"progress\":%u,\"restarting\":%s,\"auto\":\"%s\",\"auto_wait_s\":%lu,"
             "\"postponed_until\":%lu,\"skipped\":%s},",
             update_latest_newer_than(FW_VERSION) ? "true" : "false", latestf, check,
             (unsigned long)age, update_active() || ota_pull_busy() ? "true" : "false",
             update_percent(), update_reboot_pending() ? "true" : "false", autos, (unsigned long)wait_s, (unsigned long)postponed, skipf);
}

// A JSON string, or null for an empty one.
static void json_str_or_null(char *out, size_t cap, const char *s) {
    if (!s || !s[0]) {
        snprintf(out, cap, "null");
        return;
    }
    out[0] = '"';
    size_t n = json_escape(out + 1, cap - 2, s);
    out[1 + n] = '"';
    out[2 + n] = '\0';
}

// "net":{...}, — what the controller is using now, as opposed to what is
// configured: the address, gateway, resolver and time server in use, whether
// the clock is set, and the broker link with what keeps a TLS link down
static void build_net_json(char *out, size_t cap) {
    // static: IRQ stack. Each address is copied out at once, because
    // net_ip_str and net_dns_str share lwIP's single static buffer.
    static char ip[20], mask[20], gw[20], dns[20], ntp[120], problem[240];
    bool up = net_up();
    json_str_or_null(ip, sizeof(ip), up ? net_ip_str() : "");
    json_str_or_null(mask, sizeof(mask), up ? net_mask_str() : "");
    json_str_or_null(gw, sizeof(gw), up ? net_gw_str() : "");
    const char *d = net_dns_str();
    json_str_or_null(dns, sizeof(dns), strcmp(d, "none") ? d : "");
    const char *t = net_ntp_str();
    json_str_or_null(ntp, sizeof(ntp), strcmp(t, "none") ? t : "");
    bool broker = g_settings.mqtt_host[0] != '\0';
    json_str_or_null(problem, sizeof(problem), mqtt_problem());
    const char *mqtt = !broker                   ? "off"
                     : mqtt_is_connected()       ? "connected"
                     : mqtt_waiting_for_clock()  ? "waiting_for_clock"
                                                 : "connecting";
    snprintf(out, cap,
             "\"net\":{\"up\":%s,\"ip\":%s,\"netmask\":%s,\"gateway\":%s,\"dns\":%s,\"ntp\":%s,"
             "\"time_synced\":%s,\"mqtt\":\"%s\",\"mqtt_problem\":%s},",
             up ? "true" : "false", ip, mask, gw, dns, ntp, net_epoch() ? "true" : "false", mqtt,
             problem);
}

static void build_status_json(char *out, size_t cap) {
    telemetry_t t;
    ipc_snapshot_read(&t);
    uint32_t headroom = t.budget_mw > t.reserved_mw ? t.budget_mw - t.reserved_mw : 0;

    static char boot_text[80]; // static: IRQ stack
    boot_reason_text(boot_reason_last(), boot_text, sizeof(boot_text));
    static char problems[192], problems_json[256];
    unsigned n_problems = health_problems(&t, problems, sizeof(problems));
    json_escape(problems_json, sizeof(problems_json), problems);
    char ethf[64] = "";
#if PWRMAN_NET_ETH
    snprintf(ethf, sizeof(ethf), "\"eth\":\"%s\",", eth_status_str());
#endif
    static char upsf[320], updf[320], netf[512]; // static: IRQ stack
    build_ups_json(upsf, sizeof(upsf));
    build_update_json(updf, sizeof(updf));
    build_net_json(netf, sizeof(netf));
    char vinf[16]; // a number, or null on a board without the divider
    if (vin_fitted()) snprintf(vinf, sizeof(vinf), "%.2f", vin_mv() / 1000.0);
    else snprintf(vinf, sizeof(vinf), "null");
    char bladef[20]; // the bundled gen-3 blade firmware, or null
    const blade_image_header_t *bh = blade_bundle_header();
    if (bh) snprintf(bladef, sizeof(bladef), "\"%u.%u.%u\"", bh->major, bh->minor, bh->patch);
    else snprintf(bladef, sizeof(bladef), "null");
    size_t off = (size_t)snprintf(out, cap,
        "{\"name\":\"%s\",\"fw\":\"%s\",\"slot\":\"%s\",\"trial\":%s,"
        "\"uptime_s\":%lu,\"rssi\":%ld,%s"
        "\"total_w\":%.2f,\"reserved_w\":%.1f,\"budget_w\":%.1f,"
        "\"headroom_w\":%.1f,\"energy_kwh\":%.3f,\"fan\":\"%s\","
        "\"fan_mode\":\"%s\",\"alert\":%s,\"improv\":\"%s\",\"boot\":\"%s\",\"warm_start\":%s,"
        "\"vin_v\":%s,\"ceiling_ma\":%lu,\"problem\":%s,\"problems\":\"%s\",\"led_mode\":\"%s\",\"led_now\":%u,"
        "\"chassis_light\":\"%s\","
        "\"blade_fw\":%s,%s%s%s\"ports\":[",
        g_settings.device_name, FW_VERSION, flash_map_slot_name(),
        flash_map_update_pending() ? "true" : "false",
        (unsigned long)(to_ms_since_boot(get_absolute_time()) / 1000),
        (long)net_rssi(), ethf, t.total_mw / 1000.0, t.reserved_mw / 1000.0,
        t.budget_mw / 1000.0, headroom / 1000.0, t.energy_mwh / 1e6,
        t.fan_on ? "on" : "off",
        t.fan_auto ? "auto" : (t.fan_on ? "on" : "off"),
        t.alert_active ? "true" : "false", improv_state_str(), boot_text,
        t.warm_start ? "true" : "false", vinf, (unsigned long)bus_cap_ma(),
        n_problems ? "true" : "false", problems_json, led_mode_name(led_sched_current()),
        led_sched_level(&g_settings, led_sched_current()), chassis_light_name(), bladef, upsf,
        updf, netf);

    for (int i = 0; i < NUM_PORTS && off < cap; i++) {
        const port_telemetry_t *p = &t.port[i];
        static char pn[PORT_NAME_MAX * 6 + 1]; // static: IRQ stack
        json_escape(pn, sizeof(pn), settings_port_name((unsigned)i));
        char tc[8], tp[8], tm[8];
        port_temp_text(tc, sizeof(tc), p->temp_conv_dc, "null");
        port_temp_text(tp, sizeof(tp), p->temp_plug_dc, "null");
        port_temp_text(tm, sizeof(tm), p->temp_mcu_dc, "null");
        off += (size_t)snprintf(out + off, cap - off,
            "%s{\"name\":\"%s\",\"state\":\"%s\",\"gen\":%u,\"attached\":%s,\"charged\":%s,\"pdo\":%u,"
            "\"v\":%.3f,\"i\":%.3f,\"p\":%.2f,\"e\":%.3f,\"contract_w\":%.1f,\"prio\":%u,"
            "\"limit_ma\":%lu,\"max_v\":%u,\"boot\":\"%s\",\"fault\":%u,"
            "\"t_conv\":%s,\"t_plug\":%s,\"t_mcu\":%s,\"progress\":%u,"
            "\"update_due\":%s,\"silent\":%s}",
            i ? "," : "", pn, port_state_name((port_state_t)p->state), p->gen,
            p->attached ? "true" : "false", p->charged ? "true" : "false", p->selected_pdo,
            p->bus_mv / 1000.0, p->current_ma / 1000.0, p->power_mw / 1000.0,
            p->energy_mwh / 1e6, p->contract_mw / 1000.0, g_settings.port_priority[i],
            (unsigned long)g_settings.port_limit_ma[i], g_settings.port_max_mv[i] / 1000,
            settings_port_boot_name(g_settings.port_boot[i]), p->fault_bits, tc, tp, tm, p->update_pct,
            p->update_due ? "true" : "false", p->silent ? "true" : "false");
    }
    if (off < cap) snprintf(out + off, cap - off, "]}");
}

// One page of the persistent fault log, newest first. Sized so a full page
// with long boot/hardfault text still fits STATUS_JSON_MAX.
#define FAULTS_PAGE 8

static void build_faults_json(char *out, size_t cap, int offset) {
    int count = fault_log_count();
    size_t off = (size_t)snprintf(out, cap,
        "{\"available\":%s,\"count\":%d,\"offset\":%d,\"faults\":[",
        fault_log_available() ? "true" : "false", count, offset);
    for (int n = 0; n < FAULTS_PAGE && off < cap; n++) {
        fault_rec_t r;
        if (!fault_log_get(offset + n, &r)) break;
        static char text[64]; // static: IRQ stack
        fault_text(&r, text, sizeof(text));
        const char *type = r.type == EVT_FAULT ? "fault"
                         : r.type == EVT_PROBE_FAIL ? "probe_fail"
                         : r.type == EVT_BOOT ? "boot"
                         : r.type == EVT_BUS ? "bus"
                         : r.type == EVT_PD_RESET ? "pd_reset" : "?";
        bool port_rec = r.type != EVT_BOOT; // boot records reuse the mW fields
        off += (size_t)snprintf(out + off, cap - off,
            "%s{\"seq\":%lu,\"epoch\":%lu,\"uptime_s\":%lu,\"port\":%u,"
            "\"type\":\"%s\",\"code\":%u,\"arg\":%lu,\"power_w\":%.1f,"
            "\"contract_w\":%.1f,\"text\":\"%s\"}",
            n ? "," : "", (unsigned long)r.seq, (unsigned long)r.epoch,
            (unsigned long)r.uptime_s, r.port == 0xFF ? 0u : r.port + 1u, type, r.code,
            (unsigned long)r.arg, port_rec ? r.power_mw / 1000.0 : 0.0,
            port_rec ? r.contract_mw / 1000.0 : 0.0, text);
    }
    if (off < cap) snprintf(out + off, cap - off, "]}");
}

// ---- Prometheus text exposition (GET /metrics) --------------------------
// Too big for a conn's resp[] (six ports of labelled gauges and thermometers,
// ~10 KB with long port names), so it is built in one shared buffer and
// streamed with the copy flag; a second scrape while one is still being
// queued gets a 503 rather than a torn buffer.
#define METRICS_MAX 12288
static char metrics_buf[METRICS_MAX];

static bool metrics_busy(void) {
    for (int i = 0; i < MAX_CONNS; i++)
        if (conns[i].pcb && conns[i].static_body == metrics_buf) return true;
    return false;
}

// GET /api/v1/log: the console ring as text, same one-at-a-time rule
static char log_buf[LOG_RING_SIZE + 1];

static bool log_busy(void) {
    for (int i = 0; i < MAX_CONNS; i++)
        if (conns[i].pcb && conns[i].static_body == log_buf) return true;
    return false;
}

// GET /api/v1/console streams the CLI's transcript buffer, which a new
// batch would overwrite: no batch starts while one is still being sent
static bool console_busy(void) {
    size_t n;
    const char *out = cli_web_output(&n);
    for (int i = 0; i < MAX_CONNS; i++)
        if (conns[i].pcb && conns[i].static_body == out) return true;
    return false;
}

// GET /api/v1/settings and its export: with a broker certificate installed
// the object outgrows resp[], so it streams from here the same way
static char settings_buf[SETTINGS_JSON_MAX];

static bool settings_busy(void) {
    for (int i = 0; i < MAX_CONNS; i++)
        if (conns[i].pcb && conns[i].static_body == settings_buf) return true;
    return false;
}

// Prometheus label value: backslash, quote and newline are escaped
static void prom_label(char *out, size_t cap, const char *in) {
    size_t n = 0;
    for (; *in && n + 2 < cap; in++) {
        if (*in == '\\' || *in == '"') out[n++] = '\\';
        if (*in == '\n') { out[n++] = '\\'; out[n++] = 'n'; continue; }
        out[n++] = *in;
    }
    out[n] = '\0';
}

#define M_PUT(...) do { \
        if (off < cap) off += (size_t)snprintf(out + off, cap - off, __VA_ARGS__); \
    } while (0)

// Returns the body length, or 0 if it did not fit.
static size_t build_metrics(char *out, size_t cap) {
    telemetry_t t;
    ipc_snapshot_read(&t);
    uint32_t headroom = t.budget_mw > t.reserved_mw ? t.budget_mw - t.reserved_mw : 0;
    static char boot_text[80], lbl[PORT_NAME_MAX * 2 + 1]; // static: IRQ stack
    boot_reason_text(boot_reason_last(), boot_text, sizeof(boot_text));
    size_t off = 0;

    M_PUT("# TYPE pwrman_info gauge\n"
          "pwrman_info{name=\"%s\",fw=\"%s\",slot=\"%s\",boot=\"%s\"} 1\n",
          g_settings.device_name, FW_VERSION, flash_map_slot_name(), boot_text);
    M_PUT("# TYPE pwrman_uptime_seconds gauge\npwrman_uptime_seconds %lu\n",
          (unsigned long)(to_ms_since_boot(get_absolute_time()) / 1000));
    M_PUT("# TYPE pwrman_wifi_rssi_dbm gauge\npwrman_wifi_rssi_dbm %ld\n", (long)net_rssi());
    M_PUT("# TYPE pwrman_mqtt_connected gauge\npwrman_mqtt_connected %d\n", mqtt_is_connected() ? 1 : 0);
    M_PUT("# TYPE pwrman_total_power_watts gauge\npwrman_total_power_watts %.2f\n", t.total_mw / 1000.0);
    M_PUT("# TYPE pwrman_reserved_power_watts gauge\npwrman_reserved_power_watts %.1f\n", t.reserved_mw / 1000.0);
    M_PUT("# TYPE pwrman_budget_watts gauge\npwrman_budget_watts %.1f\n", t.budget_mw / 1000.0);
    M_PUT("# TYPE pwrman_headroom_watts gauge\npwrman_headroom_watts %.1f\n", headroom / 1000.0);
    M_PUT("# TYPE pwrman_energy_kwh_total counter\npwrman_energy_kwh_total %.6f\n", t.energy_mwh / 1e6);
    M_PUT("# TYPE pwrman_fan_on gauge\npwrman_fan_on %d\n", t.fan_on ? 1 : 0);
    M_PUT("# TYPE pwrman_fan_auto gauge\npwrman_fan_auto %d\n", t.fan_auto ? 1 : 0);
    M_PUT("# TYPE pwrman_alert_active gauge\npwrman_alert_active %d\n", t.alert_active ? 1 : 0);
    M_PUT("# TYPE pwrman_fault_log_records gauge\npwrman_fault_log_records %d\n", fault_log_count());
    M_PUT("# HELP pwrman_http_refused_total Web requests refused: no or wrong token, unknown host name, setup rules\n"
          "# TYPE pwrman_http_refused_total counter\npwrman_http_refused_total %lu\n", (unsigned long)refused_total);
    if (https_expires())
        M_PUT("# HELP pwrman_https_certificate_expiry_seconds When the web server's certificate expires\n"
              "# TYPE pwrman_https_certificate_expiry_seconds gauge\npwrman_https_certificate_expiry_seconds %lu\n",
              (unsigned long)https_expires());
    if (vin_fitted()) {
        M_PUT("# TYPE pwrman_bus_volts gauge\npwrman_bus_volts %.2f\n", vin_mv() / 1000.0);
        M_PUT("# TYPE pwrman_bus_voltage_ok gauge\npwrman_bus_voltage_ok %d\n",
              (vin_low() || vin_high()) ? 0 : 1);
    }
    const ups_state_t *u = ups_state();
    M_PUT("# TYPE pwrman_ups_present gauge\npwrman_ups_present %d\n", u->present ? 1 : 0);
    if (u->present) {
        M_PUT("# TYPE pwrman_ups_ac_ok gauge\npwrman_ups_ac_ok %d\n", (u->status_l & LAD_ST_AC_OK) ? 1 : 0);
        M_PUT("# TYPE pwrman_ups_on_battery gauge\npwrman_ups_on_battery %d\n", (u->status_l & LAD_ST_ON_BATTERY) ? 1 : 0);
        M_PUT("# TYPE pwrman_ups_charging gauge\npwrman_ups_charging %d\n", (u->status_l & LAD_ST_CHARGING) ? 1 : 0);
        M_PUT("# TYPE pwrman_ups_battery_full gauge\npwrman_ups_battery_full %d\n", (u->status_l & LAD_ST_CHG_FULL) ? 1 : 0);
        M_PUT("# TYPE pwrman_ups_fault gauge\npwrman_ups_fault %d\n", ups_fault() ? 1 : 0);
        M_PUT("# TYPE pwrman_ups_mains_volts gauge\npwrman_ups_mains_volts %.1f\n", u->mains_dv / 10.0);
        M_PUT("# TYPE pwrman_ups_battery_volts gauge\npwrman_ups_battery_volts %.2f\n", u->batt_cv / 100.0);
        M_PUT("# TYPE pwrman_ups_load_amps gauge\npwrman_ups_load_amps %.2f\n", u->load_ca / 100.0);
        M_PUT("# TYPE pwrman_ups_status_flags gauge\npwrman_ups_status_flags %u\n", u->status_l);
    }

    // per port: one line per metric per port, ports labelled by number and name
    static const struct { const char *name, *type; } PM[] = {
        {"pwrman_port_voltage_volts", "gauge"},   {"pwrman_port_current_amps", "gauge"},
        {"pwrman_port_power_watts", "gauge"},     {"pwrman_port_energy_kwh_total", "counter"},
        {"pwrman_port_contract_watts", "gauge"},  {"pwrman_port_priority", "gauge"},
        {"pwrman_port_attached", "gauge"},        {"pwrman_port_pdo", "gauge"},
        {"pwrman_port_fault_bits", "gauge"},      {"pwrman_port_limit_amps", "gauge"},
        {"pwrman_port_max_volts", "gauge"},       {"pwrman_port_charged", "gauge"},
        {"pwrman_port_state_info", "gauge"},
    };
    for (size_t m = 0; m < sizeof(PM) / sizeof(PM[0]); m++) {
        M_PUT("# TYPE %s %s\n", PM[m].name, PM[m].type);
        for (int i = 0; i < NUM_PORTS; i++) {
            const port_telemetry_t *p = &t.port[i];
            prom_label(lbl, sizeof(lbl), settings_port_name((unsigned)i));
            M_PUT("%s{port=\"%d\",name=\"%s\"", PM[m].name, i + 1, lbl);
            switch (m) {
            case 0: M_PUT("} %.3f\n", p->bus_mv / 1000.0); break;
            case 1: M_PUT("} %.3f\n", p->current_ma / 1000.0); break;
            case 2: M_PUT("} %.2f\n", p->power_mw / 1000.0); break;
            case 3: M_PUT("} %.6f\n", p->energy_mwh / 1e6); break;
            case 4: M_PUT("} %.1f\n", p->contract_mw / 1000.0); break;
            case 5: M_PUT("} %u\n", g_settings.port_priority[i]); break;
            case 6: M_PUT("} %d\n", p->attached ? 1 : 0); break;
            case 7: M_PUT("} %u\n", p->selected_pdo); break;
            case 8: M_PUT("} %u\n", p->fault_bits); break;
            case 9: M_PUT("} %.2f\n", g_settings.port_limit_ma[i] / 1000.0); break;
            case 10: M_PUT("} %u\n", g_settings.port_max_mv[i] / 1000); break;
            case 11: M_PUT("} %d\n", p->charged ? 1 : 0); break;
            default: M_PUT(",state=\"%s\"} 1\n", port_state_name((port_state_t)p->state)); break;
            }
        }
    }
    // a gen-3 blade's thermometers: one series per sensor, absent without a reading
    M_PUT("# TYPE pwrman_port_temperature_celsius gauge\n");
    for (int i = 0; i < NUM_PORTS; i++) {
        const port_telemetry_t *p = &t.port[i];
        const struct { const char *sensor; int16_t dc; } TS[] = {
            {"converter", p->temp_conv_dc}, {"plug", p->temp_plug_dc}, {"mcu", p->temp_mcu_dc},
        };
        prom_label(lbl, sizeof(lbl), settings_port_name((unsigned)i));
        for (size_t k = 0; k < sizeof(TS) / sizeof(TS[0]); k++) {
            if (TS[k].dc == PORT_TEMP_NONE) continue;
            M_PUT("pwrman_port_temperature_celsius{port=\"%d\",name=\"%s\",sensor=\"%s\"} %.1f\n",
                  i + 1, lbl, TS[k].sensor, TS[k].dc / 10.0);
        }
    }
    return off < cap ? off : 0;
}

static bool bearer_present(const conn_t *c, const char *token) {
    return http_req_bearer_ok(c->req, token);
}

// Every change over the network, the console log, the web console and the
// OTA push need the API token. A controller with no token stored refuses
// them all; the only thing it accepts is the settings save that sets the
// first token, through one of the setup doors below.
static bool authorized(const conn_t *c) {
    return g_settings.api_token[0] && bearer_present(c, g_settings.api_token);
}

// A refused request is a possible attempt at unauthorized access: one line
// on the console and syslog says who asked for what, rate-limited
// (http_req.h), and the metrics count every one.
static http_refusal_limit_t refusal_limit;

static void log_refusal(const conn_t *c, int code, const char *why) {
    refused_total++;
    unsigned unlogged;
    if (!http_refusal_log_ok(&refusal_limit, to_ms_since_boot(get_absolute_time()), &unlogged)) return;
    char line[48], from[IPADDR_STRLEN_MAX];
    http_req_line_text(c->req, line, sizeof(line));
    if (!c->pcb || !ipaddr_ntoa_r(altcp_get_ip(c->pcb, 0), from, sizeof(from))) strcpy(from, "?");
    if (unlogged) printf("http: %u more refused requests not shown\n", unlogged);
    printf("http: refused %s from %s (%d, %s)\n", line, from, code, why);
}

// No credential at all, or a wrong one.
static const char *credential_why(const conn_t *c) {
    static char v[256]; // static: lwIP callback stack; lwIP calls in one at a time
    return http_req_header(c->req, "Authorization", v, sizeof(v)) ? "wrong token" : "no token";
}

static void respond_unauthorized(conn_t *c) {
    log_refusal(c, 401, g_settings.api_token[0] ? credential_why(c) : "no API token set yet");
    respond(c, 401, "Unauthorized", "application/json",
            g_settings.api_token[0]
                ? "{\"error\":\"bearer token required\"}"
                : "{\"error\":\"no API token set yet: finish first-time setup first\"}");
}

// ---- First-time setup: how the first API token gets in without a serial
// cable. Two windows, both open only while no token is stored, both closed by
// the settings save that sets one:
//   * Over Wi-Fi, a successful Improv provisioning opens the settings to any
//     request for SETUP_WINDOW_MS. Being in Bluetooth range of the
//     chassis stands in for the token, whichever app did the provisioning
//     (the Home Assistant app's own Improv setup does not open the link the
//     controller hands back).
//   * Over Ethernet, a request arriving on the wired link's address is let in
//     for SETUP_WINDOW_MS after power-up. A short press of the front-panel
//     button restarts that hour (main.c), so a missed window costs a power
//     cycle or a press, not a factory reset.
// Either stands in for the token on /settings only, and a request let in this
// way must set a token (settings_json_apply). The Wi-Fi timer is written from
// improv_poll under the network lock and the Ethernet one from the main loop;
// both are read here in lwIP's context.

static uint32_t wifi_window_until_ms; // 0 = closed
static uint32_t eth_window_until_ms;  // 0 = closed

void http_setup_wifi_open(uint32_t now_ms) {
    wifi_window_until_ms = now_ms + SETUP_WINDOW_MS;
    if (!wifi_window_until_ms) wifi_window_until_ms = 1;
}

void http_setup_window_restart(uint32_t now_ms) {
    eth_window_until_ms = now_ms + SETUP_WINDOW_MS;
    if (!eth_window_until_ms) eth_window_until_ms = 1;
}

static bool wifi_window_live(uint32_t now_ms) {
    if (!wifi_window_until_ms || g_settings.api_token[0]) return false;
    return (int32_t)(now_ms - wifi_window_until_ms) < 0;
}

static bool eth_window_live(uint32_t now_ms) {
    if (!eth_window_until_ms || g_settings.api_token[0]) return false;
    return (int32_t)(now_ms - eth_window_until_ms) < 0;
}

// The request came in on the wired link's own address.
static bool request_on_wired(const conn_t *c) {
#if PWRMAN_NET_ETH
    if (!eth_up() || !c->pcb) return false;
    return ip4_addr_eq(ip_2_ip4(altcp_get_ip(c->pcb, 1)), netif_ip4_addr(eth_netif_ptr()));
#else
    (void)c;
    return false;
#endif
}

bool http_setup_open(uint32_t now_ms) {
    return wifi_window_live(now_ms) || (eth_window_live(now_ms) && eth_up());
}

// /settings always needs a bearer, or an open setup door.
static bool settings_authorized(const conn_t *c, bool *via_setup) {
    *via_setup = false;
    if (g_settings.api_token[0]) return bearer_present(c, g_settings.api_token);
    uint32_t now = to_ms_since_boot(get_absolute_time());
    if (wifi_window_live(now) || (eth_window_live(now) && request_on_wired(c))) {
        *via_setup = true;
        return true;
    }
    return false;
}

// Why /settings said no, in words the page shows as they are.
static void respond_settings_locked(conn_t *c) {
    log_refusal(c, 401, g_settings.api_token[0] ? credential_why(c) : "setup closed");
    const char *why;
    if (g_settings.api_token[0])
        why = "bearer token required";
    else if (request_on_wired(c))
        why = "setup window closed: power-cycle the controller or press its button once, "
              "then set an API token within an hour";
    else
        why = "no API token yet: Wi-Fi setup unlocks settings for an hour, "
              "or connect Ethernet";
    char b[192];
    snprintf(b, sizeof(b), "{\"error\":\"%s\"}", why);
    respond(c, 401, "Unauthorized", "application/json", b);
}

// Every request must name the controller in Host: its address, its device
// name, name.local, or one of the owner's hostnames. A web page on another
// site that points its own domain at the controller's address (DNS
// rebinding) sends that domain instead, so it can neither read the status
// pages nor use a setup door (see http_req.h).
static const char WRONG_HOST_HTML[] =
    "<!doctype html><meta charset=utf-8><meta name=viewport content=\"width=device-width\">"
    "<title>Not this name</title><p>This controller does not answer to this name.</p>"
    "<p>Open it by its IP address, then add the name under Settings, Network, Host names. "
    "See <a href=\"" HOST_NAMES_DOCS "\">" HOST_NAMES_DOCS "</a>.</p>\n";

static bool host_allowed(const conn_t *c) {
    char ip[IP4ADDR_STRLEN_MAX] = "";
    if (c->pcb) ip4addr_ntoa_r(ip_2_ip4(altcp_get_ip(c->pcb, 1)), ip, sizeof(ip));
    return http_req_host_ok(c->req, c->tls ? HTTPS_PORT : HTTP_PORT, ip, g_settings.device_name,
                            g_settings.hostnames, https_names());
}

// A request let in by a setup door carries no credential, so a POST must
// also declare a JSON body, which a web page on another site cannot send
// here. True once refused and answered.
static bool setup_request_refused(conn_t *c) {
    if (c->req[0] == 'P' && !http_req_is_json(c->req)) {
        log_refusal(c, 415, "setup request not sent as JSON");
        respond(c, 415, "Unsupported Media Type", "application/json",
                "{\"error\":\"settings must be sent as application/json\"}");
        return true;
    }
    return false;
}

static uint32_t reboot_at_ms; // 0 = none requested

bool http_reboot_due(uint32_t now_ms) {
    return reboot_at_ms && (int32_t)(now_ms - reboot_at_ms) >= 0;
}

// ---- Settings: the console's mqtt/name/token/budget/fan commands in one
// place (WiFi excepted: that is Improv's job over BLE). Budget and fan apply
// live; name and broker take a reboot.

static void build_settings_json(char *out, size_t cap, bool via_setup, bool secrets,
                                bool export) {
    telemetry_t t; // manual fan state is the engine's, not a setting
    ipc_snapshot_read(&t);
    settings_json_opts_t o = {.fan_on = t.fan_on, .setup = via_setup, .secrets = secrets,
                              .export = export};
    if (!settings_json_build(out, cap, &g_settings, &o))
        snprintf(out, cap, "{\"error\":\"settings do not fit the response\"}");
}

// Any subset of the fields; absent ones keep their value. The whole record
// is validated into a copy first (settings_json_apply) so a bad field
// changes nothing. An export posted back is the import path.
static void settings_post(conn_t *c, const char *body, bool via_setup) {
    static settings_t s; // static: a few hundred bytes, IRQ stack
    s = g_settings;
    settings_apply_t ap;
    const char *err = settings_json_apply(body, &s, via_setup, &ap);
    if (err) {
        char b[128];
        snprintf(b, sizeof(b), "{\"error\":\"%s\"}", err);
        respond(c, 400, "Bad Request", "application/json", b);
        return;
    }

    if (s.https && !https_config()) {
        respond(c, 400, "Bad Request", "application/json",
                "{\"error\":\"https: install a certificate first (POST /api/v1/tls)\"}");
        return;
    }

    bool reboot_required = strcmp(g_settings.device_name, s.device_name) != 0 ||
                           strcmp(g_settings.wifi_ssid, s.wifi_ssid) != 0 ||
                           strcmp(g_settings.wifi_pass, s.wifi_pass) != 0 ||
                           strcmp(g_settings.mqtt_host, s.mqtt_host) != 0 ||
                           g_settings.mqtt_port != s.mqtt_port ||
                           strcmp(g_settings.mqtt_user, s.mqtt_user) != 0 ||
                           strcmp(g_settings.mqtt_pass, s.mqtt_pass) != 0 ||
                           g_settings.ip_static != s.ip_static ||
                           g_settings.ip_addr != s.ip_addr || g_settings.ip_mask != s.ip_mask ||
                           g_settings.ip_gw != s.ip_gw; // dns, ntp and syslog apply live
    bool budget_changed = g_settings.budget_mw != s.budget_mw;
    bool led_changed = g_settings.led_brightness != s.led_brightness;
    bool names_changed = memcmp(g_settings.port_name, s.port_name, sizeof(s.port_name)) != 0;
    bool https_changed = g_settings.https != s.https;
    bool link_changed = g_settings.mqtt_tls != s.mqtt_tls || g_settings.mqtt_ca_len != s.mqtt_ca_len ||
                        memcmp(g_settings.mqtt_ca, s.mqtt_ca, sizeof(s.mqtt_ca)) != 0;
    uint32_t old_limit[NUM_PORTS];
    uint16_t old_volt[NUM_PORTS];
    memcpy(old_limit, g_settings.port_limit_ma, sizeof(old_limit));
    memcpy(old_volt, g_settings.port_max_mv, sizeof(old_volt));
    g_settings = s;

    for (uint8_t i = 0; i < NUM_PORTS; i++) {
        if (old_limit[i] != s.port_limit_ma[i]) {
            engine_cmd_t cmd = {.op = CMD_PORT_LIMIT, .port = i, .arg = s.port_limit_ma[i]};
            ipc_cmd_push(&cmd);
        }
        if (old_volt[i] != s.port_max_mv[i]) {
            engine_cmd_t cmd = {.op = CMD_PORT_VOLT, .port = i, .arg = s.port_max_mv[i]};
            ipc_cmd_push(&cmd);
        }
    }
    if (names_changed) mqtt_names_changed();
    if (link_changed) mqtt_reconnect();
    if (https_changed) http_tls_sync(); // this response still goes out on its own connection

    if (budget_changed) {
        engine_cmd_t cmd = {.op = CMD_SET_BUDGET, .arg = s.budget_mw};
        ipc_cmd_push(&cmd);
    }
    if (led_changed) {
        engine_cmd_t cmd = {.op = CMD_LED_BRIGHTNESS, .arg = s.led_brightness};
        ipc_cmd_push(&cmd);
    }
    if (ap.fan_mode_given) { // an explicit mode: apply it (the CLI's fan on|off|auto)
        engine_cmd_t cmd = s.fan_auto ? (engine_cmd_t){.op = CMD_FAN_AUTO}
                                      : (engine_cmd_t){.op = CMD_FAN, .arg = ap.fan_manual_on};
        ipc_cmd_push(&cmd);
    } else if (ap.fan_thresholds && s.fan_auto) { // new thresholds under auto: re-arm
        engine_cmd_t cmd = {.op = CMD_FAN_AUTO};
        ipc_cmd_push(&cmd);
    }

    bool saved = settings_save(); // flash_safe_execute, as the OTA path does from here
    wifi_window_until_ms = eth_window_until_ms = 0; // a token now exists (or the caller had one)
    printf("settings: %s via web%s\n", saved ? "saved" : "save FAILED",
           via_setup ? " (first-time setup)" : "");
    if (saved) {
        char b[64];
        snprintf(b, sizeof(b), "{\"ok\":true,\"reboot_required\":%s}",
                 reboot_required ? "true" : "false");
        respond(c, 200, "OK", "application/json", b);
    } else {
        respond(c, 500, "Internal Server Error", "application/json",
                "{\"error\":\"flash save failed\"}");
    }
}

static long content_length(const char *req) {
    // scan header lines only; the header always precedes any body bytes that
    // may already sit (binary, but NUL-terminated) in req[]
    for (const char *p = req; (p = strchr(p, '\n')) != NULL;) {
        p++;
        if (!strncasecmp(p, "Content-Length:", 15)) return strtol(p + 15, NULL, 10);
    }
    return -1;
}

// ---- OTA upload: POST /api/v1/update, body = firmware image (the signed
// .bin; an unsigned bin or uf2 only in a build without signing keys).
// The body streams straight into update_write(); nothing except the request
// headers ever lands in req[].

static void update_fail(conn_t *c, int code, const char *status, const char *msg) {
    char body[160];
    c->updating = false;
    c->keep_alive = false;
    if (update_conn == c) update_conn = NULL;
    update_abort();
    snprintf(body, sizeof(body), "{\"error\":\"%s\"}", msg);
    respond(c, code, status, "application/json", body);
}

static void update_complete(conn_t *c) {
    char err[96], body[192];
    c->updating = false;
    c->keep_alive = false; // it restarts into the new image
    if (update_conn == c) update_conn = NULL;
    if (!update_finish(err, sizeof(err))) {
        update_fail(c, 422, "Unprocessable Entity", err);
        return;
    }
    update_schedule_reboot(1000);
    snprintf(body, sizeof(body),
             "{\"ok\":true,\"slot\":\"%s\",\"bytes\":%lu,\"version\":\"%s\","
             "\"action\":\"trial reboot in 1s\"}",
             update_slot_name(), (unsigned long)update_bytes(), update_version_str());
    respond(c, 200, "OK", "application/json", body);
    printf("update: %lu bytes -> slot %s (v%s); trial reboot scheduled\n",
           (unsigned long)update_bytes(), update_slot_name(), update_version_str());
}

static void update_feed_bytes(conn_t *c, const uint8_t *d, uint32_t n) {
    if (!c->updating || n == 0) return;
    if (n > c->body_left) n = c->body_left; // ignore trailing junk
    char err[96];
    if (!update_write(d, n, err, sizeof(err))) {
        update_fail(c, 422, "Unprocessable Entity", err);
        return;
    }
    c->body_left -= n;
    if (c->body_left == 0) update_complete(c);
}

static void update_feed(conn_t *c, struct pbuf *p, uint16_t skip) {
    for (struct pbuf *q = p; q && c->updating; q = q->next) {
        if (skip >= q->len) {
            skip -= q->len;
            continue;
        }
        update_feed_bytes(c, (const uint8_t *)q->payload + skip, (uint32_t)(q->len - skip));
        skip = 0;
    }
}

// A refused upload is answered at once. Closing while its body still
// arrives makes the client's TCP stack reset the connection, often before it
// has read the answer, so the rest of the body is read and dropped first.
// extra: body bytes received past what req[] holds.
static void discard_body(conn_t *c, const char *body_start, uint32_t extra) {
    long cl = content_length(c->req);
    long have = (long)(c->req_len - (uint16_t)(body_start - c->req)) + (long)extra;
    c->discard = cl > have ? (uint32_t)(cl - have) : 0;
}

static void update_post_start(conn_t *c, const char *body_start) {
    c->keep_alive = false; // a refusal leaves the body unread
    if (!authorized(c)) {
        respond_unauthorized(c);
        return;
    }
    if (strstr(c->req, "Transfer-Encoding")) {
        respond(c, 400, "Bad Request", "application/json",
                "{\"error\":\"chunked bodies unsupported; send Content-Length\"}");
        return;
    }
    long cl = content_length(c->req);
    if (cl <= 0) {
        respond(c, 411, "Length Required", "application/json",
                "{\"error\":\"Content-Length required\"}");
        return;
    }

    char err[96], body[160];
    if (!update_begin((uint32_t)cl, 0, err, sizeof(err))) {
        // no update_fail(): a refusal must not abort a transfer that another
        // connection legitimately still owns
        snprintf(body, sizeof(body), "{\"error\":\"%s\"}", err);
        respond(c, 409, "Conflict", "application/json", body);
        return;
    }
    if (update_conn && update_conn != c) {
        // update_begin only lets a new transfer through when the old one has
        // gone stale, so its parked connection can be dropped outright — with
        // the flag cleared first, or its teardown would abort OUR session
        update_conn->updating = false;
        conn_close(update_conn);
    }
    update_conn = c;
    c->updating = true;
    c->body_left = (uint32_t)cl;
    printf("update: receiving %ld bytes into slot %s\n", cl, update_slot_name());

    // body bytes that arrived with the headers
    update_feed_bytes(c, (const uint8_t *)body_start,
                      (uint32_t)(c->req_len - (uint16_t)(body_start - c->req)));
}

// ---- Certificate upload: POST /api/v1/tls, body = PEM text of the private
// key and the certificate chain (net/https.h). Like the update, the body
// streams into its own buffer rather than req[], and one upload runs at a
// time.

static void tls_upload_bytes(conn_t *c, const uint8_t *d, uint32_t n) {
    if (!c->tls_upload || n == 0) return;
    if (n > c->body_left) n = c->body_left;
    memcpy(https_upload_buffer() + c->body_got, d, n);
    c->body_got += n;
    c->body_left -= n;
    if (c->body_left) return;

    c->tls_upload = false;
    tls_upload_conn = NULL;
    bool fault;
    const char *err = https_install(c->body_got, &fault);
    if (err) {
        static char b[192]; // static: lwIP callback stack
        char e[160];
        json_escape(e, sizeof(e), err);
        snprintf(b, sizeof(b), "{\"error\":\"%s\"}", e);
        respond(c, fault ? 500 : 400, fault ? "Internal Server Error" : "Bad Request", "application/json", b);
        return;
    }
    http_tls_sync(); // opens 443 if HTTPS was on, waiting for a certificate
    static char b[512];
    https_json(b, sizeof(b), net_epoch());
    respond(c, 200, "OK", "application/json", b);
}

static void tls_upload_feed(conn_t *c, struct pbuf *p, uint16_t skip) {
    for (struct pbuf *q = p; q && c->tls_upload; q = q->next) {
        if (skip >= q->len) {
            skip -= q->len;
            continue;
        }
        tls_upload_bytes(c, (const uint8_t *)q->payload + skip, (uint32_t)(q->len - skip));
        skip = 0;
    }
}

static void tls_post_start(conn_t *c, const char *body_start) {
    c->keep_alive = false; // a refusal leaves the body unread
    if (!authorized(c)) {
        respond_unauthorized(c);
        return;
    }
    long cl = content_length(c->req);
    if (cl <= 0) {
        respond(c, 411, "Length Required", "application/json", "{\"error\":\"Content-Length required\"}");
        return;
    }
    if (cl >= HTTPS_UPLOAD_MAX) {
        respond(c, 413, "Payload Too Large", "application/json",
                "{\"error\":\"send under " STR(HTTPS_UPLOAD_MAX) " bytes: the key and the certificate chain\"}");
        return;
    }
    if (tls_upload_conn && tls_upload_conn != c) {
        respond(c, 409, "Conflict", "application/json", "{\"error\":\"another certificate upload is running\"}");
        return;
    }
    tls_upload_conn = c;
    c->tls_upload = true;
    c->body_left = (uint32_t)cl;
    c->body_got = 0;
    tls_upload_bytes(c, (const uint8_t *)body_start,
                     (uint32_t)(c->req_len - (uint16_t)(body_start - c->req)));
}

static void handle_request(conn_t *c) {
    // static: lwIP calls this in IRQ context on core 0's 4KB stack, and the
    // engine's stack sits directly below it — a 1.6KB frame here plus printf's
    // float formatting was enough to overflow into it under concurrent load
    static char json[STATUS_JSON_MAX];

    if (!strncmp(c->req, "GET /", 5) && (c->req[5] == ' ' || c->req[5] == '?')) {
        // a query string (an older Improv redirect, a bookmark) gets the page too
        respond_static(c, 200, "OK", "text/html", INDEX_HTML, sizeof(INDEX_HTML) - 1, false);
    } else if (!strncmp(c->req, "GET /api/v1/status", 18)) {
        build_status_json(json, sizeof(json));
        respond(c, 200, "OK", "application/json", json);
    } else if (!strncmp(c->req, "GET /metrics", 12)) {
        if (metrics_busy()) {
            respond(c, 503, "Service Unavailable", "text/plain", "scrape in progress");
            return;
        }
        size_t len = build_metrics(metrics_buf, sizeof(metrics_buf));
        if (!len) {
            respond(c, 500, "Internal Server Error", "text/plain", "metrics too large");
            return;
        }
        respond_static(c, 200, "OK", "text/plain; version=0.0.4; charset=utf-8",
                       metrics_buf, len, true);
    } else if (!strncmp(c->req, "GET /api/v1/log", 15)) {
        if (!authorized(c)) {
            respond_unauthorized(c);
        } else if (log_busy()) {
            respond(c, 503, "Service Unavailable", "text/plain", "log busy, retry\n");
        } else {
            size_t n = log_sink_snapshot(log_buf, sizeof(log_buf));
            respond_static(c, 200, "OK", "text/plain; charset=utf-8", log_buf, n, true);
        }
    } else if (!strncmp(c->req, "GET /api/v1/console", 19)) {
        // 202 while the batch runs; then its transcript (empty before the first)
        if (!authorized(c)) {
            respond_unauthorized(c);
            return;
        }
        cli_web_state_t st = cli_web_state();
        size_t n;
        const char *out = cli_web_output(&n);
        if (st == CLI_WEB_QUEUED || st == CLI_WEB_RUNNING)
            respond(c, 202, "Accepted", "text/plain", "");
        else if (st == CLI_WEB_IDLE || !n)
            respond(c, 200, "OK", "text/plain", "");
        else
            respond_static(c, 200, "OK", "text/plain; charset=utf-8", out, n, true);
    } else if (!strncmp(c->req, "GET /api/v1/tls", 15) && (c->req[15] == ' ' || c->req[15] == '?')) {
        https_json(json, sizeof(json), net_epoch());
        respond(c, 200, "OK", "application/json", json);
    } else if (!strncmp(c->req, "GET /api/v1/faults", 18)) {
        int offset = 0;
        const char *q = strstr(c->req, "?offset=");
        if (q) offset = atoi(q + 8);
        build_faults_json(json, sizeof(json), offset < 0 ? 0 : offset);
        respond(c, 200, "OK", "application/json", json);
    } else if (!strncmp(c->req, "GET /api/v1/settings/export", 27)) {
        bool via_setup;
        if (!settings_authorized(c, &via_setup)) {
            respond_settings_locked(c);
            return;
        }
        if (via_setup && setup_request_refused(c)) return;
        bool secrets = !strncmp(c->req + 27, "?secrets=1 ", 11);
        if (secrets && via_setup) { // the setup doors never give out stored passwords
            log_refusal(c, 401, "export with secrets through setup");
            respond(c, 401, "Unauthorized", "application/json",
                    "{\"error\":\"the export with secrets needs the API token\"}");
            return;
        }
        if (settings_busy()) {
            respond(c, 503, "Service Unavailable", "application/json", "{\"error\":\"busy, retry\"}");
            return;
        }
        build_settings_json(settings_buf, sizeof(settings_buf), via_setup, secrets, true);
        respond_static(c, 200, "OK", "application/json", settings_buf, strlen(settings_buf), true);
    } else if (!strncmp(c->req, "GET /api/v1/settings", 20) ||
               !strncmp(c->req, "POST /api/v1/settings", 21)) {
        bool via_setup;
        if (!settings_authorized(c, &via_setup)) {
            respond_settings_locked(c);
            return;
        }
        if (via_setup && setup_request_refused(c)) return;
        if (c->req[0] == 'G') {
            if (settings_busy()) {
                respond(c, 503, "Service Unavailable", "application/json", "{\"error\":\"busy, retry\"}");
                return;
            }
            build_settings_json(settings_buf, sizeof(settings_buf), via_setup, false, false);
            respond_static(c, 200, "OK", "application/json", settings_buf, strlen(settings_buf), true);
        } else {
            const char *body = strstr(c->req, "\r\n\r\n");
            settings_post(c, body ? body + 4 : "", via_setup);
        }
    } else if (!strncmp(c->req, "POST /api/v1/", 13)) {
        if (!authorized(c)) {
            respond_unauthorized(c);
            return;
        }
        const char *body = strstr(c->req, "\r\n\r\n");
        body = body ? body + 4 : "";
        unsigned port;
        if (sscanf(c->req, "POST /api/v1/port/%u", &port) == 1 &&
            port >= 1 && port <= NUM_PORTS) {
            engine_cmd_t cmd = {.port = (uint8_t)(port - 1)};
            if (strstr(body, "\"enable\"")) cmd.op = CMD_PORT_ENABLE;
            else if (strstr(body, "\"disable\"")) cmd.op = CMD_PORT_DISABLE;
            else if (strstr(body, "\"hard_reset\"")) cmd.op = CMD_PORT_HARD_RESET;
            else if (strstr(body, "\"src_cap\"")) cmd.op = CMD_PORT_SRC_CAP;
            else if (strstr(body, "\"update\"")) cmd.op = CMD_PORT_UPDATE;
            else {
                respond(c, 400, "Bad Request", "application/json",
                        "{\"error\":\"unknown action\"}");
                return;
            }
            bool queued = ipc_cmd_push(&cmd);
            if (queued && (cmd.op == CMD_PORT_ENABLE || cmd.op == CMD_PORT_DISABLE) &&
                settings_port_admin_note(port - 1, cmd.op == CMD_PORT_ENABLE))
                settings_save_later(); // the "last" boot policy keeps it
            if (queued) respond(c, 200, "OK", "application/json", "{\"ok\":true}");
            else respond(c, 503, "Service Unavailable", "application/json", "{\"error\":\"busy, retry\"}");
        } else if (!strncmp(c->req, "POST /api/v1/console", 20)) {
            // command lines as text, one per line; the main loop runs them
            size_t n = strlen(body);
            long cl = content_length(c->req);
            if (cl > 0 && (size_t)cl != n) { // cut short by req[]: never run part of a batch
                respond(c, 413, "Payload Too Large", "application/json",
                        "{\"error\":\"send under " STR(CLI_WEB_IN_MAX) " bytes of commands at a time\"}");
            } else if (!n) {
                respond(c, 400, "Bad Request", "application/json", "{\"error\":\"no command\"}");
            } else if (n >= CLI_WEB_IN_MAX) {
                respond(c, 413, "Payload Too Large", "application/json",
                        "{\"error\":\"send under " STR(CLI_WEB_IN_MAX) " bytes of commands at a time\"}");
            } else if (console_busy() || !cli_web_submit(body, n)) {
                respond(c, 409, "Conflict", "application/json",
                        "{\"error\":\"the last commands are still running\"}");
            } else {
                respond(c, 202, "Accepted", "application/json", "{\"ok\":true}");
            }
        } else if (!strncmp(c->req, "POST /api/v1/update/check", 25)) {
            // the answer lands in the status JSON's update block
            char e[96];
            if (update_check_now(e, sizeof(e))) {
                respond(c, 202, "Accepted", "application/json", "{\"ok\":true}");
            } else {
                char ee[128], b[160];
                json_escape(ee, sizeof(ee), e);
                snprintf(b, sizeof(b), "{\"error\":\"%s\"}", ee);
                respond(c, 409, "Conflict", "application/json", b);
            }
        } else if (!strncmp(c->req, "POST /api/v1/update/latest", 26)) {
            // pull the release the check or the MQTT pointer named, under the
            // same signature and version rules as an upload; progress shows in
            // the status JSON and the controller reboots into it when done
            const char *url = update_latest_url();
            char e[96];
            if (!url[0]) {
                respond(c, 409, "Conflict", "application/json",
                        "{\"error\":\"no release is known yet: ask with POST /api/v1/update/check\"}");
            } else if (ota_pull_start(url, 0, e, sizeof(e))) {
                char b[64];
                snprintf(b, sizeof(b), "{\"ok\":true,\"version\":\"%s\"}", update_latest_version());
                respond(c, 202, "Accepted", "application/json", b);
            } else {
                char ee[128], b[160];
                json_escape(ee, sizeof(ee), e);
                snprintf(b, sizeof(b), "{\"error\":\"%s\"}", ee);
                respond(c, 409, "Conflict", "application/json", b);
            }
        } else if (!strncmp(c->req, "POST /api/v1/update/postpone", 28) ||
                   !strncmp(c->req, "POST /api/v1/update/skip", 24)) {
            // automatic installs only; Install still works
            char e[96];
            bool ok = c->req[20] == 'p' ? update_auto_postpone(e, sizeof(e)) : update_auto_skip(e, sizeof(e));
            if (ok) {
                respond(c, 200, "OK", "application/json", "{\"ok\":true}");
            } else {
                char ee[128], b[160];
                json_escape(ee, sizeof(ee), e);
                snprintf(b, sizeof(b), "{\"error\":\"%s\"}", ee);
                respond(c, 409, "Conflict", "application/json", b);
            }
        } else if (!strncmp(c->req, "POST /api/v1/improv", 19)) {
            // {"open": true} opens Wi-Fi setup for IMPROV_WINDOW_MS, false closes it
            bool open;
            if (!json_get_bool(body, "open", &open)) {
                respond(c, 400, "Bad Request", "application/json",
                        "{\"error\":\"send {\\\"open\\\": true} or false\"}");
            } else if (!improv_available()) {
                respond(c, 503, "Service Unavailable", "application/json",
                        "{\"error\":\"Wi-Fi setup is not available on this controller\"}");
            } else {
                if (open) improv_open(IMPROV_WINDOW_MS, "web");
                else improv_close();
                char b[64];
                snprintf(b, sizeof(b), "{\"ok\":true,\"improv\":\"%s\"}", improv_state_str());
                respond(c, 200, "OK", "application/json", b);
            }
        } else if (!strncmp(c->req, "POST /api/v1/tls/remove", 23)) {
            const char *err = https_remove();
            if (err) {
                char b[96];
                snprintf(b, sizeof(b), "{\"error\":\"%s\"}", err);
                respond(c, 409, "Conflict", "application/json", b);
            } else {
                respond(c, 200, "OK", "application/json", "{\"ok\":true}");
            }
        } else if (!strncmp(c->req, "POST /api/v1/faults/clear", 25)) {
            if (!fault_log_available()) {
                respond(c, 503, "Service Unavailable", "application/json",
                        "{\"error\":\"no fault log on this board\"}");
                return;
            }
            bool ok = fault_log_clear(); // sector erases via flash_safe_execute, like a settings save
            respond(c, ok ? 200 : 500, ok ? "OK" : "Internal Server Error", "application/json",
                    ok ? "{\"ok\":true}" : "{\"error\":\"clear failed\"}");
        } else if (!strncmp(c->req, "POST /api/v1/budget", 19)) {
            const char *w = strstr(body, "\"watts\"");
            long watts = -1;
            if (w && (w = strchr(w, ':')) != NULL) watts = strtol(w + 1, NULL, 10);
            if (watts < BUDGET_MIN_W || watts > BUDGET_MAX_W) {
                respond(c, 400, "Bad Request", "application/json",
                        "{\"error\":\"watts out of range\"}");
                return;
            }
            g_settings.budget_mw = (uint32_t)watts * 1000u;
            engine_cmd_t cmd = {.op = CMD_SET_BUDGET, .arg = g_settings.budget_mw};
            settings_save_later();
            respond(c, ipc_cmd_push(&cmd) ? 200 : 503,
                    "OK", "application/json", "{\"ok\":true}");
        } else if (!strncmp(c->req, "POST /api/v1/fan", 16)) {
            engine_cmd_t cmd;
            if (strstr(body, "\"auto\"")) {
                cmd.op = CMD_FAN_AUTO;
                g_settings.fan_auto = 1;
            } else {
                cmd.op = CMD_FAN;
                cmd.arg = strstr(body, "true") != NULL;
                g_settings.fan_auto = 0;
            }
            settings_save_later();
            respond(c, ipc_cmd_push(&cmd) ? 200 : 503,
                    "OK", "application/json", "{\"ok\":true}");
        } else if (!strncmp(c->req, "POST /api/v1/reboot", 19)) {
            uint32_t t = to_ms_since_boot(get_absolute_time()) + REBOOT_DELAY_MS;
            reboot_at_ms = t ? t : 1; // the main loop reboots (and flushes a pending save)
            printf("http: reboot requested\n");
            respond(c, 200, "OK", "application/json",
                    "{\"ok\":true,\"action\":\"reboot in 300ms\"}");
        } else {
            respond(c, 404, "Not Found", "application/json", "{\"error\":\"no such endpoint\"}");
        }
    } else {
        respond(c, 404, "Not Found", "text/plain", "not found");
    }
}

// Port 80 once HTTPS is on: the same request, at https:// on the same host
static void respond_redirect(conn_t *c) {
    static char loc[320]; // static: lwIP callback stack
    if (!http_req_https_location(c->req, loc, sizeof(loc))) {
        respond(c, 400, "Bad Request", "text/plain", "HTTPS only: send a Host header");
        return;
    }
    // 307: the method and body carry over, and nothing caches it, so turning
    // HTTPS off later is not undone by a browser that remembers the redirect
    int n = snprintf(c->resp, RESP_MAX,
                     "HTTP/1.1 307 Temporary Redirect\r\nLocation: %s\r\nContent-Type: text/plain\r\n"
                     "Content-Length: 11\r\nConnection: %s\r\n\r\nUse HTTPS.\n",
                     loc, CONN_HDR(c));
    c->resp_len = (uint16_t)(n < RESP_MAX ? n : 0);
    c->resp_sent = 0;
    send_more(c);
}

static err_t recv_cb(void *arg, struct altcp_pcb *pcb, struct pbuf *p, err_t err) {
    conn_t *c = (conn_t *)arg;
    if (!p) { // remote closed
        if (c) conn_close(c); // conn_free aborts a transfer cut off mid-body
        else altcp_close(pcb);
        return ERR_OK;
    }
    if (!c) {
        pbuf_free(p);
        altcp_abort(pcb);
        return ERR_ABRT;
    }
    (void)err;

    // Ack the window up front: everything below consumes the whole pbuf, and
    // the handlers may close the pcb (making it unsafe to touch afterwards).
    altcp_recved(pcb, p->tot_len);
    c->idle_polls = 0;

    if (c->discard) {
        c->discard -= p->tot_len < c->discard ? p->tot_len : c->discard;
        pbuf_free(p);
        if (!c->discard && !c->resp_len && !tls_unsent(c)) conn_close(c);
        return ERR_OK;
    }

    if (c->resp_len) {
        // response still in flight: a client that sends its next request
        // before this one is answered gets the connection closed after it,
        // and asks again on a new one
        c->keep_alive = false;
    } else if (c->updating) {
        update_feed(c, p, 0);
    } else if (c->tls_upload) {
        tls_upload_feed(c, p, 0);
    } else {
        uint16_t copied = pbuf_copy_partial(p, c->req + c->req_len,
                                            (uint16_t)(REQ_MAX - 1 - c->req_len), 0);
        c->req_len += copied;
        c->req[c->req_len] = '\0';

        char *hdr_end = strstr(c->req, "\r\n\r\n");
        if (hdr_end) c->keep_alive = http_req_keep_alive(c->req);
        if (hdr_end && !host_allowed(c)) {
            c->keep_alive = false;
            log_refusal(c, 403, "unknown host name");
            respond(c, 403, "Forbidden", "text/html", WRONG_HOST_HTML);
        } else if (hdr_end && !c->tls && https_enforced()) {
            c->keep_alive = false; // whatever body follows is not read
            respond_redirect(c);
        } else if (hdr_end && !strncmp(c->req, "POST /api/v1/update", 19) &&
                   (c->req[19] == ' ' || c->req[19] == '?')) {
            // the image upload only: /api/v1/update/check and /latest are requests
            // counted before the start, whose refusal may finish sending at once
            discard_body(c, hdr_end + 4, p->tot_len - copied);
            update_post_start(c, hdr_end + 4);
            if (c->updating) c->discard = 0;
            // body bytes past what fit in req[] are still in this pbuf
            if (c->updating && copied < p->tot_len) update_feed(c, p, copied);
        } else if (hdr_end && !strncmp(c->req, "POST /api/v1/tls", 16) &&
                   (c->req[16] == ' ' || c->req[16] == '?')) {
            discard_body(c, hdr_end + 4, p->tot_len - copied);
            tls_post_start(c, hdr_end + 4);
            if (c->tls_upload) c->discard = 0;
            if (c->tls_upload && copied < p->tot_len) tls_upload_feed(c, p, copied);
        } else if (hdr_end) {
            // non-update bodies are small and usually ride in with the
            // headers; when Content-Length says the rest is still in flight
            // and req[] has room for it, wait for the next segment (a client
            // that never finishes is dropped by the idle poll)
            long cl = content_length(c->req);
            long have = (long)(c->req_len - (uint16_t)(hdr_end + 4 - c->req));
            bool waiting = cl > 0 && have < cl && c->req_len < REQ_MAX - 1;
            if (!waiting) {
                // a body cut short by req[], or bytes past it (a second
                // request sent early): this connection ends with the response
                if (have != (cl > 0 ? cl : 0) || copied < p->tot_len) c->keep_alive = false;
                handle_request(c);
            }
        } else if (c->req_len >= REQ_MAX - 1) {
            c->keep_alive = false;
            respond(c, 431, "Request Header Fields Too Large", "text/plain", "too large");
        }
    }

    pbuf_free(p);
    return ERR_OK;
}

static err_t sent_cb(void *arg, struct altcp_pcb *pcb, u16_t len) {
    (void)pcb; (void)len;
    conn_t *c = (conn_t *)arg;
    if (c && c->resp_len) send_more(c);
    return ERR_OK;
}

// Every 500ms per connection. Two jobs sent_cb can't do: resume a response
// whose write failed with nothing in flight (sent_cb only fires once queued
// data is acked), and free a slot held by a client that sends no request:
// browsers preconnect spare sockets and keep finished ones open, and with
// MAX_CONNS slots a few of those would lock everyone else out. An OTA body
// in progress is exempt; update_begin() has its own stale-transfer
// handling. A certificate upload that stops sending is dropped like the rest.
static err_t poll_cb(void *arg, struct altcp_pcb *pcb) {
    (void)pcb;
    conn_t *c = (conn_t *)arg;
    if (!c) return ERR_OK;
    if (c->resp_len) {
        send_more(c);
    } else if (!c->updating && ++c->idle_polls >= IDLE_POLLS) {
        conn_close(c);
    }
    return ERR_OK;
}

static void err_cb(void *arg, err_t err) {
    (void)err;
    conn_t *c = (conn_t *)arg;
    if (c) conn_free(c); // pcb already gone
}

#define PRECONNECT_POLLS 4 // ~2 s without a byte: a spare socket, not a TLS handshake

// A connection waiting between requests, or one that has sent nothing for
// a while, can give its slot to a new one: the browser asks again on
// another. One still in its TLS handshake looks idle but is not, and
// taking its slot only starts another handshake that the next client
// takes again.
static conn_t *idle_slot(void) {
    conn_t *best = NULL;
    for (int i = 0; i < MAX_CONNS; i++) {
        conn_t *c = &conns[i];
        if (c->req_len || c->resp_len || c->updating || c->tls_upload) continue;
        if (!c->served && c->idle_polls < PRECONNECT_POLLS) continue;
        if (!best || c->idle_polls > best->idle_polls) best = c;
    }
    return best;
}

// arg is non-NULL on the HTTPS listener
static err_t accept_cb(void *arg, struct altcp_pcb *newpcb, err_t err) {
    if (err != ERR_OK || !newpcb) return ERR_VAL;
    conn_t *c = NULL;
    for (int i = 0; i < MAX_CONNS; i++) {
        if (!conns[i].pcb) { c = &conns[i]; break; }
    }
    if (!c && (c = idle_slot()) != NULL) conn_close(c);
    if (!c) return ERR_MEM;
    struct altcp_tls_config *conf = NULL;
    if (arg) {
        // the handshake starts with the client's first bytes
        struct altcp_pcb *tls = (conf = https_config()) ? altcp_tls_wrap(conf, newpcb) : NULL;
        if (!tls) {
            printf("http: no memory for another HTTPS connection\n");
            altcp_abort(newpcb);
            return ERR_ABRT;
        }
        newpcb = tls;
    }
    conn_free(c);
    c->pcb = newpcb;
    if (conf) {
        c->tls = true;
        c->tls_conf = conf;
        https_conn_opened(conf);
    }
    altcp_arg(newpcb, c);
    altcp_recv(newpcb, recv_cb);
    altcp_sent(newpcb, sent_cb);
    altcp_poll(newpcb, poll_cb, POLL_INTERVAL);
    altcp_err(newpcb, err_cb);
    return ERR_OK;
}

void http_tls_sync(void) {
    if (!net_available()) return;
    net_lock();
    bool want = g_settings.https && https_config();
    if (tls_listener && !want) {
        // connections already accepted keep going until they close
        altcp_close(tls_listener);
        tls_listener = NULL;
    }
    if (want && !tls_listener) {
        struct altcp_pcb *l = altcp_tcp_new_ip_type(IPADDR_TYPE_ANY);
        // connections from before HTTPS was last turned off may still be in
        // TIME_WAIT on 443; altcp_tcp keeps the TCP pcb as its state
        if (l) ip_set_option((struct tcp_pcb *)l->state, SOF_REUSEADDR);
        if (l && altcp_bind(l, IP_ANY_TYPE, HTTPS_PORT) == ERR_OK) {
            struct altcp_pcb *listening = altcp_listen_with_backlog(l, 4);
            if (listening) {
                tls_listener = listening;
                altcp_arg(listening, (void *)1);
                altcp_accept(listening, accept_cb);
                printf("http: HTTPS on port %d for %s; port %d redirects\n", HTTPS_PORT,
                       https_names()[0] ? https_names() : "(no names)", HTTP_PORT);
            } else {
                altcp_abort(l);
            }
        } else if (l) {
            altcp_abort(l);
        }
        if (!tls_listener) printf("http: failed to listen on port %d\n", HTTPS_PORT);
    } else if (!want && g_settings.https) {
        printf("http: HTTPS is on but no certificate is installed: serving plain HTTP\n");
    }
    net_unlock();
}

void http_init(void) {
    http_setup_window_restart(to_ms_since_boot(get_absolute_time()));
    if (!net_available()) return;
    net_lock();
    https_init();
    struct altcp_pcb *pcb = altcp_tcp_new_ip_type(IPADDR_TYPE_ANY);
    if (pcb && altcp_bind(pcb, IP_ANY_TYPE, HTTP_PORT) == ERR_OK) {
        pcb = altcp_listen_with_backlog(pcb, 4);
        altcp_accept(pcb, accept_cb);
    } else {
        printf("http: failed to bind port %d\n", HTTP_PORT);
        if (pcb) altcp_abort(pcb);
    }
    net_unlock();
    http_tls_sync();
}
