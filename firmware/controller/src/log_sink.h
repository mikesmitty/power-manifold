#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Console log sink, core 0. A stdio driver mirrors everything printed into
// log_ring; the main loop's poll ships complete lines to the syslog host in
// settings (RFC 5424 over UDP, port syslog_port) once the network is up —
// boot messages included, since the ring holds the last 4 KB until then.
// The console's echo of typed keys never enters the ring (the CLI pauses
// the sink around it); each finished command goes in once through
// log_sink_note, with its secrets masked, so passwords stay off the wire.
void log_sink_init(void); // right after stdio_init_all
void log_sink_poll(uint32_t now_ms);
void log_sink_pause(bool paused);

// One line into the ring only, not out to stdio. The newline is added here.
void log_sink_note(const char *line);

// While buf is set, everything core 0's main loop prints is also copied
// into it, carriage returns dropped, up to cap - 1 bytes and NUL-terminated;
// *len counts what was kept and *cut turns true once something did not
// fit. Output from interrupt handlers and from core 1 is not copied.
// NULL ends the capture.
void log_sink_capture(char *buf, size_t cap, size_t *len, bool *cut);

// Ring contents for GET /api/v1/log, oldest first; returns the bytes written.
size_t log_sink_snapshot(char *out, size_t cap);
// "off", "resolving host", "host does not resolve" or "10.0.0.5:514"
const char *log_sink_status(void);
