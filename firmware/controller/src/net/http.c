#include "http.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pico/cyw43_arch.h"

#include "lwip/tcp.h"

#include "ipc.h"
#include "manifold.h"
#include "net.h"
#include "settings.h"

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
} conn_t;

static conn_t conns[MAX_CONNS];

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
    "<th>Contract</th></tr></thead><tbody id='ports'></tbody></table>"
    "<script>"
    "async function tick(){try{"
    "const r=await fetch('/api/v1/status');const d=await r.json();"
    "document.getElementById('chassis').textContent="
    "`${d.name} \\u2014 ${d.total_w.toFixed(1)}W of ${d.budget_w.toFixed(0)}W budget"
    " (${d.headroom_w.toFixed(0)}W headroom) \\u2014 fan ${d.fan} \\u2014 fw ${d.fw}`;"
    "document.getElementById('ports').innerHTML=d.ports.map((p,i)=>"
    "`<tr><td>${i+1}</td><td class='s-${p.state}'>${p.state}</td>"
    "<td>${p.v.toFixed(2)}</td><td>${p.i.toFixed(2)}</td><td>${p.p.toFixed(1)}</td>"
    "<td>${p.contract_w.toFixed(0)}W</td></tr>`).join('');"
    "}catch(e){}}tick();setInterval(tick,1000);"
    "</script></body></html>";

static void conn_free(conn_t *c) {
    c->pcb = NULL;
    c->req_len = 0;
    c->resp_len = 0;
    c->resp_sent = 0;
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
        "{\"name\":\"%s\",\"fw\":\"%s\",\"uptime_s\":%lu,\"rssi\":%ld,"
        "\"total_w\":%.2f,\"reserved_w\":%.1f,\"budget_w\":%.1f,"
        "\"headroom_w\":%.1f,\"fan\":\"%s\",\"alert\":%s,\"ports\":[",
        g_settings.device_name, FW_VERSION,
        (unsigned long)(to_ms_since_boot(get_absolute_time()) / 1000),
        (long)net_rssi(), t.total_mw / 1000.0, t.reserved_mw / 1000.0,
        t.budget_mw / 1000.0, headroom / 1000.0, t.fan_on ? "on" : "off",
        t.alert_active ? "true" : "false");

    for (int i = 0; i < NUM_PORTS && off < cap; i++) {
        const port_telemetry_t *p = &t.port[i];
        off += (size_t)snprintf(out + off, cap - off,
            "%s{\"state\":\"%s\",\"attached\":%s,\"pdo\":%u,\"v\":%.3f,"
            "\"i\":%.3f,\"p\":%.2f,\"contract_w\":%.1f,\"fault\":%u}",
            i ? "," : "", port_state_name((port_state_t)p->state),
            p->attached ? "true" : "false", p->selected_pdo, p->bus_mv / 1000.0,
            p->current_ma / 1000.0, p->power_mw / 1000.0,
            p->contract_mw / 1000.0, p->fault_bits);
    }
    if (off < cap) snprintf(out + off, cap - off, "]}");
}

static bool authorized(const conn_t *c) {
    if (!g_settings.api_token[0]) return true;
    char needle[64];
    snprintf(needle, sizeof(needle), "Authorization: Bearer %s", g_settings.api_token);
    return strstr(c->req, needle) != NULL;
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
        if (c) conn_close(c);
        else tcp_close(pcb);
        return ERR_OK;
    }
    if (!c) {
        pbuf_free(p);
        tcp_abort(pcb);
        return ERR_ABRT;
    }
    (void)err;

    uint16_t copied = pbuf_copy_partial(p, c->req + c->req_len,
                                        (uint16_t)(REQ_MAX - 1 - c->req_len), 0);
    c->req_len += copied;
    c->req[c->req_len] = '\0';
    tcp_recved(pcb, p->tot_len);
    pbuf_free(p);

    // headers complete? (bodies used here are tiny and arrive with them; a
    // split POST body larger than one segment is out of scope for this server)
    if (strstr(c->req, "\r\n\r\n")) {
        handle_request(c);
    } else if (c->req_len >= REQ_MAX - 1) {
        respond(c, 431, "Request Header Fields Too Large", "text/plain", "too large");
    }
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
