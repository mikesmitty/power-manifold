#pragma once

#include <stddef.h>
#include <stdint.h>

// Improv Wi-Fi BLE wire format (https://www.improv-wifi.com/ble/), kept free
// of BTstack so the framing and checksum logic is host-testable.
//
// RPC packet, both directions: [command, data length, data..., checksum]
// where checksum = low byte of the sum of every preceding byte. The WiFi
// settings command carries [ssid_len, ssid..., pass_len, pass...]; its
// result carries length-prefixed strings, here a single redirect URL.

#define IMPROV_STATE_AUTH_REQUIRED 0x01
#define IMPROV_STATE_AUTHORIZED    0x02
#define IMPROV_STATE_PROVISIONING  0x03
#define IMPROV_STATE_PROVISIONED   0x04

#define IMPROV_ERR_NONE              0x00
#define IMPROV_ERR_INVALID_RPC       0x01
#define IMPROV_ERR_UNKNOWN_RPC       0x02
#define IMPROV_ERR_UNABLE_TO_CONNECT 0x03
#define IMPROV_ERR_NOT_AUTHORIZED    0x04
#define IMPROV_ERR_UNKNOWN           0xFF

#define IMPROV_CMD_WIFI_SETTINGS 0x01
#define IMPROV_CMD_IDENTIFY      0x02

#define IMPROV_CAP_IDENTIFY 0x01

#define IMPROV_SSID_MAX 32
#define IMPROV_PASS_MAX 64
// largest packet accepted: cmd, len, ssid_len, ssid, pass_len, pass, checksum
#define IMPROV_RPC_MAX (3 + IMPROV_SSID_MAX + 1 + IMPROV_PASS_MAX + 1)

typedef struct {
    uint8_t cmd;
    char    ssid[IMPROV_SSID_MAX + 1]; // WIFI_SETTINGS only
    char    pass[IMPROV_PASS_MAX + 1]; // "" for an open network
} improv_rpc_t;

uint8_t improv_checksum(const uint8_t *p, size_t n);

// Framing check on a receive buffer that may hold a partial packet (writes
// arrive in MTU-sized pieces): >0 = a complete packet of that length starts
// at buf, 0 = need more bytes, <0 = malformed, discard the buffer.
int improv_rpc_frame_len(const uint8_t *buf, size_t len);

// Parse one complete packet. Returns IMPROV_ERR_NONE and fills *out, or the
// error code to report to the client (INVALID_RPC for checksum/framing
// trouble, UNKNOWN_RPC for a command we don't implement).
uint8_t improv_rpc_parse(const uint8_t *pkt, size_t len, improv_rpc_t *out);

// Result packet answering `cmd` with one string (the redirect URL). Returns
// the packet length, 0 if it doesn't fit in cap.
size_t improv_result_build(uint8_t cmd, const char *url, uint8_t *out, size_t cap);
