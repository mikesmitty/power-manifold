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

// Addressing: DHCP on every link by default. With settings ip_static the
// static address goes on the wired link when a W6100 is fitted and on WiFi
// otherwise (never both: WiFi behind a wired link stays on DHCP as the
// standby). settings ip_dns, when set, is the resolver whatever the mode;
// in static mode with none set the gateway is used. Changes apply at boot.
void net_init(void);
void net_poll(uint32_t now_ms);
bool net_static_on_wifi(void); // the static address belongs to WiFi (net_init decided)
const char *net_dns_str(void); // resolver in use, "none" when unset
// Dotted-quad text <-> network-byte-order address; a valid netmask is a
// contiguous run of ones.
bool net_ip4_parse(const char *s, uint32_t *addr_nbo);
const char *net_ip4_str(uint32_t addr_nbo); // "" for 0; static buffer
bool net_ip4_mask_valid(uint32_t mask_nbo);
bool net_available(void);      // lwIP running
bool net_up(void);             // some link up with an address
const char *net_ip_str(void);  // address of the preferred link, "0.0.0.0" when down
const char *net_mask_str(void); // its netmask and gateway, "" when down
const char *net_gw_str(void);
int32_t net_rssi(void);        // WiFi only, 0 otherwise
uint32_t net_epoch(void);      // unix time from SNTP, 0 until first sync

// WiFi specifics used by Improv provisioning. Without NET_WIFI: no-op / -1.
void net_reconnect(void);   // (re)join with the credentials now in g_settings
int  net_link_status(void); // raw cyw43 link status; <0 = join rejected/failed

// lwIP lock: every lwIP call made outside an lwIP callback goes between these
void net_lock(void);
void net_unlock(void);
async_context_t *net_async_context(void); // NULL until net_init succeeded
