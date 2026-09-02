#pragma once

#include <stdbool.h>
#include <stdint.h>

// Improv Wi-Fi BLE provisioning (https://www.improv-wifi.com/ble/) over
// BTstack on the CYW43 radio, core 0. The standard protocol, so the Home
// Assistant companion app and the Web Bluetooth provisioner at
// improv-wifi.com work unchanged.
//
// BLE carries only SSID + password. The GATT service is advertised only
// inside a provisioning window, which opens by itself while the device has
// no WiFi credentials or has been off the network for IMPROV_DOWN_OPEN_MS,
// and on request (CLI `improv on`, the Home Assistant button) for
// IMPROV_WINDOW_MS. It closes on success (the client is handed
// http://<ip>/ to continue in the web UI), when the network comes back, on
// timeout, or on `improv off`; the BT controller is powered down outside
// the window. A failed attempt (wrong password, 30 s without an IP) reports
// UNABLE_TO_CONNECT and restores the previous credentials.

#define IMPROV_DOWN_OPEN_MS (5 * 60 * 1000)
#define IMPROV_WINDOW_MS    (10 * 60 * 1000)

void improv_init(void);            // after cyw43_arch_init succeeded
void improv_poll(uint32_t now_ms); // main loop, before net_poll

// Open a window for window_ms (0 = until closed or provisioned). False
// when BLE is unavailable (radio init failed).
bool improv_open(uint32_t window_ms, const char *why);
void improv_close(void);

bool improv_available(void);
bool improv_active(void); // window open (advertising, connected or provisioning)
// "off", "advertising", "connected", "provisioning", "provisioned"
const char *improv_state_str(void);
uint32_t improv_window_left_s(uint32_t now_ms); // 0 when closed or open-ended
