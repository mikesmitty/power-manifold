#include "net.h"

#include <stdio.h>
#include <string.h>

#if PWRMAN_NET_WIFI
#include "pico/cyw43_arch.h"
#else
#include "pico/async_context_threadsafe_background.h"
#include "pico/lwip_nosys.h"
#endif

#include "lwip/apps/mdns.h"
#include "lwip/apps/sntp.h"
#include "lwip/dhcp.h"
#include "lwip/dns.h"
#include "lwip/netif.h"

#include "eth.h"
#include "improv.h"
#include "ip4_text.h"
#include "settings.h"

#define RECONNECT_INTERVAL_MS (10 * 1000)

static async_context_t *ctx;   // lwIP's home; NULL until net_init succeeds
static struct netif *active;   // netif holding the default route
static bool services_started;
static bool eth_mdns;
static bool static_on_wifi; // settings ip_static and no W6100: WiFi carries the address
static uint32_t epoch_at_boot; // unix time minus ms_since_boot/1000

#if PWRMAN_NET_WIFI
static bool wifi_link;
static bool wifi_mdns;
static uint32_t last_connect_ms;

static void start_connect(void) {
    cyw43_arch_wifi_connect_async(g_settings.wifi_ssid,
                                  g_settings.wifi_pass[0] ? g_settings.wifi_pass : NULL,
                                  CYW43_AUTH_WPA2_MIXED_PSK);
}

static struct netif *wifi_netif(void) {
    return &cyw43_state.netif[CYW43_ITF_STA];
}

static bool wifi_up(void) {
    return ctx && cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA) == CYW43_LINK_UP;
}
#else
static bool wifi_up(void) {
    return false;
}
#endif

void net_init(void) {
#if PWRMAN_NET_WIFI
    if (cyw43_arch_init()) {
        printf("net: cyw43 init failed; running without network\n");
        return;
    }
    ctx = cyw43_arch_async_context();
    cyw43_arch_enable_sta_mode();

    net_lock();
    netif_set_hostname(wifi_netif(), g_settings.device_name);
    net_unlock();
#else
    static async_context_threadsafe_background_t bg;
    if (!async_context_threadsafe_background_init_with_defaults(&bg) ||
        !lwip_nosys_init(&bg.core)) {
        printf("net: lwIP init failed; running without network\n");
        return;
    }
    ctx = &bg.core;
#endif

    eth_init(); // probes the W6100; absent or not built in is fine. A static
                // address goes to the wired link when the chip answers.

#if PWRMAN_NET_WIFI
    if (g_settings.ip_static && !eth_present()) {
        // the driver started DHCP with STA mode; a static address replaces it
        // and, with no DHCP client left on the netif, survives every rejoin
        static_on_wifi = true;
        ip4_addr_t ip = {.addr = g_settings.ip_addr}, mask = {.addr = g_settings.ip_mask},
                   gw = {.addr = g_settings.ip_gw};
        net_lock();
        dhcp_release_and_stop(wifi_netif());
        netif_set_addr(wifi_netif(), &ip, &mask, &gw);
        net_unlock();
        printf("net: wifi static address %s\n", ip4addr_ntoa(&ip));
    }

    if (g_settings.wifi_ssid[0]) {
        start_connect();
    } else {
        printf("net: no WiFi credentials; use CLI 'wifi <ssid> [pass]' or Improv over BLE\n");
    }
    last_connect_ms = 0;

    improv_init(); // BLE provisioning shares the radio; advertises on demand
#endif
}

// The resolver the settings ask for: the override, else in static mode the
// gateway (nobody else would set one), else 0 = leave DHCP's alone.
static uint32_t wanted_dns(void) {
    if (g_settings.ip_dns) return g_settings.ip_dns;
    if (g_settings.ip_static) return g_settings.ip_gw;
    return 0;
}

// mDNS/SNTP once, plus each netif announced the first time it comes up
static void announce(struct netif *n, bool *done) {
    if (*done) return;
    if (!services_started) {
        mdns_resp_init();
        sntp_setoperatingmode(SNTP_OPMODE_POLL);
        sntp_setservername(0, "pool.ntp.org");
        sntp_init();
        services_started = true;
    }
    mdns_resp_add_netif(n, g_settings.device_name);
    mdns_resp_add_service(n, g_settings.device_name, "_http", DNSSD_PROTO_TCP, 80, NULL, NULL);
    *done = true;
}

void net_poll(uint32_t now_ms) {
    if (!ctx) return;
    struct netif *wifi_n = NULL, *eth_n = NULL;

#if PWRMAN_NET_WIFI
    if (g_settings.wifi_ssid[0]) {
        int status = cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA);
        bool up = status == CYW43_LINK_UP;
        if (up && !wifi_link)
            printf("net: wifi up, ip %s\n", ip4addr_ntoa(netif_ip4_addr(wifi_netif())));
        else if (!up && wifi_link)
            printf("net: wifi down\n");
        wifi_link = up;
        if (up) wifi_n = wifi_netif();

        // retry on explicit failure or lingering downtime
        if (!up && (status < 0 || now_ms - last_connect_ms > RECONNECT_INTERVAL_MS)) {
            last_connect_ms = now_ms;
            start_connect();
        }
    }
