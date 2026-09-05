#include "log_sink.h"

#include <stdio.h>
#include <string.h>

#include "pico/critical_section.h"
#include "pico/stdio/driver.h"
#include "pico/time.h"

#include "lwip/dns.h"
#include "lwip/udp.h"

#include "log_ring.h"
#include "net/net.h"
#include "settings.h"

#define RESOLVE_RETRY_MS (60 * 1000)      // after a failed lookup
#define RESOLVE_TTL_MS   (10 * 60 * 1000) // re-resolve a working name this often
#define LINES_PER_POLL   8

static critical_section_t cs; // printf runs from the main loop and lwIP's worker alike
static volatile bool paused;

static struct udp_pcb *pcb;
static ip_addr_t host_ip;
static char resolved_name[64]; // the settings host the state below is about
static bool resolved, resolving, unresolvable;
static uint32_t next_resolve_ms; // 0 = now
static char status_buf[64];

static void out_chars(const char *buf, int len) {
    if (paused || len <= 0) return;
    critical_section_enter_blocking(&cs);
    log_ring_write(buf, (size_t)len);
    critical_section_exit(&cs);
}

static stdio_driver_t driver = {.out_chars = out_chars};

void log_sink_init(void) {
    critical_section_init(&cs);
    stdio_set_driver_enabled(&driver, true);
}

void log_sink_pause(bool p) {
    paused = p;
}

size_t log_sink_snapshot(char *out, size_t cap) {
    critical_section_enter_blocking(&cs);
    size_t n = log_ring_snapshot(out, cap);
    critical_section_exit(&cs);
    return n;
}

static void dns_cb(const char *name, const ip_addr_t *addr, void *arg) {
    (void)name; (void)arg;
    resolving = false;
    if (addr) {
        host_ip = *addr;
        resolved = true;
        unresolvable = false;
    } else {
        resolved = false;
        unresolvable = true;
        next_resolve_ms = to_ms_since_boot(get_absolute_time()) + RESOLVE_RETRY_MS;
        if (!next_resolve_ms) next_resolve_ms = 1;
    }
}

// Under the network lock. True while an address for the settings host is
// usable; a working name is refreshed every RESOLVE_TTL_MS, a failing one
// retried every RESOLVE_RETRY_MS, and a changed host starts over (dropping
// the backlog count, which was never going anywhere).
static bool ensure_address(uint32_t now_ms) {
    const char *host = g_settings.syslog_host;
    if (strcmp(host, resolved_name) != 0) {
        snprintf(resolved_name, sizeof(resolved_name), "%s", host);
        resolved = resolving = unresolvable = false;
        next_resolve_ms = 0;
        critical_section_enter_blocking(&cs);
        log_ring_take_dropped();
        critical_section_exit(&cs);
    }
    if (resolving) return resolved;
    if (next_resolve_ms && (int32_t)(now_ms - next_resolve_ms) < 0) return resolved;

    ip_addr_t a;
    err_t err = dns_gethostbyname(host, &a, dns_cb, NULL);
    if (err == ERR_OK) {
        host_ip = a;
        resolved = true;
        unresolvable = false;
    } else if (err == ERR_INPROGRESS) {
        resolving = true;
    } else {
        resolved = false;
        unresolvable = true;
    }
    next_resolve_ms = now_ms + (unresolvable ? RESOLVE_RETRY_MS : RESOLVE_TTL_MS);
    if (!next_resolve_ms) next_resolve_ms = 1;
    return resolved;
}

static void send_line(const char *msg) {
    char dgram[LOG_LINE_MAX + 96];
    size_t n = log_syslog_format(dgram, sizeof(dgram), net_epoch(), g_settings.device_name, msg);
    struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, (u16_t)n, PBUF_RAM);
    if (!p) return; // heap tight: the line stays lost, the next poll carries on
    memcpy(p->payload, dgram, n);
    udp_sendto(pcb, p, &host_ip, g_settings.syslog_port);
    pbuf_free(p);
}

void log_sink_poll(uint32_t now_ms) {
    if (!g_settings.syslog_host[0] || !net_available() || !net_up()) return;
    net_lock();
    if (!pcb) pcb = udp_new();
    if (pcb && ensure_address(now_ms)) {
        char line[LOG_LINE_MAX + 1];
        for (int i = 0; i < LINES_PER_POLL; i++) {
            critical_section_enter_blocking(&cs);
            uint32_t lost = log_ring_take_dropped();
            bool have = log_ring_next_line(line, sizeof(line));
            critical_section_exit(&cs);
            if (lost) {
                char note[48];
                snprintf(note, sizeof(note), "log: %lu line(s) lost before this point",
                         (unsigned long)lost);
                send_line(note);
            }
            if (!have) break;
            send_line(line);
        }
    }
    net_unlock();
}

const char *log_sink_status(void) {
    if (!g_settings.syslog_host[0]) return "off";
    if (resolved) {
        snprintf(status_buf, sizeof(status_buf), "%s:%u", ipaddr_ntoa(&host_ip),
                 g_settings.syslog_port);
        return status_buf;
    }
    return unresolvable ? "host does not resolve" : "resolving host";
}
