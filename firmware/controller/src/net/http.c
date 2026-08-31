#include "http.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "pico/cyw43_arch.h"

#include "lwip/tcp.h"

#include "flash_map.h"
#include "ipc.h"
#include "manifold.h"
#include "net.h"
#include "settings.h"
#include "update.h"

#define HTTP_PORT     80
#define MAX_CONNS     4
#define REQ_MAX       1024
#define RESP_MAX      2560

typedef struct {
    struct tcp_pcb *pcb;
    char req[REQ_MAX];
    uint16_t req_len;
    char resp[RESP_MAX];
    uint16_t resp_len;
    uint16_t resp_sent;
    bool updating;      // headers done, body streams into update_write()
    uint32_t body_left; // update body bytes still expected
} conn_t;

static conn_t conns[MAX_CONNS];
static conn_t *update_conn; // the one connection allowed to stream an update

static const char INDEX_HTML[] =
    "<!doctype html><html><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>Power Manifold</title><style>"
    "body{font-family:system-ui,sans-serif;background:#111;color:#eee;margin:1.5em}"
    "h1{font-size:1.3em}table{border-collapse:collapse;width:100%;max-width:44em}"
    "td,th{padding:.4em .7em;text-align:left;border-bottom:1px solid #333}"
    "th{color:#888;font-weight:600}#chassis{color:#9ad;margin:.8em 0}"
    ".s-active{color:#6f6}.s-idle{color:#fc6}.s-fault{color:#f55}"
    ".s-throttled{color:#ff5}.s-absent{color:#666}.s-probe{color:#6dd}"
    ".s-disabled{color:#555}</style></head><body>"
    "<h1>Power Manifold</h1><div id='chassis'>loading&hellip;</div>"
    "<table><thead><tr><th>Port</th><th>State</th><th>V</th><th>A</th><th>W</th>"
    "<th>kWh</th><th>Contract</th></tr></thead><tbody id='ports'></tbody></table>"
    "<script>"
    "async function tick(){try{"
    "const r=await fetch('/api/v1/status');const d=await r.json();"
    "document.getElementById('chassis').textContent="
    "`${d.name} \\u2014 ${d.total_w.toFixed(1)}W of ${d.budget_w.toFixed(0)}W budget"
    " (${d.headroom_w.toFixed(0)}W headroom) \\u2014 fan ${d.fan} \\u2014 fw ${d.fw}`;"
    "document.getElementById('ports').innerHTML=d.ports.map((p,i)=>"
    "`<tr><td>${i+1}</td><td class='s-${p.state}'>${p.state}</td>"
    "<td>${p.v.toFixed(2)}</td><td>${p.i.toFixed(2)}</td><td>${p.p.toFixed(1)}</td>"
    "<td>${p.e.toFixed(3)}</td><td>${p.contract_w.toFixed(0)}W</td></tr>`).join('');"
    "}catch(e){}}tick();setInterval(tick,1000);"
    "</script></body></html>";

static void conn_free(conn_t *c) {
    if (c->updating) update_abort(); // transfer died with its connection
    if (update_conn == c) update_conn = NULL;
    c->pcb = NULL;
    c->req_len = 0;
    c->resp_len = 0;
    c->resp_sent = 0;
    c->updating = false;
    c->body_left = 0;
}

static void conn_close(conn_t *c) {
    if (c->pcb) {
        tcp_arg(c->pcb, NULL);
        tcp_recv(c->pcb, NULL);
        tcp_sent(c->pcb, NULL);
        tcp_err(c->pcb, NULL);
        if (tcp_close(c->pcb) != ERR_OK) tcp_abort(c->pcb);
    }
    conn_free(c);
}

static void send_more(conn_t *c) {
    while (c->resp_sent < c->resp_len) {
        uint16_t chunk = c->resp_len - c->resp_sent;
        uint16_t room = tcp_sndbuf(c->pcb);
        if (room == 0) return;
        if (chunk > room) chunk = room;
        if (tcp_write(c->pcb, c->resp + c->resp_sent, chunk, TCP_WRITE_FLAG_COPY) != ERR_OK)
            return;
        c->resp_sent += chunk;
    }
    tcp_output(c->pcb);
    if (c->resp_sent >= c->resp_len) conn_close(c);
}

