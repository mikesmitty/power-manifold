#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// TLS on the broker link, in the three states settings mqtt_tls chooses
// (MQTT_TLS_OFF, MQTT_TLS_VERIFIED and MQTT_TLS_UNVERIFIED, settings.h).
// Off, the link is plain MQTT. Unverified, it is TLS that checks nothing:
// hidden from a passive listener, open to anyone who can stand in for the
// broker, the same as a client's "insecure" switch. Verified, the broker's
// chain must lead to a trusted certificate: the one installed in mqtt_ca
// when there is one (the CA that issued the broker's, or the broker's own
// self-signed one), else one of the Let's Encrypt roots built into the
// image (roots/*.pem). The name in the broker's certificate is checked
// whenever mqtt_host is a name. With a public CA that name check is the
// whole defence, because anyone can get a Let's Encrypt certificate for a
// name they control, so the built-in roots serve a broker by name only and
// a broker by address needs its certificate installed. The update client
// stays on plain HTTP by design; firmware authenticity rests on the image
// signature.

// Whether an incoming certificate parses, before it is stored: NULL when it
// does, else the message for a 400. The host tests stub this.
const char *mqtt_ca_check(const uint8_t *der, size_t len);

// "plain", "unverified", "verified, installed certificate" or "verified,
// Let's Encrypt", from the settings
const char *mqtt_tls_mode_str(void);

// Why the settings allow no connection attempt, or NULL when they do: a
// verified link to a broker by address with no certificate installed.
const char *mqtt_tls_blocker(void);

// One line about the installed certificate for the console: subject, expiry
// and SHA-256 fingerprint. False with none installed.
bool mqtt_ca_describe(char *out, size_t cap);

// Verified TLS cannot judge certificate dates before the clock is known
bool mqtt_tls_wants_clock(void);

// A connection configuration for the current settings, built afresh each
// time (the one before last is released then). NULL when the trusted
// certificates do not parse, the settings block the link, or memory is
// short.
struct altcp_tls_config;
struct altcp_tls_config *mqtt_tls_config(void);

// Right after mqtt_client_connect on a TLS connection: send the broker's
// name to it and, when verifying, require the certificate to carry it. An
// address-literal broker has no name to send or check, so nothing happens.
struct mqtt_client_s;
void mqtt_tls_set_hostname(struct mqtt_client_s *client);

// The last attempt's verdict on the broker's certificate: start clears what
// this attempt has noted, end shows it (or clears it when the link came up).
void mqtt_tls_attempt_start(void);
void mqtt_tls_attempt_end(bool connected);

// Why the last attempt refused the broker's certificate ("broker
// certificate does not carry the broker's name"), or NULL
const char *mqtt_tls_refusal(void);
