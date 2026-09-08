#include "health.h"

#include <stdio.h>
#include <string.h>

#include "flash_map.h"
#include "ipc.h"
#include "net/eth.h"
#include "net/net.h"
#include "settings.h"
#include "ups/ups.h"

static size_t put(char *buf, size_t cap, size_t n, const char *s) {
    size_t len = strlen(s);
    if (n >= cap - 1) return n;
    if (len > cap - 1 - n) len = cap - 1 - n;
    memcpy(buf + n, s, len);
    buf[n + len] = '\0';
    return n + len;
}

unsigned health_problems(const telemetry_t *t, char *buf, size_t cap) {
    if (cap == 0) return 0;
    buf[0] = '\0';
    size_t n = 0;
    unsigned count = 0;

    bool any_fault = false;
    for (unsigned i = 0; i < NUM_PORTS; i++) {
        if (t->port[i].state != PORT_STATE_FAULT) continue;
        n = put(buf, cap, n, any_fault ? ", " : "faults: ");
        n = put(buf, cap, n, settings_port_name(i));
        any_fault = true;
        count++;
    }
    if (!ipc_engine_alive()) {
        n = put(buf, cap, n, count ? "; " : "");
        n = put(buf, cap, n, "engine stalled");
        count++;
    }
    if (flash_map_update_pending()) {
        n = put(buf, cap, n, count ? "; " : "");
        n = put(buf, cap, n, "trial firmware uncommitted");
        count++;
    }
    if (eth_present() && !eth_link() && net_up()) {
        n = put(buf, cap, n, count ? "; " : "");
        n = put(buf, cap, n, "wired link down, on WiFi");
        count++;
    }
    if (ups_on_battery()) {
        n = put(buf, cap, n, count ? "; " : "");
        n = put(buf, cap, n, "on UPS battery");
        count++;
    }
    if (ups_fault()) {
        char text[96];
        ups_fault_text(text, sizeof(text));
        n = put(buf, cap, n, count ? "; " : "");
        n = put(buf, cap, n, "UPS battery: ");
        n = put(buf, cap, n, text);
        count++;
    }
    return count;
}
