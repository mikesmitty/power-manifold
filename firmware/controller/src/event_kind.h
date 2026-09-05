#pragma once

#include "manifold.h"

// Home Assistant event type for an engine event: what happened to the port
// in one word ("attached", "fault", ...). "" for transitions that carry no
// meaning of their own — a state change into FAULT or THROTTLED, which
// arrive alongside their own richer events. Hardware-free.
const char *event_kind(const engine_evt_t *e);

// Every kind event_kind() can return, as the JSON list discovery publishes
// (HA rejects an event whose type is not in the entity's list).
#define EVENT_KINDS_JSON                                                       \
    "[\"inserted\",\"ready\",\"attached\",\"detached\",\"removed\",\"enabled\"," \
    "\"disabled\",\"contract\",\"throttled\",\"restored\",\"fault\",\"probe_failed\"," \
    "\"charged\",\"charging\",\"auto_off\"]"