static void respond(conn_t *c, int code, const char *status,
                    const char *content_type, const char *body) {
    int n = snprintf(c->resp, RESP_MAX,
                     "HTTP/1.1 %d %s\r\nContent-Type: %s\r\n"
                     "Content-Length: %u\r\nConnection: close\r\n\r\n%s",
                     code, status, content_type, (unsigned)strlen(body), body);
    c->resp_len = (uint16_t)(n >= RESP_MAX ? RESP_MAX - 1 : n);
    c->resp_sent = 0;
    send_more(c);
}

static void build_status_json(char *out, size_t cap) {
    telemetry_t t;
    ipc_snapshot_read(&t);
    uint32_t headroom = t.budget_mw > t.reserved_mw ? t.budget_mw - t.reserved_mw : 0;

    size_t off = (size_t)snprintf(out, cap,
        "{\"name\":\"%s\",\"fw\":\"%s\",\"slot\":\"%s\",\"trial\":%s,"
        "\"uptime_s\":%lu,\"rssi\":%ld,"
        "\"total_w\":%.2f,\"reserved_w\":%.1f,\"budget_w\":%.1f,"
        "\"headroom_w\":%.1f,\"energy_kwh\":%.3f,\"fan\":\"%s\",\"alert\":%s,"
        "\"ports\":[",
        g_settings.device_name, FW_VERSION, flash_map_slot_name(),
        flash_map_update_pending() ? "true" : "false",
        (unsigned long)(to_ms_since_boot(get_absolute_time()) / 1000),
        (long)net_rssi(), t.total_mw / 1000.0, t.reserved_mw / 1000.0,
        t.budget_mw / 1000.0, headroom / 1000.0, t.energy_mwh / 1e6,
        t.fan_on ? "on" : "off", t.alert_active ? "true" : "false");

    for (int i = 0; i < NUM_PORTS && off < cap; i++) {
        const port_telemetry_t *p = &t.port[i];
        off += (size_t)snprintf(out + off, cap - off,
            "%s{\"state\":\"%s\",\"attached\":%s,\"pdo\":%u,\"v\":%.3f,"
            "\"i\":%.3f,\"p\":%.2f,\"e\":%.3f,\"contract_w\":%.1f,\"prio\":%u,"
            "\"fault\":%u}",
            i ? "," : "", port_state_name((port_state_t)p->state),
            p->attached ? "true" : "false", p->selected_pdo, p->bus_mv / 1000.0,
            p->current_ma / 1000.0, p->power_mw / 1000.0, p->energy_mwh / 1e6,
            p->contract_mw / 1000.0, g_settings.port_priority[i], p->fault_bits);
    }
    if (off < cap) snprintf(out + off, cap - off, "]}");
}

