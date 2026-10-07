#pragma once

#include <stdbool.h>
#include <stddef.h>

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

// The hostnames setting from what the owner typed: names separated by spaces
// or commas, each a DNS name (letters, digits, hyphens and dots, without a
// port). Written to out lowercase, single-spaced, with any trailing dot
// dropped; empty input clears the list. NULL on success, else the reason.
const char *http_req_hostnames_parse(const char *in, char *out, size_t cap);
