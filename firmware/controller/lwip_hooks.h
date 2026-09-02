#pragma once

// lwIP hook file (LWIP_HOOK_FILENAME in lwipopts.h).

#include "lwip/ip4_addr.h"

struct netif;

// Source-based routing: with WiFi and wired Ethernet on the same subnet,
// replies must leave through the interface that owns their source address
// rather than whichever netif lwIP lists first. Implemented in net.c.
struct netif *net_ip4_route_src(const ip4_addr_t *src, const ip4_addr_t *dest);
