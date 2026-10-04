#include "fault_text.h"

#include <stdio.h>
#include <string.h>

#include "engine/blade.h"
#include "boot_reason.h"

static size_t put(char *buf, size_t cap, size_t n, const char *s) {
    size_t len = strlen(s);
    if (n >= cap - 1) return n;
    if (len > cap - 1 - n) len = cap - 1 - n;
    memcpy(buf + n, s, len);
    buf[n + len] = '\0';
    return n + len;
}

// Names for a set of bits, joined with "+"; returns whether any was set.
static bool put_bits(char *buf, size_t cap, size_t *n, uint32_t bits,
                     const char *const *names, int count) {
    bool any = false;
    for (int b = 0; b < count; b++) {
        if (!(bits & (1u << b))) continue;
        if (any) *n = put(buf, cap, *n, "+");
        *n = put(buf, cap, *n, names[b]);
        any = true;
    }
    return any;
}

size_t fault_text(const fault_rec_t *r, char *buf, size_t cap) {
    // PORT_FAULT_* (manifold.h)
    static const char *const BITS[16] = {
        "general", "otw1", "otw2", "ntc1", "ntc2", "cc", "short-vbatt", "vbatt-low",
        "ocp", "ovp", "vconn", "port-hot", "converter", "converter-hot", "plug-hot", "blade"};
    // BLADE_FAULT_* (blade_regs.h): the detail a gen-3 record carries
    static const char *const BLADE[13] = {
        "ovp", "ocp", "vconn-ocp", "cc-ovp", "port-otp", "conv-scp", "conv-ocp", "conv-ovp",
        "conv-hot", "plug-hot", "bus", "vbus", "pd"};
    static const char *const PROBE[] = {"?", "mux", "ina226", "mpq4242", "enable", "expander reset",
                                        "blade", "no answer", "update", "stopped answering"};
    // UPDATE_FAIL_* (blade_update.h): the detail of a failed bootloader trip
    static const char *const UPDATE[] = {"?", "bootloader silent", "wrong chip", "no image", "erase",
                                         "write", "verify", "crash loop", "not taking"};
    if (cap == 0) return 0;
    buf[0] = '\0';
    size_t n = 0;

    switch (r->type) {
    case EVT_FAULT: {
        bool any;
        if (PORT_FAULT_ARG_IS_GEN3(r->arg)) {
            // the blade's own word says more than the folded bits
            any = put_bits(buf, cap, &n, r->arg & 0xFFFF, BLADE, 13);
        } else {
            // a gen-2 record: the trip flag in arg predates PORT_FAULT_OCP
            uint32_t bits = r->code | (r->arg ? PORT_FAULT_OCP : 0);
            any = put_bits(buf, cap, &n, bits, BITS, 16);
        }
        if (!any) n = put(buf, cap, n, "none");
        return n;
    }
    case EVT_PROBE_FAIL:
        n = put(buf, cap, n, "probe: ");
        n = put(buf, cap, n, r->code < sizeof(PROBE) / sizeof(PROBE[0]) ? PROBE[r->code] : "?");
        if (r->code == PROBE_FAIL_UPDATE) {
            n = put(buf, cap, n, " (");
            n = put(buf, cap, n, r->arg < sizeof(UPDATE) / sizeof(UPDATE[0]) ? UPDATE[r->arg] : "?");
            n = put(buf, cap, n, ")");
        }
        return n;
    case EVT_BUS: {
        char v[16];
        snprintf(v, sizeof(v), "%lu.%02lu V", (unsigned long)r->arg / 1000, (unsigned long)(r->arg % 1000) / 10);
        n = put(buf, cap, n, r->code == BUS_LOW ? "bus low: " : "bus recovered: ");
        return put(buf, cap, n, v);
    }
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
