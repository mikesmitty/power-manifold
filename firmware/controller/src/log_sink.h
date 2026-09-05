#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Console log sink, core 0. A stdio driver mirrors everything printed into
// log_ring; the main loop's poll ships complete lines to the syslog host in
// settings (RFC 5424 over UDP, port syslog_port) once the network is up —
// boot messages included, since the ring holds the last 4 KB until then.
// Typed console input never enters the ring (the CLI pauses the sink
// around its echo), so credentials pasted at the console stay off the wire.
void log_sink_init(void); // right after stdio_init_all
void log_sink_poll(uint32_t now_ms);
void log_sink_pause(bool paused);

// Ring contents for GET /api/v1/log, oldest first; returns the bytes written.
size_t log_sink_snapshot(char *out, size_t cap);
// "off", "resolving host", "host does not resolve" or "10.0.0.5:514"
const char *log_sink_status(void);
