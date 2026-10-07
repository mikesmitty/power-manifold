#include "update_auto.h"

#include <string.h>

#include "civil_time.h"

update_auto_t update_auto_decide(const update_auto_in_t *in) {
    if (!in->enabled) return UPDATE_AUTO_OFF;
    if (!in->latest[0]) return UPDATE_AUTO_NONE;
    if (!strcmp(in->latest, in->skip)) return UPDATE_AUTO_SKIPPED;
    // A postponement made while the clock was set holds until the clock is
    // set again after a restart.
    if (in->postpone_until && (!in->epoch || in->epoch < in->postpone_until))
        return UPDATE_AUTO_POSTPONED;
    if (in->now_ms - in->seen_ms < in->wait_days * UPDATE_AUTO_DAY_MS) return UPDATE_AUTO_HOLD;
    if (in->last_try_ms && in->now_ms - in->last_try_ms < UPDATE_AUTO_RETRY_MS) return UPDATE_AUTO_RETRY;
    if (in->epoch) {
        uint16_t m = civil_local_minute(in->epoch, in->tz_offset_min);
        if (m < UPDATE_AUTO_WINDOW_START || m >= UPDATE_AUTO_WINDOW_END) return UPDATE_AUTO_WINDOW;
    }
    return UPDATE_AUTO_INSTALL;
}

uint8_t update_auto_wait_days(uint32_t random) {
    return (uint8_t)(UPDATE_AUTO_WAIT_DAYS_MIN + random % (UPDATE_AUTO_WAIT_DAYS_MAX - UPDATE_AUTO_WAIT_DAYS_MIN + 1));
}

const char *update_auto_name(update_auto_t s) {
    switch (s) {
    case UPDATE_AUTO_OFF:       return "off";
    case UPDATE_AUTO_NONE:      return "none";
    case UPDATE_AUTO_SKIPPED:   return "skipped";
    case UPDATE_AUTO_POSTPONED: return "postponed";
    case UPDATE_AUTO_HOLD:      return "waiting";
    case UPDATE_AUTO_RETRY:     return "retry";
    case UPDATE_AUTO_WINDOW:    return "scheduled";
    case UPDATE_AUTO_INSTALL:   return "installing";
    }
    return "none";
}
