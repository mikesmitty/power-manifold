#pragma once

#include <stdbool.h>
#include <stdint.h>

// WiFi (CYW43) bring-up and housekeeping, core 0. The network layer is
// deliberately optional at runtime: if the radio fails or credentials are
// unset, the engine keeps running and the USB CLI remains available.
//
// The eventual wired path (W6100 on the reserved GP16-21) slots in here as an
// alternative lwIP netif; mqtt/http above it are transport-agnostic.

void net_init(void);
void net_poll(uint32_t now_ms);
bool net_available(void); // radio initialized, lwIP running
bool net_up(void);        // link up with an IP address
const char *net_ip_str(void);
int32_t net_rssi(void);
uint32_t net_epoch(void); // unix time from SNTP, 0 until first sync
