#include <string.h>

#include "event_kind.h"
#include "manifold.h"
#include "microtest.h"

static const char *kind(uint8_t type, uint16_t code, uint32_t arg) {
    engine_evt_t e = {.type = type, .port = 0, .code = code, .arg = arg};
    return event_kind(&e);
}

static void test_state_changes(void) {
    MT_ASSERT(!strcmp(kind(EVT_STATE_CHANGE, PORT_STATE_PROBE, PORT_STATE_ABSENT), "inserted"));
    MT_ASSERT(!strcmp(kind(EVT_STATE_CHANGE, PORT_STATE_IDLE, PORT_STATE_PROBE), "ready"));
    MT_ASSERT(!strcmp(kind(EVT_STATE_CHANGE, PORT_STATE_ACTIVE, PORT_STATE_IDLE), "attached"));
    MT_ASSERT(!strcmp(kind(EVT_STATE_CHANGE, PORT_STATE_IDLE, PORT_STATE_ACTIVE), "detached"));
    MT_ASSERT(!strcmp(kind(EVT_STATE_CHANGE, PORT_STATE_IDLE, PORT_STATE_THROTTLED), "detached"));
    MT_ASSERT(!strcmp(kind(EVT_STATE_CHANGE, PORT_STATE_ABSENT, PORT_STATE_ACTIVE), "removed"));
    MT_ASSERT(!strcmp(kind(EVT_STATE_CHANGE, PORT_STATE_DISABLED, PORT_STATE_IDLE), "disabled"));
    MT_ASSERT(!strcmp(kind(EVT_STATE_CHANGE, PORT_STATE_PROBE, PORT_STATE_DISABLED), "enabled"));
    // transitions that ride along with a richer event stay silent
    MT_ASSERT(!strcmp(kind(EVT_STATE_CHANGE, PORT_STATE_FAULT, PORT_STATE_ACTIVE), ""));
    MT_ASSERT(!strcmp(kind(EVT_STATE_CHANGE, PORT_STATE_THROTTLED, PORT_STATE_ACTIVE), ""));
    MT_ASSERT(!strcmp(kind(EVT_STATE_CHANGE, PORT_STATE_ACTIVE, PORT_STATE_THROTTLED), ""));
    MT_ASSERT(!strcmp(kind(EVT_STATE_CHANGE, PORT_STATE_PROBE, PORT_STATE_FAULT), ""));
}

static void test_other_events(void) {
    MT_ASSERT(!strcmp(kind(EVT_FAULT, MPQ_FAULT_OTW1, 0), "fault"));
    MT_ASSERT(!strcmp(kind(EVT_CONTRACT, 5, 60000), "contract"));
    MT_ASSERT(!strcmp(kind(EVT_PROBE_FAIL, 3, 3), "probe_failed"));
    MT_ASSERT(!strcmp(kind(EVT_THROTTLE, THROTTLE_CLAMPED, 45000), "throttled"));
    MT_ASSERT(!strcmp(kind(EVT_THROTTLE, THROTTLE_STEP, 60000), "throttled"));
    MT_ASSERT(!strcmp(kind(EVT_THROTTLE, THROTTLE_RESTORED, 100000), "restored"));
    MT_ASSERT(!strcmp(kind(EVT_CHARGE, CHARGE_DONE, 42), "charged"));
    MT_ASSERT(!strcmp(kind(EVT_CHARGE, CHARGE_RESUMED, 50), "charging"));
    MT_ASSERT(!strcmp(kind(EVT_CHARGE, CHARGE_AUTO_OFF, AUTO_OFF_SLEEP), "auto_off"));
    MT_ASSERT(!strcmp(kind(EVT_BOOT, 0, 0), "")); // never reaches MQTT anyway
}

// Every kind the firmware can emit must be in the list discovery publishes,
// or Home Assistant drops the event with a warning.
static void test_every_kind_is_published(void) {
    char quoted[32];
    for (uint16_t to = 0; to < PORT_STATE_COUNT; to++) {
        for (uint32_t from = 0; from < PORT_STATE_COUNT; from++) {
            const char *k = kind(EVT_STATE_CHANGE, to, from);
            if (!*k) continue;
            snprintf(quoted, sizeof(quoted), "\"%s\"", k);
            MT_ASSERT(strstr(EVENT_KINDS_JSON, quoted) != NULL);
        }
    }
    const uint8_t types[] = {EVT_FAULT, EVT_CONTRACT, EVT_PROBE_FAIL, EVT_THROTTLE, EVT_CHARGE};
    for (size_t i = 0; i < sizeof(types); i++) {
        for (uint16_t code = 0; code < 3; code++) {
            const char *k = kind(types[i], code, 0);
            MT_ASSERT(*k != '\0');
            snprintf(quoted, sizeof(quoted), "\"%s\"", k);
            MT_ASSERT(strstr(EVENT_KINDS_JSON, quoted) != NULL);
        }
    }
}

void run_event_tests(void) {
    mt_run("events: state changes map to HA event types", test_state_changes);
    mt_run("events: fault/contract/probe/throttle kinds", test_other_events);
    mt_run("events: every emitted kind is in the discovery list", test_every_kind_is_published);
}
