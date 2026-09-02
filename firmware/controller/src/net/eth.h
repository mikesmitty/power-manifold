#pragma once

#include <stdbool.h>
#include <stdint.h>

// WIZnet W6100 wired Ethernet as an lwIP netif (MACRAW over SPI0, see
// pins.h for the EVB-Pico2 mapping). Core 0. Probed once at boot: a board
// without the chip just reports "absent" and the rest of the stack carries
// on over WiFi. Receive is interrupt-driven (INTn -> async-context worker),
// the PHY link is polled every half second and drives DHCP.

struct netif;

#if PWRMAN_NET_ETH
bool eth_init(void); // after lwIP is running; false = no chip found
bool eth_present(void);
bool eth_link(void); // PHY link
bool eth_up(void);   // link up with an address
struct netif *eth_netif_ptr(void);
// "absent" | "no link" | "100M full, 10.0.0.5" | "100M full, no address"
const char *eth_status_str(void);
#else
static inline bool eth_init(void) { return false; }
static inline bool eth_present(void) { return false; }
static inline bool eth_link(void) { return false; }
static inline bool eth_up(void) { return false; }
static inline struct netif *eth_netif_ptr(void) { return (struct netif *)0; }
static inline const char *eth_status_str(void) { return "not built"; }
#endif
