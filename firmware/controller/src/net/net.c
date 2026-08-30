#include "net.h"

#include <stdio.h>
#include <string.h>

#include "pico/cyw43_arch.h"

#include "lwip/apps/mdns.h"
#include "lwip/apps/sntp.h"
#include "lwip/netif.h"

#include "settings.h"

#define RECONNECT_INTERVAL_MS (10 * 1000)

static bool available;
static bool link_up;
static bool services_started;
static uint32_t last_connect_ms;
static uint32_t epoch_at_boot; // unix time minus ms_since_boot/1000

static void start_connect(void) {
    cyw43_arch_wifi_connect_async(g_settings.wifi_ssid,
                                  g_settings.wifi_pass[0] ? g_settings.wifi_pass : NULL,
                                  CYW43_AUTH_WPA2_MIXED_PSK);
}

void net_init(void) {
    if (cyw43_arch_init()) {
        printf("net: cyw43 init failed; running without network\n");
        available = false;
        return;
    }
    available = true;
    cyw43_arch_enable_sta_mode();

    cyw43_arch_lwip_begin();
    netif_set_hostname(netif_default, g_settings.device_name);
    cyw43_arch_lwip_end();

    if (g_settings.wifi_ssid[0]) {
        start_connect();
    } else {
        printf("net: no WiFi credentials; use CLI 'wifi <ssid> [pass]'\n");
    }
    last_connect_ms = 0;
}

static void start_services(void) {
    cyw43_arch_lwip_begin();
    mdns_resp_init();
    mdns_resp_add_netif(netif_default, g_settings.device_name);
    mdns_resp_add_service(netif_default, g_settings.device_name, "_http",
                          DNSSD_PROTO_TCP, 80, NULL, NULL);
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "pool.ntp.org");
    sntp_init();
    cyw43_arch_lwip_end();
}

void net_poll(uint32_t now_ms) {
    if (!available || !g_settings.wifi_ssid[0]) return;

    int status = cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA);
    bool up = status == CYW43_LINK_UP;

    if (up && !link_up) {
        printf("net: up, ip %s\n", net_ip_str());
        if (!services_started) {
            start_services();
            services_started = true;
        }
    }
    link_up = up;

    // retry on explicit failure or lingering downtime
    if (!up && (status < 0 || now_ms - last_connect_ms > RECONNECT_INTERVAL_MS)) {
        last_connect_ms = now_ms;
        start_connect();
    }
}

bool net_available(void) {
    return available;
}

bool net_up(void) {
    return available && link_up;
}

const char *net_ip_str(void) {
    if (!net_up()) return "0.0.0.0";
    return ip4addr_ntoa(netif_ip4_addr(netif_default));
}

int32_t net_rssi(void) {
    int32_t rssi = 0;
    if (net_up()) cyw43_wifi_get_rssi(&cyw43_state, &rssi);
    return rssi;
}

// called from lwIP via the SNTP_SET_SYSTEM_TIME hook in lwipopts.h
void sntp_report_time(uint32_t sec) {
    epoch_at_boot = sec - to_ms_since_boot(get_absolute_time()) / 1000;
}

uint32_t net_epoch(void) {
    if (!epoch_at_boot) return 0;
    return epoch_at_boot + to_ms_since_boot(get_absolute_time()) / 1000;
}
