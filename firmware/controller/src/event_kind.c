#include "event_kind.h"

const char *event_kind(const engine_evt_t *e) {
    switch ((evt_type_t)e->type) {
    case EVT_STATE_CHANGE: {
        port_state_t to = (port_state_t)e->code, from = (port_state_t)e->arg;
        switch (to) {
        case PORT_STATE_PROBE:
            if (from == PORT_STATE_ABSENT) return "inserted";
            if (from == PORT_STATE_DISABLED) return "enabled";
            return ""; // FAULT -> PROBE: a recovery attempt; "ready" follows if it works
        case PORT_STATE_IDLE:
            if (from == PORT_STATE_PROBE) return "ready";
            if (from == PORT_STATE_ACTIVE || from == PORT_STATE_THROTTLED) return "detached";
            return "";
        case PORT_STATE_ACTIVE:
            return from == PORT_STATE_IDLE ? "attached" : ""; // THROTTLED -> ACTIVE is "restored"
        case PORT_STATE_ABSENT:
            return "removed";
        case PORT_STATE_DISABLED:
            return "disabled";
        default:
            return ""; // FAULT and THROTTLED come with EVT_FAULT / EVT_THROTTLE
        }
    }
    case EVT_FAULT:      return "fault";
    case EVT_CONTRACT:   return "contract";
    case EVT_PROBE_FAIL: return "probe_failed";
    case EVT_THROTTLE:   return e->code == THROTTLE_RESTORED ? "restored" : "throttled";
    case EVT_CHARGE:
        return e->code == CHARGE_DONE ? "charged" : e->code == CHARGE_RESUMED ? "charging"
                                                                               : "auto_off";
    default:             return "";
    }
}
