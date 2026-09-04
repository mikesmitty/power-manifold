#include "fault_text.h"

#include <stdio.h>
#include <string.h>

#include "boot_reason.h"

static size_t put(char *buf, size_t cap, size_t n, const char *s) {
    size_t len = strlen(s);
    if (n >= cap - 1) return n;
    if (len > cap - 1 - n) len = cap - 1 - n;
    memcpy(buf + n, s, len);
    buf[n + len] = '\0';
    return n + len;
}

size_t fault_text(const fault_rec_t *r, char *buf, size_t cap) {
    static const char *const BITS[8] = {"general", "otw1", "otw2", "ntc1",
                                        "ntc2", "cc", "short-vbatt", "vbatt-low"};
    static const char *const PROBE[] = {"?", "mux", "ina226", "mpq4242", "enable"};
    if (cap == 0) return 0;
    buf[0] = '\0';
    size_t n = 0;

    switch (r->type) {
    case EVT_FAULT: {
        bool any = false;
        for (int b = 0; b < 8; b++) {
            if (!(r->code & (1u << b))) continue;
            if (any) n = put(buf, cap, n, "+");
            n = put(buf, cap, n, BITS[b]);
            any = true;
        }
        if (r->arg) n = put(buf, cap, n, any ? " +ocp" : "ocp");
        else if (!any) n = put(buf, cap, n, "none");
        return n;
    }
    case EVT_PROBE_FAIL:
        n = put(buf, cap, n, "probe: ");
        return put(buf, cap, n, r->code < sizeof(PROBE) / sizeof(PROBE[0]) ? PROBE[r->code] : "?");
    case EVT_BOOT: {
        boot_cause_t b = {.reason = (boot_reason_t)(r->code & 0xFF),
                         .core = (uint8_t)(r->code >> 8),
                         .pc = r->arg, .lr = r->power_mw, .cfsr = r->contract_mw};
        n = put(buf, cap, n, "boot: ");
        return n + boot_reason_text(&b, buf + n, cap - n);
    }
    default:
        return put(buf, cap, n, "?");
    }
}
