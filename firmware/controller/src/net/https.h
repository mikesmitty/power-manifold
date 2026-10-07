#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// The web server's HTTPS certificate: installed over the API (acme.sh's
// deploy hook, or the page), kept in the data partition, and turned into
// the mbedTLS server configuration the port-443 listener uses.
//
// Out of the box nothing is installed and the server speaks plain HTTP.
// Settings https turns HTTPS on, which needs a certificate: the server then
// listens on 443 and port 80 only redirects there. Installing a newer
// certificate takes effect for new connections at once.
//
// The key must be ECDSA (P-256 or P-384): an RSA signature takes the
// controller over a second per connection. The certificate is stored with
// its chain, up to TLS_CERTS_MAX certificates in TLS_RECORD_MAX bytes, two
// copies so a save cut off by a power loss leaves the previous one. The
// record is never exported and a factory reset erases it.

struct altcp_tls_config;

#define HTTPS_UPLOAD_MAX 12288 // PEM text of the key and a full chain
#define HTTPS_EXPIRY_WARN_DAYS 7 // the renewal has stopped: a problem from here

// Core 0, once at start-up, before http_init: loads the stored certificate.
void https_init(void);

// The upload buffer POST /api/v1/tls streams into (HTTPS_UPLOAD_MAX bytes).
char *https_upload_buffer(void);

// Check and install the PEM text in the upload buffer: the key, then the
// chain with the leaf first, in any block order. Saves to flash and makes
// it the certificate new connections get. NULL on success, else a message
// for a 400 or 500 (*server_fault set for the latter). Core 0, with the
// network lock (lwIP's context).
const char *https_install(size_t len, bool *server_fault);

// Remove the certificate (refused while settings https is on).
const char *https_remove(void);

// The configuration new HTTPS connections get, or NULL with no usable
// certificate. Connections keep theirs until they close: each one
// accepted calls https_conn_opened and, when it goes, https_conn_closed.
struct altcp_tls_config *https_config(void);
void https_conn_opened(struct altcp_tls_config *conf);
void https_conn_closed(struct altcp_tls_config *conf);

// Settings https is on and a certificate is there: port 80 redirects.
bool https_enforced(void);

// The DNS names the certificate covers, space-separated ("*.x" for a
// wildcard), "" with none. The web server answers to them.
const char *https_names(void);

// The unix time the certificate expires, 0 with none installed
uint32_t https_expires(void);

// Days until the certificate expires, given the unix time (negative once
// it has); false with none installed.
bool https_days_left(uint32_t now_epoch, int32_t *days);

// What needs attention for the health line, or NULL: HTTPS on without a
// usable certificate, or one expiring within HTTPS_EXPIRY_WARN_DAYS.
const char *https_problem(uint32_t now_epoch);

// One line for the console: names, issuer, expiry and SHA-256 fingerprint.
// False with none installed.
bool https_describe(char *out, size_t cap);

// The GET /api/v1/tls object. Returns its length, 0 if it did not fit.
size_t https_json(char *out, size_t cap, uint32_t now_epoch);

// Factory reset: erase both stored copies. Core 0, engine running.
bool https_wipe(void);
