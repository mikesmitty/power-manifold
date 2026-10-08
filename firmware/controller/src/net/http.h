#pragma once

#include <stdbool.h>
#include <stdint.h>

// Minimal HTTP server (core 0, lwIP raw API) — the broker-independent local
// management path.
//
//   GET  /                    embedded status page, with a settings panel
//   GET  /api/v1/status       JSON snapshot
//   GET  /api/v1/settings     name, broker, port, user, whether a password /
//                             token is stored; budget, fan mode + thresholds
//   POST /api/v1/settings     any subset of {"name","mqtt_host","mqtt_port",
//                             "mqtt_user","mqtt_pass","token","budget_w",
//                             "fan_mode","fan_on_w","fan_off_w","fan_on_ma"};
//                             saved to flash at once; budget/fan apply live,
//                             name/broker take a reboot (reboot_required)
//   POST /api/v1/port/<n>     {"action":"enable"|"disable"|"hard_reset"|"src_cap"}
//   POST /api/v1/fan          {"on":true|false} or {"mode":"auto"}
//   POST /api/v1/budget       {"watts":N}
//   POST /api/v1/reboot       plain reboot shortly after the response
//   POST /api/v1/update       OTA: body = firmware image (uf2 or bin); writes
//                             the inactive A/B slot, then reboots into a trial
//   GET  /api/v1/tls          the HTTPS certificate: names, issuer, expiry
//   POST /api/v1/tls          body = PEM key and certificate chain: install
//   POST /api/v1/tls/remove   remove it (with settings https off)
//
// With settings https on and a certificate installed, the same server also
// listens on 443 and port 80 answers every request with a redirect there.
// Responses keep the connection open for the next request (HTTP/1.1
// keep-alive), so a page polling over HTTPS pays for one handshake.
//
// Every POST, the console log and the settings need "Authorization: Bearer
// <token>". With no token stored the controller refuses them all, except
// that /settings opens for first-time setup in one of two windows: ten
// minutes after Wi-Fi setup over Improv succeeds, for any request, or the
// first hour after power-up for a request arriving over Ethernet. A request
// let in that way must set a token, which closes both.

void http_init(void); // also starts the Ethernet setup hour, and loads the HTTPS certificate

// Open or close the port-443 listener to match settings https and the
// installed certificate (net/https.h). After either changes; takes the
// network lock.
void http_tls_sync(void);

// Open the Wi-Fi setup window (10 minutes, or until a token exists). Improv
// calls this with the network lock held once provisioning succeeds.
void http_setup_wifi_open(uint32_t now_ms);

// Start the Ethernet setup hour over (a short press of the front-panel
// button). Harmless once a token is stored.
void http_setup_window_restart(uint32_t now_ms);

// Settings are open for first-time setup right now: the Wi-Fi window is
// open, or the Ethernet hour is running with the wired link up. For the LED
// cue.
bool http_setup_open(uint32_t now_ms);

// A reboot was requested over the API and its delay has elapsed; the main
// loop does the reboot (flushing any debounced settings save first).
bool http_reboot_due(uint32_t now_ms);
