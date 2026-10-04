#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// TLS on the broker link, in one of three states the settings choose. With
// mqtt_tls 0 the link is plain MQTT. With mqtt_tls 1 and no certificate
// installed it is TLS that verifies nothing: hidden from a passive listener,
// open to anyone who can stand in for the broker, the same as a client's
// "insecure" switch. With mqtt_tls 1 and mqtt_ca installed the broker's
// chain is verified against that certificate, and so is the name in it when
// mqtt_host is a name rather than an address. The installed certificate is
// either the CA that issued the broker's or the broker's own self-signed
// one. The update client stays on plain HTTP by design; firmware
// authenticity rests on the image signature.

// Whether an incoming certificate parses, before it is stored: NULL when it
// does, else the message for a 400. The host tests stub this.
const char *mqtt_ca_check(const uint8_t *der, size_t len);

// "plain", "unverified" or "verified", from the settings
const char *mqtt_tls_mode_str(void);

// One line about the installed certificate for the console: subject, expiry
// and SHA-256 fingerprint. False with none installed.
bool mqtt_ca_describe(char *out, size_t cap);

// Verified TLS cannot judge certificate dates before the clock is known
bool mqtt_tls_wants_clock(void);

// A connection configuration for the current settings, built afresh each
// time (the one before last is released then). NULL when the installed
// certificate does not parse or memory is short.
struct altcp_tls_config;
struct altcp_tls_config *mqtt_tls_config(void);

// Right after mqtt_client_connect on a TLS connection: send the broker's
// name to it and, when verifying, require the certificate to carry it. An
// address-literal broker has no name to send or check, so nothing happens.
struct mqtt_client_s;
void mqtt_tls_set_hostname(struct mqtt_client_s *client);
