#include <stdlib.h>
#include <string.h>

#include "log_ring.h"
#include "microtest.h"

static char line[LOG_LINE_MAX + 1];

static void test_lines(void) {
    log_ring_reset();
    log_ring_write("first\nsecond\r\n", 14);
    MT_ASSERT(log_ring_next_line(line, sizeof(line)));
    MT_ASSERT(!strcmp(line, "first"));
    MT_ASSERT(log_ring_next_line(line, sizeof(line)));
    MT_ASSERT(!strcmp(line, "second")); // CR dropped
    MT_ASSERT(!log_ring_next_line(line, sizeof(line)));
    // a line arrives in pieces
    log_ring_write("abc", 3);
    MT_ASSERT(!log_ring_next_line(line, sizeof(line)));
    log_ring_write("def\n", 4);
    MT_ASSERT(log_ring_next_line(line, sizeof(line)));
    MT_ASSERT(!strcmp(line, "abcdef"));
    // console prompt stripped, empty lines skipped, controls dropped
    log_ring_write("\n\n> net: wifi up\n\x1b[0mx\ty\n", 26);
    MT_ASSERT(log_ring_next_line(line, sizeof(line)));
    MT_ASSERT(!strcmp(line, "net: wifi up"));
    MT_ASSERT(log_ring_next_line(line, sizeof(line)));
    MT_ASSERT(!strcmp(line, "[0mxy"));
    MT_ASSERT(!log_ring_next_line(line, sizeof(line)));
    MT_ASSERT_EQ(log_ring_take_dropped(), 0);
}

static void test_long_line_splits(void) {
    log_ring_reset();
    char big[LOG_LINE_MAX + 50];
    memset(big, 'x', sizeof(big));
    log_ring_write(big, sizeof(big));
    MT_ASSERT(log_ring_next_line(line, sizeof(line)));
    MT_ASSERT_EQ(strlen(line), LOG_LINE_MAX);
    MT_ASSERT(!log_ring_next_line(line, sizeof(line))); // the rest waits for its end
    log_ring_write("\n", 1);
    MT_ASSERT(log_ring_next_line(line, sizeof(line)));
    MT_ASSERT_EQ(strlen(line), 50);
}

static void test_overflow_resyncs(void) {
    log_ring_reset();
    char l[32];
    int written = 0;
    for (int i = 0; i < 600; i++) { // 5.4 KB into a 4 KB ring, nobody reading
        int n = snprintf(l, sizeof(l), "line %03d\n", i);
        log_ring_write(l, (size_t)n);
        written += n;
    }
    MT_ASSERT(written > LOG_RING_SIZE);
    MT_ASSERT(log_ring_next_line(line, sizeof(line)));
    MT_ASSERT(!strncmp(line, "line ", 5));   // a whole line, not the cut one
    MT_ASSERT_EQ(strlen(line), 8);
    int first = atoi(line + 5);
    MT_ASSERT(first > 0);
    uint32_t lost = log_ring_take_dropped();
    MT_ASSERT_EQ(lost, (uint32_t)first);     // lines 0..first-1 never made it
    int n = 1;
    while (log_ring_next_line(line, sizeof(line))) n++;
    MT_ASSERT_EQ(first + n, 600);            // everything after is intact and in order
    MT_ASSERT(!strcmp(line, "line 599"));
    // snapshot holds the newest ring-full, oldest first
    static char snap[LOG_RING_SIZE + 1];
    size_t got = log_ring_snapshot(snap, sizeof(snap));
    MT_ASSERT_EQ(got, LOG_RING_SIZE);
    MT_ASSERT(!strcmp(snap + got - 9, "line 599\n"));
    char small[16];
    MT_ASSERT_EQ(log_ring_snapshot(small, sizeof(small)), 15);
    MT_ASSERT(!strcmp(small, "e 598\nline 599\n"));
}

static void test_syslog_format(void) {
    char out[160];
    log_syslog_format(out, sizeof(out), 0, "pwrman", "hello");
    MT_ASSERT(!strcmp(out, "<134>1 - pwrman pwrman - - - hello"));
    log_syslog_format(out, sizeof(out), 1788556512, "pwrman", "hello"); // 2026-09-04 21:15:12 UTC
    MT_ASSERT(!strcmp(out, "<134>1 2026-09-04T21:15:12Z pwrman pwrman - - - hello"));
    log_syslog_format(out, sizeof(out), 951782400, "", "leap"); // 2000-02-29
    MT_ASSERT(!strcmp(out, "<134>1 2000-02-29T00:00:00Z - pwrman - - - leap"));
    MT_ASSERT_EQ(log_syslog_format(out, 10, 0, "h", "truncated"), 9);
}

void run_log_ring_tests(void) {
    mt_run("log: lines come out clean and complete", test_lines);
    mt_run("log: an endless line is split", test_long_line_splits);
    mt_run("log: overflow resyncs and counts the loss", test_overflow_resyncs);
    mt_run("log: RFC 5424 datagram", test_syslog_format);
}