#else
    (void)now_ms;
#endif
    if (eth_up()) eth_n = eth_netif_ptr();

    struct netif *best = eth_n ? eth_n : wifi_n; // wired wins while it has a link
    net_lock();
#if PWRMAN_NET_WIFI
    if (wifi_n) announce(wifi_n, &wifi_mdns);
#endif
    if (eth_n) announce(eth_n, &eth_mdns);
    if (best != active) {
        active = best;
        if (best) {
            netif_set_default(best);
            printf("net: default route via %s, ip %s\n", best == eth_n ? "ethernet" : "wifi",
                   ip4addr_ntoa(netif_ip4_addr(best)));
        } else {
            printf("net: no link\n");
        }
    }
    // a configured resolver always wins: DHCP rewrites the servers on every
    // bind and renewal, so put ours back whenever it has been displaced
    uint32_t dns = wanted_dns();
    if (dns && ip_2_ip4(dns_getserver(0))->addr != dns) {
        ip_addr_t server;
        ip_addr_set_ip4_u32(&server, dns);
        dns_setserver(0, &server);
    }
    net_unlock();
}

bool net_static_on_wifi(void) {
    return static_on_wifi;
}

const char *net_dns_str(void) {
    if (!ctx) return "none";
    const ip_addr_t *s = dns_getserver(0);
    if (ip_addr_isany(s)) return "none";
    return ipaddr_ntoa(s);
}

bool net_ip4_parse(const char *s, uint32_t *addr_nbo) {
    return ip4_parse(s, addr_nbo);
}

const char *net_ip4_str(uint32_t addr_nbo) {
    static char buf[16];
    ip4_format(buf, sizeof(buf), addr_nbo);
    return buf;
}

bool net_ip4_mask_valid(uint32_t mask_nbo) {
    return ip4_mask_valid(mask_nbo);
}

// lwIP hook (lwip_hooks.h): a packet with a source address that belongs to
// one of our netifs leaves through that netif
struct netif *net_ip4_route_src(const ip4_addr_t *src, const ip4_addr_t *dest) {
    (void)dest;
    struct netif *n;
    NETIF_FOREACH(n) {
        if (netif_is_up(n) && netif_is_link_up(n) && ip4_addr_eq(netif_ip4_addr(n), src))
            return n;
    }
    return NULL;
}

bool net_available(void) {
    return ctx != NULL;
}

bool net_up(void) {
    return eth_up() || wifi_up();
}

// Live link state rather than net_poll's bookkeeping: callers that run
// before net_poll in the same pass (improv's redirect URL) would otherwise
// see 0.0.0.0 on the pass a link came up.
static struct netif *preferred_netif(void) {
    if (eth_up()) return eth_netif_ptr();
#if PWRMAN_NET_WIFI
    if (wifi_up()) return wifi_netif();
#endif
    return NULL;
}

const char *net_ip_str(void) {
    struct netif *n = preferred_netif();
    return n ? ip4addr_ntoa(netif_ip4_addr(n)) : "0.0.0.0";
}

// own buffers: ip4addr_ntoa's single static one would make two of these in
// one printf show the same value
const char *net_mask_str(void) {
    static char buf[16];
    struct netif *n = preferred_netif();
    return n ? ip4addr_ntoa_r(netif_ip4_netmask(n), buf, sizeof(buf)) : "";
}

const char *net_gw_str(void) {
    static char buf[16];
    struct netif *n = preferred_netif();
    return n ? ip4addr_ntoa_r(netif_ip4_gw(n), buf, sizeof(buf)) : "";
}

int32_t net_rssi(void) {
    int32_t rssi = 0;
#if PWRMAN_NET_WIFI
    if (wifi_up()) cyw43_wifi_get_rssi(&cyw43_state, &rssi);
#endif
    return rssi;
}

void net_reconnect(void) {
#if PWRMAN_NET_WIFI
    if (!ctx || !g_settings.wifi_ssid[0]) return;
    last_connect_ms = to_ms_since_boot(get_absolute_time());
    // Drop the current association first: a join to the same SSID with a
    // different key otherwise rides the existing session for ~25 s before
    // the AP rejects it, and the link never visibly goes down for a while.
    cyw43_wifi_leave(&cyw43_state, CYW43_ITF_STA);
    start_connect();
#endif
}

int net_link_status(void) {
#if PWRMAN_NET_WIFI
    if (!ctx) return CYW43_LINK_FAIL;
    return cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA);
#else
    return -1;
#endif
}

void net_lock(void) {
    if (ctx) async_context_acquire_lock_blocking(ctx);
}

void net_unlock(void) {
    if (ctx) async_context_release_lock(ctx);
}

async_context_t *net_async_context(void) {
    return ctx;
}

// called from lwIP via the SNTP_SET_SYSTEM_TIME hook in lwipopts.h
void sntp_report_time(uint32_t sec) {
    epoch_at_boot = sec - to_ms_since_boot(get_absolute_time()) / 1000;
}

uint32_t net_epoch(void) {
    if (!epoch_at_boot) return 0;
    return epoch_at_boot + to_ms_since_boot(get_absolute_time()) / 1000;
}
