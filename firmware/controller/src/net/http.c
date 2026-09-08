#include "http.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>


#include "lwip/tcp.h"
#include "pico/rand.h"

#include "boot_reason_hw.h"
#include "fault_log.h"
#include "fault_text.h"
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
#include "net.h"
#include "settings.h"
#include "settings_json.h"
#include "update.h"
#include "ups/lad_proto.h"
#include "ups/ups.h"

#define HTTP_PORT       80
#define MAX_CONNS       4
#define REQ_MAX         3072 // browser headers + a full settings export posted back
#define STATUS_JSON_MAX 2752 // six ports with escaped labels, the problem text and the UPS block, worst case
#define HDR_MAX         128  // the status line + our three headers
#define RESP_MAX        (STATUS_JSON_MAX + HDR_MAX)
#define POLL_INTERVAL   1    // tcp_poll units of 500ms
#define IDLE_POLLS      20   // drop a connection that sends no request in ~10s
#define REBOOT_DELAY_MS 300  // API reboot: let the response leave first
#define SETUP_SECRET_TTL_MS (10 * 60 * 1000)
#define STR_(x) #x
#define STR(x) STR_(x)

// one per-port "off when charged" checkbox in the settings panel
#define PORT_AUTOOFF_BOX(n) \
    "<label><input type='checkbox' name='pa" #n "'>Port " #n "</label>"

// one per-port power-up policy select in the settings panel
#define PORT_BOOT_SELECT(n) \
    "<select name='pb" #n "' title='Port " #n "'><option value='on'>on</option>" \
    "<option value='off'>off</option><option value='last'>last</option></select>"

