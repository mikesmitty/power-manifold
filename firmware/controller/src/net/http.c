#include "http.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>


#include "lwip/tcp.h"
#include "pico/rand.h"

#include "flash_map.h"
#include "ipc.h"
#include "manifold.h"
#include "eth.h"
#include "improv.h"
#include "jsonlite.h"
#include "net.h"
#include "settings.h"
#include "update.h"

#define HTTP_PORT       80
#define MAX_CONNS       4
#define REQ_MAX         2048 // browser headers + a full settings body
#define STATUS_JSON_MAX 1600
#define HDR_MAX         128  // the status line + our three headers
#define RESP_MAX        (STATUS_JSON_MAX + HDR_MAX)
#define POLL_INTERVAL   1    // tcp_poll units of 500ms
#define IDLE_POLLS      20   // drop a connection that sends no request in ~10s
#define REBOOT_DELAY_MS 300  // API reboot: let the response leave first
#define SETUP_SECRET_TTL_MS (10 * 60 * 1000)
#define STR_(x) #x
#define STR(x) STR_(x)

typedef struct {
    struct tcp_pcb *pcb;
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
    bool updating;      // headers done, body streams into update_write()
    uint32_t body_left; // update body bytes still expected
    uint8_t idle_polls; // poll ticks with no request yet (browser preconnects)
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
    ".s-disabled{color:#555}"
    "tr.p{cursor:pointer}tr.p:hover td{background:#1a1a1a}tr.sel td{background:#1c2430}"
    "tr.d td{padding:.6em .7em .9em;background:#161a20}"
    ".g{display:grid;grid-template-columns:repeat(3,1fr);gap:.8em}"
    ".g div{font-size:.8em;color:#888}.g b{color:#eee;font-weight:600}"
    "svg{display:block;width:100%;height:5em;margin-top:.3em;background:#0d0d0d;"
    "border:1px solid #333}"
    "polyline{fill:none;stroke:#9ad;stroke-width:1.5;vector-effect:non-scaling-stroke}"
    "#hint{font-size:.8em;color:#666;margin-top:.8em;max-width:44em}"
    "details{margin-top:1.2em;max-width:44em}summary{cursor:pointer;color:#888}"
    "label{display:block;margin:.55em 0;font-size:.8em;color:#888}"
    "input,select{display:block;width:100%;box-sizing:border-box;margin-top:.2em;padding:.4em;"
    "background:#1a1a1a;color:#eee;border:1px solid #333;border-radius:3px;font:inherit}"
    "button{padding:.45em 1em;margin:.6em .6em 0 0;background:#1c2430;color:#eee;"
    "border:1px solid #345;border-radius:3px;cursor:pointer;font:inherit}"
    "#msg{color:#fc6;font-size:.85em;margin:.4em 0;min-height:1.2em}"
    "#lock input{display:inline-block;width:14em;margin-right:.5em}"
    "@media(max-width:40em){body{margin:1em .6em}td,th{padding:.4em .35em}"
    ".g{grid-template-columns:1fr}}"
    "</style></head><body>"
    "<h1>Power Manifold</h1><div id='chassis'>loading&hellip;</div>"
    "<table><thead><tr><th>Port</th><th>State</th><th>V</th><th>A</th>"
    "<th title='measured by the INA226'>Draw W</th>"
    "<th title='PD contract wattage held against the chassis budget'>Res W</th>"
    "<th>kWh</th></tr></thead><tbody id='ports'></tbody></table>"
    "<div id='hint'>Click a port for its last 10 minutes of W / A / V, sampled"
    " once a second while this page is open.</div>"
    // Connection-level settings. Unlocked by the API token, or by the setup
    // secret Improv passes in the redirect URL while no token exists yet.
    "<details id='cfg'><summary>Settings</summary><div id='msg'></div>"
    "<div id='lock' hidden><input id='tok' type='password' placeholder='API token'>"
    "<button id='ul'>Unlock</button></div>"
    "<form id='f' hidden autocomplete='off'>"
    "<label>Device name (hostname, MQTT topic id)"
    "<input name='dname' maxlength='31' pattern='[A-Za-z0-9\\-]+' required></label>"
    "<label>MQTT broker (blank = MQTT off)<input name='mhost' maxlength='63'></label>"
    "<label>MQTT port<input name='mport' type='number' min='1' max='65535'></label>"
    "<label>MQTT user<input name='muser' maxlength='32'></label>"
    "<label>MQTT password<input name='mpass' type='password' maxlength='64'></label>"
    "<label>Chassis budget (W)<input name='bud' type='number' min='" STR(BUDGET_MIN_W) "'"
    " max='" STR(BUDGET_MAX_W) "' required></label>"
    "<label>Fan<select name='fmode'><option value='auto'>auto</option>"
    "<option value='on'>on</option><option value='off'>off</option></select></label>"
    "<label>Fan auto: on at or above (W)"
    "<input name='fon' type='number' min='1' max='1000' required></label>"
    "<label>Fan auto: off at or below (W)"
    "<input name='foff' type='number' min='0' max='999' required></label>"
    "<label>Fan auto: also on while any contract exceeds (mA, 0 = off)"
    "<input name='fma' type='number' min='0' max='10000' required></label>"
    "<label>API token (locks the API and this panel)"
    "<input name='atok' type='password' maxlength='32'></label>"
    "<button type='submit'>Save</button>"
    "<button type='button' id='rb'>Reboot</button></form></details>"
    "<script>"
    // Per-port sample rings live in the page: the 1 Hz status poll already
    // carries V/A/W, so history costs the firmware nothing. Sparklines are
    // inline SVG — the device is LAN-only, so no chart library from a CDN.
    "const N=600,H=[],ports=document.getElementById('ports');let sel=-1,last;"
    "ports.onclick=e=>{const r=e.target.closest('tr.p');"
    "if(r){const i=+r.dataset.i;sel=sel==i?-1:i;draw();}};"
    // Samples fill the strip's width until the ring is full, then scroll;
    // the caption states the span actually covered so the scale is explicit.
    "function line(k,u,dp){const s=H[sel]||[],n=s.length,m=Math.max(...s.map(x=>x[k]),1e-9);"
    "const pts=s.map((x,j)=>`${(300*j/Math.max(n-1,1)).toFixed(1)},"
    "${(58-56*x[k]/m).toFixed(1)}`).join(' ');"
    "const sp=Math.max(n-1,0),t=sp<60?sp+'s':Math.floor(sp/60)+'m'+(sp%60?sp%60+'s':'');"
    "return `<div>${u} now <b>${(n?s[n-1][k]:0).toFixed(dp)}</b>"
    " &middot; peak ${m.toFixed(dp)} &middot; ${t}"
    "<svg viewBox='0 0 300 60' preserveAspectRatio='none'>"
    "<polyline points='${pts}'/></svg></div>`;}"
    "function draw(){if(!last)return;"
    "ports.innerHTML=last.ports.map((p,i)=>"
    "`<tr class='p${i==sel?' sel':''}' data-i='${i}'><td>${i+1}</td>"
    "<td class='s-${p.state}'>${p.state}</td>"
    "<td>${p.v.toFixed(2)}</td><td>${p.i.toFixed(2)}</td><td>${p.p.toFixed(1)}</td>"
    "<td>${p.contract_w.toFixed(0)}</td><td>${p.e.toFixed(3)}</td></tr>`+"
    "(i==sel?`<tr class='d'><td colspan='7'><div class='g'>${line('p','W',1)}"
    "${line('i','A',2)}${line('v','V',2)}</div></td></tr>`:'')).join('');}"
    "async function tick(){try{"
    "const r=await fetch('/api/v1/status');const d=await r.json();last=d;"
    "document.getElementById('chassis').textContent="
    "`${d.name} \\u2014 ${d.total_w.toFixed(1)}W drawn, ${d.reserved_w.toFixed(0)}W"
    " reserved of ${d.budget_w.toFixed(0)}W budget"
    " (${d.headroom_w.toFixed(0)}W free) \\u2014 fan ${d.fan} \\u2014 fw ${d.fw}`;"
    "d.ports.forEach((p,i)=>{(H[i]=H[i]||[]).push({v:p.v,i:p.i,p:p.p});"
    "if(H[i].length>N)H[i].shift();});"
    "draw();}catch(e){}}tick();setInterval(tick,1000);"
    // Settings panel. The bearer lives in sessionStorage for this tab only;
    // a ?s=<secret> from Improv's redirect seeds it and is scrubbed from the
    // address bar. Blank password/token fields mean "unchanged".
    "const CFG=document.getElementById('cfg'),F=document.getElementById('f'),"
    "M=document.getElementById('msg'),LK=document.getElementById('lock'),"
    "KEYS={dname:'name',mhost:'mqtt_host',mport:'mqtt_port',muser:'mqtt_user',bud:'budget_w',"
    "fmode:'fan_mode',fon:'fan_on_w',foff:'fan_off_w',fma:'fan_on_ma'},"
    "NUM={mport:1,bud:1,fon:1,foff:1,fma:1},"
    "hdr=()=>sessionStorage.tok?{Authorization:'Bearer '+sessionStorage.tok}:{};"
    "async function cfgLoad(){let r;"
    "try{r=await fetch('/api/v1/settings',{headers:hdr()});}"
    "catch(e){M.textContent='No response from the device.';return;}"
    "if(r.status==401){LK.hidden=false;F.hidden=true;"
    "M.textContent=(sessionStorage.tok?'Token rejected. ':'')+"
    "'Enter the API token to edit settings.';return;}"
    "const d=await r.json();LK.hidden=true;F.hidden=false;"
    "for(const k in KEYS)F[k].value=d[KEYS[k]];"
    "F.mpass.placeholder=d.mqtt_pass_set?'(unchanged)':'(none)';"
    "F.atok.placeholder=d.token_set?'(unchanged)':'required';F.atok.required=!d.token_set;"
    "M.textContent=d.token_set?'':'Setup: choose an API token to finish. It locks"
    " the API and this panel, so keep a copy.';}"
    "F.onsubmit=async e=>{e.preventDefault();const b={};"
    "for(const k in KEYS)b[KEYS[k]]=NUM[k]?+F[k].value:F[k].value;b.mqtt_port=b.mqtt_port||1883;"
    "if(F.mpass.value)b.mqtt_pass=F.mpass.value;if(F.atok.value)b.token=F.atok.value;"
    "let r,d={};try{r=await fetch('/api/v1/settings',{method:'POST',"
    "headers:{...hdr(),'Content-Type':'application/json'},body:JSON.stringify(b)});"
    "d=await r.json();}catch(e){}"
    "if(!r||!r.ok){M.textContent='Not saved: '+(d.error||'no response');return;}"
    "if(b.token)sessionStorage.tok=b.token;F.mpass.value=F.atok.value='';"
    "await cfgLoad();M.textContent=d.reboot_required?"
    "'Saved. Reboot to apply the name and broker.':'Saved and applied.';};"
    "document.getElementById('rb').onclick=async()=>{"
    "try{await fetch('/api/v1/reboot',{method:'POST',headers:hdr()});}catch(e){}"
    "M.textContent='Rebooting; this page reloads in a few seconds.';"
    "setTimeout(()=>location.reload(),6000);};"
    "document.getElementById('ul').onclick=()=>{"
    "sessionStorage.tok=document.getElementById('tok').value;cfgLoad();};"
    "CFG.ontoggle=()=>{if(CFG.open)cfgLoad();};"
    "const U=new URL(location),S=U.searchParams.get('s');"
    "if(S){sessionStorage.tok=S;U.searchParams.delete('s');"
    "history.replaceState(null,'',U);CFG.open=true;}"
    "</script></body></html>";
_Static_assert(sizeof(INDEX_HTML) - 1 <= UINT16_MAX, "conn_t.static_len is 16-bit");

static void conn_free(conn_t *c) {
    if (c->updating) update_abort(); // transfer died with its connection
    if (update_conn == c) update_conn = NULL;
    c->pcb = NULL;
    c->req_len = 0;
    c->resp_len = 0;
    c->resp_sent = 0;
    c->static_body = NULL;
    c->static_len = 0;
    c->static_sent = 0;
    c->updating = false;
    c->body_left = 0;
    c->idle_polls = 0;
}

static void conn_close(conn_t *c) {
    if (c->pcb) {
        tcp_arg(c->pcb, NULL);
        tcp_recv(c->pcb, NULL);
        tcp_sent(c->pcb, NULL);
        tcp_poll(c->pcb, NULL, 0);
        tcp_err(c->pcb, NULL);
        if (tcp_close(c->pcb) != ERR_OK) tcp_abort(c->pcb);
    }
    conn_free(c);
}

// Queue as much of one span as the send buffer takes; true once it is all
// queued. sent_cb resumes a partial span when the window opens again.
static bool send_span(conn_t *c, const char *data, uint16_t len, uint16_t *sent,
                      uint8_t flags) {
    while (*sent < len) {
        uint16_t chunk = len - *sent;
        uint16_t room = tcp_sndbuf(c->pcb);
        if (room == 0) return false;
        if (chunk > room) chunk = room;
        err_t err = tcp_write(c->pcb, data + *sent, chunk, flags);
        if (err != ERR_OK) {
            printf("http: tcp_write %d at %u/%u, retrying on poll\n", (int)err,
                   (unsigned)*sent, (unsigned)len);
            return false;
        }
        *sent += chunk;
    }
    return true;
}

static void send_more(conn_t *c) {
    // resp[] is reused per request, so lwIP copies it; a static body lives
    // in flash for good, so lwIP may reference it in place
    bool done = send_span(c, c->resp, c->resp_len, &c->resp_sent, TCP_WRITE_FLAG_COPY) &&
                send_span(c, c->static_body, c->static_len, &c->static_sent, 0);
    tcp_output(c->pcb);
    if (done) conn_close(c);
}

#define HDR_FMT "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %u\r\n" \
                "Connection: close\r\n\r\n"

// Small dynamic body: headers and body together in resp[]. Bodies are the
// status JSON (STATUS_JSON_MAX) or short error/result objects, so this fits
// by construction; a body that somehow doesn't becomes a 500 rather than a
// truncated payload the client would wait on forever.
static void respond(conn_t *c, int code, const char *status,
                    const char *content_type, const char *body) {
    int n = snprintf(c->resp, RESP_MAX, HDR_FMT "%s", code, status, content_type,
                     (unsigned)strlen(body), body);
    if (n >= RESP_MAX) {
        n = snprintf(c->resp, RESP_MAX, HDR_FMT "%s", 500, "Internal Server Error",
                     "text/plain", 18u, "response too large");
    }
    c->resp_len = (uint16_t)n;
    c->resp_sent = 0;
    send_more(c);
}

// Constant body (flash-resident, any size): only the headers use resp[].
static void respond_static(conn_t *c, int code, const char *status,
                           const char *content_type, const char *body, size_t len) {
    int n = snprintf(c->resp, RESP_MAX, HDR_FMT, code, status, content_type,
                     (unsigned)len);
    c->resp_len = (uint16_t)n;
    c->resp_sent = 0;
    c->static_body = body;
    c->static_len = (uint16_t)len;
    c->static_sent = 0;
    send_more(c);
}

static void build_status_json(char *out, size_t cap) {
    telemetry_t t;
    ipc_snapshot_read(&t);
    uint32_t headroom = t.budget_mw > t.reserved_mw ? t.budget_mw - t.reserved_mw : 0;

    char ethf[64] = "";
#if PWRMAN_NET_ETH
    snprintf(ethf, sizeof(ethf), "\"eth\":\"%s\",", eth_status_str());
#endif
    size_t off = (size_t)snprintf(out, cap,
        "{\"name\":\"%s\",\"fw\":\"%s\",\"slot\":\"%s\",\"trial\":%s,"
        "\"uptime_s\":%lu,\"rssi\":%ld,%s"
        "\"total_w\":%.2f,\"reserved_w\":%.1f,\"budget_w\":%.1f,"
        "\"headroom_w\":%.1f,\"energy_kwh\":%.3f,\"fan\":\"%s\","
        "\"fan_mode\":\"%s\",\"alert\":%s,\"ble\":\"%s\",\"ports\":[",
        g_settings.device_name, FW_VERSION, flash_map_slot_name(),
        flash_map_update_pending() ? "true" : "false",
        (unsigned long)(to_ms_since_boot(get_absolute_time()) / 1000),
        (long)net_rssi(), ethf, t.total_mw / 1000.0, t.reserved_mw / 1000.0,
        t.budget_mw / 1000.0, headroom / 1000.0, t.energy_mwh / 1e6,
        t.fan_on ? "on" : "off",
        t.fan_auto ? "auto" : (t.fan_on ? "on" : "off"),
        t.alert_active ? "true" : "false", improv_state_str());

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

static bool bearer_present(const conn_t *c, const char *token) {
    const char *p = strstr(c->req, "Authorization: Bearer ");
    if (!p) return false;
    p += 22;
    size_t n = strlen(token);
    return n && !strncmp(p, token, n) &&
           (p[n] == '\r' || p[n] == '\n' || p[n] == ' ' || p[n] == '\0');
}

// Everything under POST /api/v1/: open until an API token is set.
static bool authorized(const conn_t *c) {
    if (!g_settings.api_token[0]) return true;
    return bearer_present(c, g_settings.api_token);
}

// ---- Setup secret: Improv hands the provisioning client http://<ip>/?s=<secret>
// so the same phone can finish first-time setup (broker, name, token) in the
// web UI without a serial cable. It stands in for the API token on /settings
// only, only while no token is stored, and only for SETUP_SECRET_TTL_MS;
// the token a setup request must set retires it. Written from improv_poll
// under the network lock, read here in lwIP's context.

static char setup_secret[9];
static uint32_t setup_until_ms; // 0 = none issued

const char *http_setup_secret_issue(uint32_t now_ms) {
    snprintf(setup_secret, sizeof(setup_secret), "%08lx", (unsigned long)get_rand_32());
    setup_until_ms = now_ms + SETUP_SECRET_TTL_MS;
    if (!setup_until_ms) setup_until_ms = 1;
    return setup_secret;
}

static bool setup_secret_live(void) {
    if (!setup_until_ms || g_settings.api_token[0]) return false;
    uint32_t now = to_ms_since_boot(get_absolute_time());
    return (int32_t)(now - setup_until_ms) < 0;
}

// /settings always needs a bearer: the token, or the live setup secret.
static bool settings_authorized(const conn_t *c, bool *via_setup) {
    *via_setup = false;
    if (g_settings.api_token[0]) return bearer_present(c, g_settings.api_token);
    if (setup_secret_live() && bearer_present(c, setup_secret)) {
        *via_setup = true;
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

static void build_settings_json(char *out, size_t cap, bool via_setup) {
    // static: escaping can grow a field sixfold, and this runs on the IRQ stack
    static char name[sizeof(g_settings.device_name) * 6];
    static char host[sizeof(g_settings.mqtt_host) * 6];
    static char user[sizeof(g_settings.mqtt_user) * 6];
    json_escape(name, sizeof(name), g_settings.device_name);
    json_escape(host, sizeof(host), g_settings.mqtt_host);
    json_escape(user, sizeof(user), g_settings.mqtt_user);
    telemetry_t t; // manual fan state is the engine's, not a setting
    ipc_snapshot_read(&t);
    snprintf(out, cap,
             "{\"name\":\"%s\",\"mqtt_host\":\"%s\",\"mqtt_port\":%u,\"mqtt_user\":\"%s\","
             "\"mqtt_pass_set\":%s,\"token_set\":%s,\"setup\":%s,"
             "\"budget_w\":%lu,\"fan_mode\":\"%s\",\"fan_on_w\":%u,\"fan_off_w\":%u,"
             "\"fan_on_ma\":%u}",
             name, host, g_settings.mqtt_port, user,
             g_settings.mqtt_pass[0] ? "true" : "false",
             g_settings.api_token[0] ? "true" : "false", via_setup ? "true" : "false",
             (unsigned long)(g_settings.budget_mw / 1000u),
             g_settings.fan_auto ? "auto" : (t.fan_on ? "on" : "off"),
             g_settings.fan_on_w, g_settings.fan_off_w, g_settings.fan_on_ma);
}

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
        if (!isalnum((unsigned char)*s) && *s != '-') return false;
    }
    return true;
}

// Any subset of the fields; absent ones keep their value. The whole record
// is validated into a copy first so a bad field changes nothing.
static void settings_post(conn_t *c, const char *body, bool via_setup) {
    static settings_t s; // static: a few hundred bytes, IRQ stack
    s = g_settings;
    const char *err = NULL;
    long port;
    int r;

    if ((r = json_get_str(body, "name", s.device_name, sizeof(s.device_name))) < 0)
        err = "name too long";
    else if (r > 0 && !valid_name(s.device_name))
        err = "name: letters, digits and hyphens only";
    else if ((r = json_get_str(body, "mqtt_host", s.mqtt_host, sizeof(s.mqtt_host))) < 0)
        err = "mqtt_host too long";
    else if (r > 0 && !header_safe(s.mqtt_host))
        err = "mqtt_host: no spaces or control characters";
    else if ((r = json_get_str(body, "mqtt_user", s.mqtt_user, sizeof(s.mqtt_user))) < 0)
        err = "mqtt_user too long";
    else if (r > 0 && !no_controls(s.mqtt_user))
        err = "mqtt_user: no control characters";
    else if ((r = json_get_str(body, "mqtt_pass", s.mqtt_pass, sizeof(s.mqtt_pass))) < 0)
        err = "mqtt_pass too long";
    else if (r > 0 && !no_controls(s.mqtt_pass))
        err = "mqtt_pass: no control characters";
    else if ((r = json_get_str(body, "token", s.api_token, sizeof(s.api_token))) < 0)
        err = "token too long";
    else if (r > 0 && !header_safe(s.api_token))
        err = "token: no spaces or control characters";
    else if (json_get_int(body, "mqtt_port", &port) && (port < 1 || port > 65535))
        err = "mqtt_port out of range";
    else if (via_setup && !s.api_token[0])
        err = "set an API token to finish setup";

    // operational fields: applied to the engine below, not just persisted
    long v;
    char mode[8];
    int m = json_get_str(body, "fan_mode", mode, sizeof(mode));
    bool fan_touched = m != 0, fan_manual_on = false;
    if (!err && json_get_int(body, "budget_w", &v)) {
        if (v < BUDGET_MIN_W || v > BUDGET_MAX_W) err = "budget_w out of range";
        else s.budget_mw = (uint32_t)v * 1000u;
    }
    if (!err && json_get_int(body, "fan_on_w", &v)) {
        if (v < 1 || v > 1000) err = "fan_on_w: 1-1000";
        else s.fan_on_w = (uint16_t)v;
        fan_touched = true;
    }
    if (!err && json_get_int(body, "fan_off_w", &v)) {
        if (v < 0 || v > 1000) err = "fan_off_w: 0-1000";
        else s.fan_off_w = (uint16_t)v;
        fan_touched = true;
    }
    if (!err && fan_touched && s.fan_off_w >= s.fan_on_w)
        err = "fan_off_w must be below fan_on_w";
    if (!err && json_get_int(body, "fan_on_ma", &v)) {
        if (v < 0 || v > 10000) err = "fan_on_ma: 0-10000 (0 disables)";
        else s.fan_on_ma = (uint16_t)v;
        fan_touched = true;
    }
    if (!err && m != 0) {
        if (m > 0 && !strcmp(mode, "auto")) {
            s.fan_auto = 1;
        } else if (m > 0 && (!strcmp(mode, "on") || !strcmp(mode, "off"))) {
            s.fan_auto = 0;
            fan_manual_on = mode[1] == 'n';
        } else {
            err = "fan_mode: auto, on or off";
        }
    }

    if (err) {
        char b[128];
        snprintf(b, sizeof(b), "{\"error\":\"%s\"}", err);
        respond(c, 400, "Bad Request", "application/json", b);
        return;
    }
    if (json_get_int(body, "mqtt_port", &port)) s.mqtt_port = (uint16_t)port;

    bool reboot_required = strcmp(g_settings.device_name, s.device_name) != 0 ||
                           strcmp(g_settings.mqtt_host, s.mqtt_host) != 0 ||
                           g_settings.mqtt_port != s.mqtt_port ||
                           strcmp(g_settings.mqtt_user, s.mqtt_user) != 0 ||
                           strcmp(g_settings.mqtt_pass, s.mqtt_pass) != 0;
    bool budget_changed = g_settings.budget_mw != s.budget_mw;
    g_settings = s;

    if (budget_changed) {
        engine_cmd_t cmd = {.op = CMD_SET_BUDGET, .arg = s.budget_mw};
        ipc_cmd_push(&cmd);
    }
    if (m != 0) { // an explicit mode: apply it (the CLI's fan on|off|auto)
        engine_cmd_t cmd = s.fan_auto ? (engine_cmd_t){.op = CMD_FAN_AUTO}
                                      : (engine_cmd_t){.op = CMD_FAN, .arg = fan_manual_on};
        ipc_cmd_push(&cmd);
    } else if (fan_touched && s.fan_auto) { // new thresholds under auto: re-arm
        engine_cmd_t cmd = {.op = CMD_FAN_AUTO};
        ipc_cmd_push(&cmd);
    }

    bool saved = settings_save(); // flash_safe_execute, as the OTA path does from here
    setup_until_ms = 0;           // a token now exists (or the caller had one)
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
    // static: lwIP calls this in IRQ context on core 0's 4KB stack, and the
    // engine's stack sits directly below it — a 1.6KB frame here plus printf's
    // float formatting was enough to overflow into it under concurrent load
    static char json[STATUS_JSON_MAX];

    if (!strncmp(c->req, "GET /", 5) && (c->req[5] == ' ' || c->req[5] == '?')) {
        // "/?s=<secret>" is the Improv redirect; the page reads the query itself
        respond_static(c, 200, "OK", "text/html", INDEX_HTML, sizeof(INDEX_HTML) - 1);
    } else if (!strncmp(c->req, "GET /api/v1/status", 18)) {
        build_status_json(json, sizeof(json));
        respond(c, 200, "OK", "application/json", json);
    } else if (!strncmp(c->req, "GET /api/v1/settings", 20) ||
               !strncmp(c->req, "POST /api/v1/settings", 21)) {
        bool via_setup;
        if (!settings_authorized(c, &via_setup)) {
            respond(c, 401, "Unauthorized", "application/json",
                    "{\"error\":\"bearer token required\"}");
            return;
        }
        if (c->req[0] == 'G') {
            build_settings_json(json, sizeof(json), via_setup);
            respond(c, 200, "OK", "application/json", json);
        } else {
            const char *body = strstr(c->req, "\r\n\r\n");
            settings_post(c, body ? body + 4 : "", via_setup);
        }
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
            // non-update bodies are small and usually ride in with the
            // headers; when Content-Length says the rest is still in flight
            // and req[] has room for it, wait for the next segment (a client
            // that never finishes is dropped by the idle poll)
            long cl = content_length(c->req);
            long have = (long)(c->req_len - (uint16_t)(hdr_end + 4 - c->req));
            if (!(cl > 0 && have < cl && c->req_len < REQ_MAX - 1)) handle_request(c);
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

// Every 500ms per connection. Two jobs sent_cb can't do: resume a response
// whose tcp_write failed with nothing in flight (sent_cb only fires once
// queued data is acked), and free a slot held by a client that never sends a
// request — browsers preconnect spare sockets, and with MAX_CONNS slots a few
// of those would lock everyone else out. An OTA body in progress is exempt;
// update_begin() has its own stale-transfer handling.
static err_t poll_cb(void *arg, struct tcp_pcb *pcb) {
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
    tcp_poll(newpcb, poll_cb, POLL_INTERVAL);
    tcp_err(newpcb, err_cb);
    return ERR_OK;
}

void http_init(void) {
    if (!net_available()) return;
    net_lock();
    struct tcp_pcb *pcb = tcp_new_ip_type(IPADDR_TYPE_ANY);
    if (pcb && tcp_bind(pcb, IP_ANY_TYPE, HTTP_PORT) == ERR_OK) {
        pcb = tcp_listen_with_backlog(pcb, 4);
        tcp_accept(pcb, accept_cb);
    } else {
        printf("http: failed to bind port %d\n", HTTP_PORT);
        if (pcb) tcp_abort(pcb);
    }
    net_unlock();
}
