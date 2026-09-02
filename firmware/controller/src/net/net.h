#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "pico/async_context.h"

// Network links, core 0: CYW43 WiFi (NET_WIFI) and/or W6100 wired Ethernet
// (NET_ETH), both lwIP netifs. Wired wins the default route while its link
// is up; WiFi carries on underneath and takes over if the cable goes.
// mqtt/http/mdns/sntp above this layer are transport-agnostic. The network
// is optional at runtime: if lwIP never comes up the engine keeps running
// and the USB CLI remains available.

void net_init(void);
void net_poll(uint32_t now_ms);
bool net_available(void);      // lwIP running
bool net_up(void);             // some link up with an address
const char *net_ip_str(void);  // address of the preferred link, "0.0.0.0" when down
int32_t net_rssi(void);        // WiFi only, 0 otherwise
uint32_t net_epoch(void);      // unix time from SNTP, 0 until first sync

// WiFi specifics used by Improv provisioning. Without NET_WIFI: no-op / -1.
void net_reconnect(void);   // (re)join with the credentials now in g_settings
int  net_link_status(void); // raw cyw43 link status; <0 = join rejected/failed

// lwIP lock: every lwIP call made outside an lwIP callback goes between these
void net_lock(void);
void net_unlock(void);
async_context_t *net_async_context(void); // NULL until net_init succeeded