static bool authorized(const conn_t *c) {
    if (!g_settings.api_token[0]) return true;
    char needle[64];
    snprintf(needle, sizeof(needle), "Authorization: Bearer %s", g_settings.api_token);
    return strstr(c->req, needle) != NULL;
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

// ---- OTA upload: POST /api/v1/update, body = firmware image (uf2 or bin).
// The body streams straight into update_write(); nothing except the request
// headers ever lands in req[].

static void update_fail(conn_t *c, int code, const char *status, const char *msg) {
    char body[160];
    c->updating = false;
    if (update_conn == c) update_conn = NULL;
    update_abort();
    snprintf(body, sizeof(body), "{\"error\":\"%s\"}", msg);
    respond(c, code, status, "application/json", body);
}

static void update_complete(conn_t *c) {
    char err[96], body[192];
    c->updating = false;
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

static void update_post_start(conn_t *c, const char *body_start) {
    if (!authorized(c)) {
        respond(c, 401, "Unauthorized", "application/json",
                "{\"error\":\"bearer token required\"}");
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
    if (!update_begin((uint32_t)cl, err, sizeof(err))) {
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

static void handle_request(conn_t *c) {
    char json[1600];

    if (!strncmp(c->req, "GET / ", 6)) {
        respond(c, 200, "OK", "text/html", INDEX_HTML);
    } else if (!strncmp(c->req, "GET /api/v1/status", 18)) {
        build_status_json(json, sizeof(json));
        respond(c, 200, "OK", "application/json", json);
    } else if (!strncmp(c->req, "POST /api/v1/", 13)) {
        if (!authorized(c)) {
            respond(c, 401, "Unauthorized", "application/json",
                    "{\"error\":\"bearer token required\"}");
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
            else {
                respond(c, 400, "Bad Request", "application/json",
                        "{\"error\":\"unknown action\"}");
                return;
            }
            respond(c, ipc_cmd_push(&cmd) ? 200 : 503,
                    "OK", "application/json", "{\"ok\":true}");
        } else if (!strncmp(c->req, "POST /api/v1/fan", 16)) {
            engine_cmd_t cmd = {.op = CMD_FAN, .arg = strstr(body, "true") != NULL};
            respond(c, ipc_cmd_push(&cmd) ? 200 : 503,
                    "OK", "application/json", "{\"ok\":true}");
        } else {
            respond(c, 404, "Not Found", "application/json", "{\"error\":\"no such endpoint\"}");
        }
    } else {
        respond(c, 404, "Not Found", "text/plain", "not found");
    }
}

static err_t recv_cb(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err) {
    conn_t *c = (conn_t *)arg;
    if (!p) { // remote closed
        if (c) conn_close(c); // conn_free aborts a transfer cut off mid-body
        else tcp_close(pcb);
        return ERR_OK;
    }
    if (!c) {
        pbuf_free(p);
        tcp_abort(pcb);
        return ERR_ABRT;
    }
    (void)err;

    // Ack the window up front: everything below consumes the whole pbuf, and
    // the handlers may close the pcb (making it unsafe to touch afterwards).
    tcp_recved(pcb, p->tot_len);

    if (c->resp_len) {
        // response already in flight; drain and ignore whatever else arrives
    } else if (c->updating) {
        update_feed(c, p, 0);
    } else {
        uint16_t copied = pbuf_copy_partial(p, c->req + c->req_len,
                                            (uint16_t)(REQ_MAX - 1 - c->req_len), 0);
        c->req_len += copied;
        c->req[c->req_len] = '\0';

        char *hdr_end = strstr(c->req, "\r\n\r\n");
        if (hdr_end && !strncmp(c->req, "POST /api/v1/update", 19)) {
            update_post_start(c, hdr_end + 4);
            // body bytes past what fit in req[] are still in this pbuf
            if (c->updating && copied < p->tot_len) update_feed(c, p, copied);
        } else if (hdr_end) {
            // non-update bodies are tiny and arrive with the headers; a split
            // POST body larger than one segment is out of scope for this server
            handle_request(c);
        } else if (c->req_len >= REQ_MAX - 1) {
            respond(c, 431, "Request Header Fields Too Large", "text/plain", "too large");
        }
    }

    pbuf_free(p);
    return ERR_OK;
}

static err_t sent_cb(void *arg, struct tcp_pcb *pcb, u16_t len) {
    (void)pcb; (void)len;
    conn_t *c = (conn_t *)arg;
    if (c && c->resp_len) send_more(c);
    return ERR_OK;
}

static void err_cb(void *arg, err_t err) {
    (void)err;
    conn_t *c = (conn_t *)arg;
    if (c) conn_free(c); // pcb already gone
}

static err_t accept_cb(void *arg, struct tcp_pcb *newpcb, err_t err) {
    (void)arg;
    if (err != ERR_OK || !newpcb) return ERR_VAL;
    conn_t *c = NULL;
    for (int i = 0; i < MAX_CONNS; i++) {
        if (!conns[i].pcb) { c = &conns[i]; break; }
    }
    if (!c) return ERR_MEM;
    conn_free(c);
    c->pcb = newpcb;
    tcp_arg(newpcb, c);
    tcp_recv(newpcb, recv_cb);
    tcp_sent(newpcb, sent_cb);
    tcp_err(newpcb, err_cb);
    return ERR_OK;
}

void http_init(void) {
    if (!net_available()) return;
    cyw43_arch_lwip_begin();
    struct tcp_pcb *pcb = tcp_new_ip_type(IPADDR_TYPE_ANY);
    if (pcb && tcp_bind(pcb, IP_ANY_TYPE, HTTP_PORT) == ERR_OK) {
        pcb = tcp_listen_with_backlog(pcb, 4);
        tcp_accept(pcb, accept_cb);
    } else {
        printf("http: failed to bind port %d\n", HTTP_PORT);
        if (pcb) tcp_abort(pcb);
    }
    cyw43_arch_lwip_end();
}