// one per-port voltage cap select in the settings panel
#define PORT_VOLT_SELECT(n) \
    "<select name='pv" #n "' title='Port " #n "'><option value='5'>5 V</option>" \
    "<option value='9'>9 V</option><option value='12'>12 V</option>" \
    "<option value='15'>15 V</option><option value='20'>20 V</option></select>"

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
    bool static_copy;   // body is a reusable RAM buffer: lwIP must copy it
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
    "#prob{color:#f55;margin:-.3em 0 .8em;max-width:44em}#prob:empty{display:none}"
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
    "#msg,#flm,#lgm{color:#fc6;font-size:.85em;margin:.4em 0;min-height:1.2em}"
    "pre{white-space:pre-wrap;word-break:break-all;font-size:.75em;background:#0d0d0d;"
    "border:1px solid #333;padding:.6em;max-height:24em;overflow:auto;margin:.4em 0}"
    "#lock input{display:inline-block;width:14em;margin-right:.5em}"
    "#bk{margin-top:1em;border-top:1px solid #333;padding-top:.4em}"
    "#bk input[type=checkbox]{display:inline;width:auto;margin-right:.4em}"
    "#ao label{margin:0;color:#eee}#ao input{display:inline;width:auto;margin-right:.4em}"
    "@media(max-width:40em){body{margin:1em .6em}td,th{padding:.4em .35em}"
    ".g{grid-template-columns:1fr}}"
    "</style></head><body>"
    "<h1>Power Manifold</h1><div id='chassis'>loading&hellip;</div><div id='prob'></div>"
    "<table><thead><tr><th>Port</th><th>State</th><th>V</th><th>A</th>"
    "<th title='measured by the INA226'>Draw W</th>"
    "<th title='PD contract wattage held against the chassis budget'>Res W</th>"
    "<th>kWh</th></tr></thead><tbody id='ports'></tbody></table>"
    "<div id='hint'>Click a port for its last 10 minutes of W / A / V, sampled"
    " once a second while this page is open.</div>"
    // Persistent fault log (data partition): newest first, refreshed while open.
    "<details id='fl'><summary>Fault log</summary><div id='flm'></div>"
    "<table id='flt' hidden><thead><tr><th>When</th><th>Port</th><th>Event</th>"
    "<th title='draw / contract at the moment of the event'>W then</th></tr></thead>"
    "<tbody></tbody></table><button type='button' id='flc'>Clear log</button></details>"
    // Console ring (last 4 KB of what the firmware printed); needs the token.
    "<details id='lg'><summary>Console log</summary><div id='lgm'></div><pre id='lgp'></pre>"
    "<button type='button' id='lgr'>Refresh</button></details>"
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
    "<label>Addressing (wired link if a W6100 is fitted, else WiFi; applies at reboot)"
    "<select name='ipmode'><option value='dhcp'>DHCP</option>"
    "<option value='static'>static</option></select></label>"
    "<div class='g'><label>IP address<input name='ip' placeholder='10.0.0.20'></label>"
    "<label>Netmask<input name='mask' placeholder='255.255.255.0'></label>"
    "<label>Gateway<input name='gw' placeholder='10.0.0.1'></label></div>"
    "<label>DNS server (blank = from DHCP, or the gateway when static; applies at once)"
    "<input name='dns' placeholder='10.0.0.1'></label>"
    "<label>Syslog host (blank = off; the console is mirrored there as RFC 5424 over UDP)"
    "<input name='slh' maxlength='63'></label>"
    "<label>Syslog port<input name='slp' type='number' min='1' max='65535'></label>"
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
    "<label>Status LED brightness (0-255, 0 = off but faults still show)"
    "<input name='led' type='number' min='0' max='255' required></label>"
    "<label>Power-up LED sweep<select name='lboot'><option value='white'>white</option>"
    "<option value='rainbow'>rainbow</option></select></label>"
    "<label>Dimmed LED brightness (night window or idle)"
    "<input name='ldim' type='number' min='0' max='255' required></label>"
    "<label>Night window, local time (both blank = off; may wrap midnight)</label>"
    "<div class='g'><input name='lns' type='time'><input name='lne' type='time'></div>"
    "<label>Dim after this long without a port event (minutes, 0 = never)"
    "<input name='lidle' type='number' min='0' max='1440' required></label>"
    "<label>UTC offset in minutes for local time (e.g. -240 for UTC-4)"
    "<input name='tz' type='number' min='-720' max='840' required></label>"
    "<label>Port names (blank = Port N; shown in the table and Home Assistant)</label>"
    "<div class='g'><input name='pn1' maxlength='23' placeholder='Port 1'>"
    "<input name='pn2' maxlength='23' placeholder='Port 2'>"
    "<input name='pn3' maxlength='23' placeholder='Port 3'>"
    "<input name='pn4' maxlength='23' placeholder='Port 4'>"
    "<input name='pn5' maxlength='23' placeholder='Port 5'>"
    "<input name='pn6' maxlength='23' placeholder='Port 6'></div>"
    "<label>Port current limits (mA, " STR(PORT_LIMIT_MIN_MA) "-" STR(PORT_LIMIT_MAX_MA) ": the current every PDO"
    " advertises, so the watt ceiling scales with the voltage the device picks)</label>"
    "<div class='g'><input name='pl1' type='number' min='" STR(PORT_LIMIT_MIN_MA) "' max='" STR(PORT_LIMIT_MAX_MA) "' step='20' required>"
    "<input name='pl2' type='number' min='" STR(PORT_LIMIT_MIN_MA) "' max='" STR(PORT_LIMIT_MAX_MA) "' step='20' required>"
    "<input name='pl3' type='number' min='" STR(PORT_LIMIT_MIN_MA) "' max='" STR(PORT_LIMIT_MAX_MA) "' step='20' required>"
    "<input name='pl4' type='number' min='" STR(PORT_LIMIT_MIN_MA) "' max='" STR(PORT_LIMIT_MAX_MA) "' step='20' required>"
    "<input name='pl5' type='number' min='" STR(PORT_LIMIT_MIN_MA) "' max='" STR(PORT_LIMIT_MAX_MA) "' step='20' required>"
    "<input name='pl6' type='number' min='" STR(PORT_LIMIT_MIN_MA) "' max='" STR(PORT_LIMIT_MAX_MA) "' step='20' required></div>"
    "<label>Voltage cap: the highest PDO each port advertises (20 V = the whole table; a live port"
    " renegotiates at once)</label>"
    "<div class='g'>" PORT_VOLT_SELECT(1) PORT_VOLT_SELECT(2) PORT_VOLT_SELECT(3)
    PORT_VOLT_SELECT(4) PORT_VOLT_SELECT(5) PORT_VOLT_SELECT(6) "</div>"
    "<label>Charged = a sink drawing under (mW; 0 = detection off)"
    "<input name='chmw' type='number' min='0' max='20000' step='50' required></label>"
    "<label>&hellip;for this long (minutes)"
    "<input name='chmin' type='number' min='1' max='255' required></label>"
    "<label>Switch off once charged</label>"
    "<div class='g' id='ao'>" PORT_AUTOOFF_BOX(1) PORT_AUTOOFF_BOX(2) PORT_AUTOOFF_BOX(3)
    PORT_AUTOOFF_BOX(4) PORT_AUTOOFF_BOX(5) PORT_AUTOOFF_BOX(6) "</div>"
    "<label>Sleep timer: switch off this many minutes after a sink attaches (0 = never)</label>"
    "<div class='g'><input name='ps1' type='number' min='0' max='" STR(PORT_SLEEP_MAX_MIN) "' required>"
    "<input name='ps2' type='number' min='0' max='" STR(PORT_SLEEP_MAX_MIN) "' required>"
    "<input name='ps3' type='number' min='0' max='" STR(PORT_SLEEP_MAX_MIN) "' required>"
    "<input name='ps4' type='number' min='0' max='" STR(PORT_SLEEP_MAX_MIN) "' required>"
    "<input name='ps5' type='number' min='0' max='" STR(PORT_SLEEP_MAX_MIN) "' required>"
    "<input name='ps6' type='number' min='0' max='" STR(PORT_SLEEP_MAX_MIN) "' required></div>"
    "<label>Port state at power-up (last = however it was switched, from any surface)</label>"
    "<div class='g'>" PORT_BOOT_SELECT(1) PORT_BOOT_SELECT(2) PORT_BOOT_SELECT(3)
    PORT_BOOT_SELECT(4) PORT_BOOT_SELECT(5) PORT_BOOT_SELECT(6) "</div>"
    "<label>API token (locks the API and this panel)"
    "<input name='atok' type='password' maxlength='32'></label>"
    "<button type='submit'>Save</button>"
    "<button type='button' id='rb'>Reboot</button>"
    // Backup: the export is the settings object; importing posts it back.
    "<div id='bk'><label><input type='checkbox' id='xps'> include the WiFi and MQTT"
    " passwords and the API token in the export</label>"
    "<button type='button' id='xpb'>Export</button>"
    "<button type='button' id='imb'>Import&hellip;</button>"
    "<input type='file' id='imf' accept='.json,application/json' hidden></div></form></details>"
    "<script>"
    // Per-port sample rings live in the page: the 1 Hz status poll already
    // carries V/A/W, so history costs the firmware nothing. Sparklines are
    // inline SVG — the device is LAN-only, so no chart library from a CDN.
    "const N=600,H=[],ports=document.getElementById('ports'),"
    "esc=s=>s.replace(/[&<>]/g,c=>'&#'+c.charCodeAt(0)+';');let sel=-1,last;"
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
    "`<tr class='p${i==sel?' sel':''}' data-i='${i}'><td title='Port ${i+1}'>${esc(p.name)}</td>"
    "<td class='s-${p.state}'>${p.state}${p.charged?' &middot; charged':''}</td>"
    "<td>${p.v.toFixed(2)}</td><td>${p.i.toFixed(2)}</td><td>${p.p.toFixed(1)}</td>"
    "<td>${p.contract_w.toFixed(0)}</td><td>${p.e.toFixed(3)}</td></tr>`+"
    "(i==sel?`<tr class='d'><td colspan='7'><div class='g'>${line('p','W',1)}"
    "${line('i','A',2)}${line('v','V',2)}</div></td></tr>`:'')).join('');}"
    "async function tick(){try{"
    "const r=await fetch('/api/v1/status');const d=await r.json();last=d;"
    "document.getElementById('chassis').textContent="
    "`${d.name} \\u2014 ${d.total_w.toFixed(1)}W drawn, ${d.reserved_w.toFixed(0)}W"
    " reserved of ${d.budget_w.toFixed(0)}W budget"
    " (${d.headroom_w.toFixed(0)}W free) \\u2014 fan ${d.fan} \\u2014 fw ${d.fw}"
    " \\u2014 last boot ${d.boot}${d.led_mode!='normal'?' \\u2014 LEDs dimmed ('+d.led_mode+')':''}"
    "${d.ups&&d.ups.present?' \\u2014 UPS '+d.ups.status:''}`;"
    "document.getElementById('prob').textContent=d.problems?'\\u26a0 '+d.problems:'';"
    "d.ports.forEach((p,i)=>{(H[i]=H[i]||[]).push({v:p.v,i:p.i,p:p.p});"
    "if(H[i].length>N)H[i].shift();});"
    "draw();}catch(e){}}tick();setInterval(tick,1000);"
    // Settings panel. The bearer lives in sessionStorage for this tab only;
    // a ?s=<secret> from Improv's redirect seeds it and is scrubbed from the
    // address bar. Blank password/token fields mean "unchanged".
    "const CFG=document.getElementById('cfg'),F=document.getElementById('f'),"
    "M=document.getElementById('msg'),LK=document.getElementById('lock'),"
    "PN=[...document.querySelectorAll('input[name^=pn]')],"
    "PL=[...document.querySelectorAll('input[name^=pl]')],"
    "PB=[...document.querySelectorAll('select[name^=pb]')],"
    "PV=[...document.querySelectorAll('select[name^=pv]')],"
    "PA=[...document.querySelectorAll('input[name^=pa]')],"
    "PS=[...document.querySelectorAll('input[name^=ps]')],"
    "KEYS={dname:'name',mhost:'mqtt_host',mport:'mqtt_port',muser:'mqtt_user',bud:'budget_w',"
    "fmode:'fan_mode',fon:'fan_on_w',foff:'fan_off_w',fma:'fan_on_ma',"
    "led:'led_brightness',lboot:'led_boot',ipmode:'ip_mode',ip:'ip',mask:'netmask',"
    "gw:'gateway',dns:'dns',slh:'syslog_host',slp:'syslog_port',chmw:'charged_mw',"
    "chmin:'charged_min',ldim:'led_dim',lidle:'led_idle_min',tz:'tz_offset_min'},"
    "NUM={mport:1,bud:1,fon:1,foff:1,fma:1,led:1,slp:1,chmw:1,chmin:1,ldim:1,lidle:1,tz:1},"
    "hdr=()=>sessionStorage.tok?{Authorization:'Bearer '+sessionStorage.tok}:{};"
    "async function cfgLoad(){let r;"
    "try{r=await fetch('/api/v1/settings',{headers:hdr()});}"
    "catch(e){M.textContent='No response from the device.';return;}"
    "if(r.status==401){LK.hidden=false;F.hidden=true;"
    "M.textContent=(sessionStorage.tok?'Token rejected. ':'')+"
    "'Enter the API token to edit settings.';return;}"
    "const d=await r.json();LK.hidden=true;F.hidden=false;"
    "for(const k in KEYS)F[k].value=d[KEYS[k]];PN.forEach((e,i)=>e.value=d.port_names[i]||'');"
    "PL.forEach((e,i)=>e.value=d.port_limits_ma[i]);PB.forEach((e,i)=>e.value=d.port_boot[i]);"
    "PV.forEach((e,i)=>e.value=d.port_max_v[i]);"
    "PA.forEach((e,i)=>e.checked=!!d.port_auto_off[i]);PS.forEach((e,i)=>e.value=d.port_sleep_min[i]);"
    "const NW=(d.led_night||'').split('-');F.lns.value=NW[0]||'';F.lne.value=NW[1]||'';"
    "F.mpass.placeholder=d.mqtt_pass_set?'(unchanged)':'(none)';"
    "F.atok.placeholder=d.token_set?'(unchanged)':'required';F.atok.required=!d.token_set;"
    "M.textContent=d.token_set?'':'Setup: choose an API token to finish. It locks"
    " the API and this panel, so keep a copy.';}"
    "F.onsubmit=async e=>{e.preventDefault();const b={};"
    "for(const k in KEYS)b[KEYS[k]]=NUM[k]?+F[k].value:F[k].value;b.mqtt_port=b.mqtt_port||1883;"
    "b.port_names=PN.map(e=>e.value.trim());b.port_limits_ma=PL.map(e=>+e.value);"
    "b.port_boot=PB.map(e=>e.value);b.port_max_v=PV.map(e=>+e.value);"
    "b.port_auto_off=PA.map(e=>e.checked?1:0);"
    "b.port_sleep_min=PS.map(e=>+e.value);"
    "b.led_night=F.lns.value&&F.lne.value?F.lns.value+'-'+F.lne.value:'';"
    "if(F.mpass.value)b.mqtt_pass=F.mpass.value;if(F.atok.value)b.token=F.atok.value;"
    "let r,d={};try{r=await fetch('/api/v1/settings',{method:'POST',"
    "headers:{...hdr(),'Content-Type':'application/json'},body:JSON.stringify(b)});"
    "d=await r.json();}catch(e){}"
    "if(!r||!r.ok){M.textContent='Not saved: '+(d.error||'no response');return;}"
    "if(b.token)sessionStorage.tok=b.token;F.mpass.value=F.atok.value='';"
    "await cfgLoad();M.textContent=d.reboot_required?"
    "'Saved. Reboot to apply the name, broker and addressing.':'Saved and applied.';};"
    "document.getElementById('rb').onclick=async()=>{"
    "try{await fetch('/api/v1/reboot',{method:'POST',headers:hdr()});}catch(e){}"
    "M.textContent='Rebooting; this page reloads in a few seconds.';"
    "setTimeout(()=>location.reload(),6000);};"
    "document.getElementById('xpb').onclick=async()=>{let r;"
    "try{r=await fetch('/api/v1/settings/export'+(document.getElementById('xps').checked?"
    "'?secrets=1':''),{headers:hdr()});}catch(e){}"
    "if(!r||!r.ok){M.textContent='Export failed'+(r?' ('+r.status+')':'')+'.';return;}"
    "const a=document.createElement('a');a.href=URL.createObjectURL(await r.blob());"
    "a.download=(F.dname.value||'pwrman')+'-settings.json';a.click();"
    "URL.revokeObjectURL(a.href);M.textContent='Exported.';};"
    "const IMF=document.getElementById('imf');"
    "document.getElementById('imb').onclick=()=>IMF.click();"
    "IMF.onchange=async()=>{const f=IMF.files[0];if(!f)return;const t=await f.text();"
    "IMF.value='';let r,d={};try{r=await fetch('/api/v1/settings',{method:'POST',"
    "headers:{...hdr(),'Content-Type':'application/json'},body:t});d=await r.json();}catch(e){}"
    "if(!r||!r.ok){M.textContent='Not imported: '+(d.error||'no response');return;}"
    "await cfgLoad();M.textContent=d.reboot_required?"
    "'Imported. Reboot to apply the name, network and broker.':'Imported and applied.';};"
    "document.getElementById('ul').onclick=()=>{"
    "sessionStorage.tok=document.getElementById('tok').value;cfgLoad();};"
    "CFG.ontoggle=()=>{if(CFG.open)cfgLoad();};"
    "const U=new URL(location),S=U.searchParams.get('s');"
    "if(S){sessionStorage.tok=S;U.searchParams.delete('s');"
    "history.replaceState(null,'',U);CFG.open=true;}"
    // Console log panel: the ring as text, bottom = newest.
    "const LG=document.getElementById('lg'),LGP=document.getElementById('lgp'),"
    "LGM=document.getElementById('lgm');"
    "async function lgLoad(){let r;try{r=await fetch('/api/v1/log',{headers:hdr()});}"
    "catch(e){LGM.textContent='No response from the device.';return;}"
    "if(r.status==401){LGM.textContent='Unlock the Settings panel with the API token first.';"
    "LGP.textContent='';return;}"
    "LGP.textContent=await r.text();LGM.textContent='';LGP.scrollTop=LGP.scrollHeight;}"
    "LG.ontoggle=()=>{if(LG.open)lgLoad();};document.getElementById('lgr').onclick=lgLoad;"
    // Fault log panel: newest page of records, human text from the firmware.
    "const FL=document.getElementById('fl'),FLM=document.getElementById('flm'),"
    "FLT=document.getElementById('flt');"
    "const at=f=>f.epoch?new Date(f.epoch*1000).toLocaleString():'up '+f.uptime_s+'s';"
    "async function flLoad(){let d;try{d=await (await fetch('/api/v1/faults')).json();}"
    "catch(e){FLM.textContent='No response from the device.';return;}"
    "if(!d.available){FLM.textContent='No fault log on this board (needs the data partition).';"
    "FLT.hidden=true;return;}"
    "FLM.textContent=d.count?`${d.count} record${d.count==1?'':'s'}, newest first`+"
    "(d.count>d.faults.length?` (showing ${d.faults.length})`:''):'No faults recorded.';"
    "FLT.hidden=!d.count;FLT.tBodies[0].innerHTML=d.faults.map(f=>`<tr><td>${at(f)}</td>"
    "<td>${f.port||'chassis'}</td><td>${esc(f.text)}</td><td>${f.type=='boot'?'':"
    "f.power_w.toFixed(1)+' / '+f.contract_w.toFixed(0)}</td></tr>`).join('');}"
    "FL.ontoggle=()=>{if(FL.open)flLoad();};setInterval(()=>{if(FL.open)flLoad();},10000);"
    "document.getElementById('flc').onclick=async()=>{let r;"
    "try{r=await fetch('/api/v1/faults/clear',{method:'POST',headers:hdr()});}catch(e){}"
    "FLM.textContent=r&&r.ok?'Cleared.':r&&r.status==401?"
    "'Unlock Settings with the API token first.':'Clear failed.';if(r&&r.ok)flLoad();};"
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
    c->static_copy = false;
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
        uint16_t room = tcp_sndbuf(c->pcb);
        if (room == 0) return false;
        if (chunk > room) chunk = room;
        if (chunk > TCP_MSS) chunk = TCP_MSS;
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
    // resp[] is reused per request, so lwIP must copy it; a static body may
    // be referenced in place where the netif allows it (see send_span)
    bool done = send_span(c, c->resp, c->resp_len, &c->resp_sent, TCP_WRITE_FLAG_COPY) &&
                send_span(c, c->static_body, c->static_len, &c->static_sent,
                          c->static_copy ? TCP_WRITE_FLAG_COPY : 0);
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
// copy: body is a RAM buffer that will be rewritten later (lwIP copies it as
// it goes, so the buffer is free once the whole body is queued); otherwise
// it lives in flash for good and lwIP may reference it in place (on the
// CYW43 netif it copies regardless, see send_span).
static void respond_static(conn_t *c, int code, const char *status,
                           const char *content_type, const char *body, size_t len,
                           bool copy) {
    int n = snprintf(c->resp, RESP_MAX, HDR_FMT, code, status, content_type,
                     (unsigned)len);
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
    static char upsf[320]; // static: IRQ stack
    build_ups_json(upsf, sizeof(upsf));
    size_t off = (size_t)snprintf(out, cap,
        "{\"name\":\"%s\",\"fw\":\"%s\",\"slot\":\"%s\",\"trial\":%s,"
        "\"uptime_s\":%lu,\"rssi\":%ld,%s"
        "\"total_w\":%.2f,\"reserved_w\":%.1f,\"budget_w\":%.1f,"
        "\"headroom_w\":%.1f,\"energy_kwh\":%.3f,\"fan\":\"%s\","
        "\"fan_mode\":\"%s\",\"alert\":%s,\"ble\":\"%s\",\"boot\":\"%s\","
        "\"problem\":%s,\"problems\":\"%s\",\"led_mode\":\"%s\",\"led_now\":%u,%s\"ports\":[",
        g_settings.device_name, FW_VERSION, flash_map_slot_name(),
        flash_map_update_pending() ? "true" : "false",
        (unsigned long)(to_ms_since_boot(get_absolute_time()) / 1000),
        (long)net_rssi(), ethf, t.total_mw / 1000.0, t.reserved_mw / 1000.0,
        t.budget_mw / 1000.0, headroom / 1000.0, t.energy_mwh / 1e6,
        t.fan_on ? "on" : "off",
        t.fan_auto ? "auto" : (t.fan_on ? "on" : "off"),
        t.alert_active ? "true" : "false", improv_state_str(), boot_text,
        n_problems ? "true" : "false", problems_json, led_mode_name(led_sched_current()),
        led_sched_level(&g_settings, led_sched_current()), upsf);

    for (int i = 0; i < NUM_PORTS && off < cap; i++) {
        const port_telemetry_t *p = &t.port[i];
        static char pn[PORT_NAME_MAX * 6 + 1]; // static: IRQ stack
        json_escape(pn, sizeof(pn), settings_port_name((unsigned)i));
        off += (size_t)snprintf(out + off, cap - off,
            "%s{\"name\":\"%s\",\"state\":\"%s\",\"attached\":%s,\"charged\":%s,\"pdo\":%u,"
            "\"v\":%.3f,\"i\":%.3f,\"p\":%.2f,\"e\":%.3f,\"contract_w\":%.1f,\"prio\":%u,"
            "\"limit_ma\":%lu,\"max_v\":%u,\"boot\":\"%s\",\"fault\":%u}",
            i ? "," : "", pn, port_state_name((port_state_t)p->state),
            p->attached ? "true" : "false", p->charged ? "true" : "false", p->selected_pdo,
            p->bus_mv / 1000.0, p->current_ma / 1000.0, p->power_mw / 1000.0,
            p->energy_mwh / 1e6, p->contract_mw / 1000.0, g_settings.port_priority[i],
            (unsigned long)g_settings.port_limit_ma[i], g_settings.port_max_mv[i] / 1000,
            settings_port_boot_name(g_settings.port_boot[i]), p->fault_bits);
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
                         : r.type == EVT_BOOT ? "boot" : "?";
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
// Too big for a conn's resp[] (six ports of labelled gauges), so it is built
// in one shared buffer and streamed with the copy flag; a second scrape while
// one is still being queued gets a 503 rather than a torn buffer.
#define METRICS_MAX 8192
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
    return off < cap ? off : 0;
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

    bool reboot_required = strcmp(g_settings.device_name, s.device_name) != 0 ||
                           strcmp(g_settings.wifi_ssid, s.wifi_ssid) != 0 ||
                           strcmp(g_settings.wifi_pass, s.wifi_pass) != 0 ||
                           strcmp(g_settings.mqtt_host, s.mqtt_host) != 0 ||
                           g_settings.mqtt_port != s.mqtt_port ||
                           strcmp(g_settings.mqtt_user, s.mqtt_user) != 0 ||
                           strcmp(g_settings.mqtt_pass, s.mqtt_pass) != 0 ||
                           g_settings.ip_static != s.ip_static ||
                           g_settings.ip_addr != s.ip_addr || g_settings.ip_mask != s.ip_mask ||
                           g_settings.ip_gw != s.ip_gw; // dns and syslog apply live
    bool budget_changed = g_settings.budget_mw != s.budget_mw;
    bool led_changed = g_settings.led_brightness != s.led_brightness;
    bool names_changed = memcmp(g_settings.port_name, s.port_name, sizeof(s.port_name)) != 0;
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
            respond(c, 401, "Unauthorized", "application/json",
                    "{\"error\":\"bearer token required\"}");
        } else if (log_busy()) {
            respond(c, 503, "Service Unavailable", "text/plain", "log busy, retry\n");
        } else {
            size_t n = log_sink_snapshot(log_buf, sizeof(log_buf));
            respond_static(c, 200, "OK", "text/plain; charset=utf-8", log_buf, n, true);
        }
    } else if (!strncmp(c->req, "GET /api/v1/faults", 18)) {
        int offset = 0;
        const char *q = strstr(c->req, "?offset=");
        if (q) offset = atoi(q + 8);
        build_faults_json(json, sizeof(json), offset < 0 ? 0 : offset);
        respond(c, 200, "OK", "application/json", json);
    } else if (!strncmp(c->req, "GET /api/v1/settings/export", 27)) {
        bool via_setup;
        if (!settings_authorized(c, &via_setup)) {
            respond(c, 401, "Unauthorized", "application/json",
                    "{\"error\":\"bearer token required\"}");
            return;
        }
        bool secrets = !strncmp(c->req + 27, "?secrets=1 ", 11);
        build_settings_json(json, sizeof(json), via_setup, secrets, true);
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
            build_settings_json(json, sizeof(json), via_setup, false, false);
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
            bool queued = ipc_cmd_push(&cmd);
            if (queued && (cmd.op == CMD_PORT_ENABLE || cmd.op == CMD_PORT_DISABLE) &&
                settings_port_admin_note(port - 1, cmd.op == CMD_PORT_ENABLE))
                settings_save_later(); // the "last" boot policy keeps it
            respond(c, queued ? 200 : 503, "OK", "application/json", "{\"ok\":true}");
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
