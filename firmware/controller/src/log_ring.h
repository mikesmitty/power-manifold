#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Console output ring behind the log sink: everything printed, oldest bytes
// dropped first. Hardware-free; the caller serializes access.
#define LOG_RING_SIZE 4096
#define LOG_LINE_MAX  240 // longer lines are split

void log_ring_reset(void);
void log_ring_write(const char *s, size_t n);

// Next complete line not yet taken: line ending removed, carriage returns
// and other control characters dropped, a leading console prompt ("> ")
// stripped, empty lines skipped. False when no complete line is waiting.
bool log_ring_next_line(char *out, size_t cap);

// Lines the reader lost to overflow since the last call.
uint32_t log_ring_take_dropped(void);

// Everything the ring still holds, oldest first, NUL-terminated; returns
// the bytes written (at most cap - 1).
size_t log_ring_snapshot(char *out, size_t cap);

// RFC 5424 syslog datagram: PRI 134 (local0.info), version 1, the epoch as
// an ISO 8601 UTC timestamp (NIL when 0), hostname, app "pwrman".
size_t log_syslog_format(char *out, size_t cap, uint32_t epoch, const char *host,
                         const char *msg);
