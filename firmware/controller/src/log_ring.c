#include "log_ring.h"

#include <stdio.h>
#include <string.h>

#include "civil_time.h"

static char ring[LOG_RING_SIZE];
static uint32_t head;    // bytes ever written; the next goes to ring[head % SIZE]
static uint32_t tail;    // the line reader's position
static uint32_t dropped; // lines the reader never saw
static bool resync;      // tail sits mid-line after an overflow

static char at(uint32_t pos) {
    return ring[pos % LOG_RING_SIZE];
}

void log_ring_reset(void) {
    head = tail = dropped = 0;
    resync = false;
}

void log_ring_write(const char *s, size_t n) {
    if (n == 0) return;
    if (n > LOG_RING_SIZE) { // only the tail of a huge write can survive
        s += n - LOG_RING_SIZE;
        n = LOG_RING_SIZE;
    }
    uint32_t new_head = head + (uint32_t)n;
    if (new_head - tail > LOG_RING_SIZE) {
        // the reader is about to be lapped: count the lines it loses while
        // their bytes are still there, then move it to the oldest survivor
        uint32_t new_tail = new_head - LOG_RING_SIZE;
        uint32_t end = new_tail < head ? new_tail : head;
        for (uint32_t p = tail; p < end; p++)
            if (at(p) == '\n') dropped++;
        tail = new_tail;
        resync = at(new_tail - 1) != '\n'; // mid-line: the reader skips the rest of it
    }
    for (size_t i = 0; i < n; i++) ring[(head + i) % LOG_RING_SIZE] = s[i];
    head = new_head;
}

// copy [from, to) into out, minus control characters and a leading prompt
static size_t take(char *out, size_t cap, uint32_t from, uint32_t to) {
    size_t n = 0;
    if (to - from >= 2 && at(from) == '>' && at(from + 1) == ' ') from += 2;
    for (uint32_t p = from; p < to && n < cap - 1; p++) {
        char c = at(p);
        if ((unsigned char)c < 0x20 || c == 0x7F) continue;
        out[n++] = c;
    }
    out[n] = '\0';
    return n;
}

bool log_ring_next_line(char *out, size_t cap) {
    if (cap == 0) return false;
    for (;;) {
        if (resync) {
            // discard the remains of the line the overflow cut through
            uint32_t p = tail;
            while (p < head && at(p) != '\n') p++;
            if (p >= head) { tail = p; return false; } // its end has not arrived yet
            dropped++;
            tail = p + 1;
            resync = false;
        }
        uint32_t p = tail;
        while (p < head && at(p) != '\n') p++;
        if (p >= head) {
            if (head - tail < LOG_LINE_MAX) return false; // wait for the line end
            p = tail + LOG_LINE_MAX;                        // split an endless line
            size_t n = take(out, cap, tail, p);
            tail = p;
            if (n) return true;
            continue;
        }
        size_t n = take(out, cap, tail, p);
        tail = p + 1;
        if (n) return true;
    }
}

uint32_t log_ring_take_dropped(void) {
    uint32_t n = dropped;
    dropped = 0;
    return n;
}

size_t log_ring_snapshot(char *out, size_t cap) {
    if (cap == 0) return 0;
    uint32_t start = head > LOG_RING_SIZE ? head - LOG_RING_SIZE : 0;
    size_t n = head - start;
    if (n > cap - 1) {
        start += (uint32_t)(n - (cap - 1));
        n = cap - 1;
    }
    for (size_t i = 0; i < n; i++) out[i] = at(start + (uint32_t)i);
    out[n] = '\0';
    return n;
}

size_t log_syslog_format(char *out, size_t cap, uint32_t epoch, const char *host,
                         const char *msg) {
    char ts[24] = "-";
    if (epoch) {
        unsigned y, m, d;
        civil_from_days(epoch / 86400, &y, &m, &d);
        uint32_t rem = epoch % 86400;
        snprintf(ts, sizeof(ts), "%04u-%02u-%02uT%02lu:%02lu:%02luZ", y, m, d,
                 (unsigned long)(rem / 3600), (unsigned long)(rem % 3600 / 60),
                 (unsigned long)(rem % 60));
    }
    int n = snprintf(out, cap, "<134>1 %s %s pwrman - - - %s", ts, host[0] ? host : "-", msg);
    if (n < 0) return 0;
    return (size_t)n < cap ? (size_t)n : cap - 1;
}
