#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Header checks on a raw HTTP request (request line, headers, blank line,
// body), for the requests a setup door lets in without a credential. Pure
// string code; host-tested.
//
// A web page on another site can make the owner's browser send a POST to the
// controller, but only with a text/plain, form-urlencoded or multipart body
// unless the controller agrees to a CORS preflight, which it never does. A
// page that points its own domain name at the controller's address (DNS
// rebinding) gets past that rule, but its requests still carry its own name
// in Host.

// The value of the named header (case-insensitive name, surrounding spaces
// trimmed) copied into out. Only the header block is searched. False when the
// header is absent or its value does not fit.
bool http_req_header(const char *req, const char *name, char *out, size_t cap);

// The body is declared as application/json, with or without parameters.
bool http_req_is_json(const char *req);

// Host names the controller itself: its address on the connection (dotted
// IPv4 text), its device name, the device name under .local, or one of the
// owner's extra names (the hostnames setting: lowercase, space-separated),
// with an optional :80. False when Host is absent or names anything else.
bool http_req_host_ok(const char *req, const char *local_ip, const char *device_name,
                      const char *hostnames);

// The Authorization header is "Bearer <secret>" (scheme in any case) with
// exactly this secret. The comparison takes the same time wherever the first
// wrong character is, so response timing cannot reveal the secret a
// character at a time; only its length shows. False for an empty secret.
bool http_req_bearer_ok(const char *req, const char *secret);

// The hostnames setting from what the owner typed: names separated by spaces
// or commas, each a DNS name (letters, digits, hyphens and dots, without a
// port). Written to out lowercase, single-spaced, with any trailing dot
// dropped; empty input clears the list. NULL on success, else the reason.
const char *http_req_hostnames_parse(const char *in, char *out, size_t cap);

// The method and path of the request line, for a log line: the query is
// dropped, anything but printable ASCII becomes '?', and a long path is cut
// short. The request is whatever the client sent, so nothing of it reaches
// the log unfiltered.
void http_req_line_text(const char *req, char *out, size_t cap);

// Refused requests are logged at most HTTP_REFUSAL_LOG_MAX to a minute, so
// a client that keeps trying cannot fill the log; the rest are counted and
// the count is reported with the next line that is logged.
#define HTTP_REFUSAL_LOG_MAX 5
#define HTTP_REFUSAL_WINDOW_MS 60000u

typedef struct {
    uint32_t window_ms; // start of the current minute
    unsigned logged;    // lines logged in it
    unsigned unlogged;  // refusals since the last line that were not logged
    bool     started;
} http_refusal_limit_t;

// True when this refusal gets a line; *unlogged is then the number of
// earlier ones that did not.
bool http_refusal_log_ok(http_refusal_limit_t *l, uint32_t now_ms, unsigned *unlogged);
