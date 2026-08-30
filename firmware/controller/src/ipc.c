#include "ipc.h"

#include "hardware/sync.h"
#include "pico/time.h"
#include "pico/util/queue.h"

#define CMD_QUEUE_DEPTH 16
#define EVT_QUEUE_DEPTH 32
#define HEARTBEAT_FRESH_US (200 * 1000) // engine ticks at 100 Hz; 20 missed = dead

static queue_t cmd_q;
static queue_t evt_q;

static volatile uint32_t snap_seq;
static telemetry_t snap;

static volatile uint32_t heartbeat_ms;
static volatile bool heartbeat_seen;

void ipc_init(void) {
    queue_init(&cmd_q, sizeof(engine_cmd_t), CMD_QUEUE_DEPTH);
    queue_init(&evt_q, sizeof(engine_evt_t), EVT_QUEUE_DEPTH);
}

bool ipc_cmd_push(const engine_cmd_t *cmd) {
    return queue_try_add(&cmd_q, cmd);
}

bool ipc_cmd_pop(engine_cmd_t *cmd) {
    return queue_try_remove(&cmd_q, cmd);
}

bool ipc_evt_push(const engine_evt_t *evt) {
    return queue_try_add(&evt_q, evt);
}

bool ipc_evt_pop(engine_evt_t *evt) {
    return queue_try_remove(&evt_q, evt);
}

void ipc_snapshot_publish(const telemetry_t *t) {
    snap_seq++; // odd: write in progress
    __dmb();
    snap = *t;
    __dmb();
    snap_seq++;
}

void ipc_snapshot_read(telemetry_t *t) {
    uint32_t s1, s2;
    do {
        s1 = snap_seq;
        if (s1 & 1u) continue;
        __dmb();
        *t = snap;
        __dmb();
        s2 = snap_seq;
    } while (s1 != s2);
}

void ipc_engine_heartbeat(void) {
    heartbeat_ms = to_ms_since_boot(get_absolute_time());
    heartbeat_seen = true;
}

bool ipc_engine_alive(void) {
    if (!heartbeat_seen) return false;
    uint32_t age = to_ms_since_boot(get_absolute_time()) - heartbeat_ms;
    return age < (HEARTBEAT_FRESH_US / 1000);
}
