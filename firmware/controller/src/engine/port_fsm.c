#include "port_fsm.h"

#include <string.h>

#include "budget.h"
#include "ina226.h"
#include "ipc.h"
#include "mpq4242.h"
#include "settings.h"
#include "tca9539.h"
#include "tca9548a.h"

#define FAULT_COOLDOWN_MS 5000
#define PROBE_MAX_ATTEMPTS 3

// EVT_PROBE_FAIL codes
#define PROBE_FAIL_MUX     1
#define PROBE_FAIL_INA226  2
#define PROBE_FAIL_MPQ4242 3
#define PROBE_FAIL_EN      4

typedef struct {
    port_state_t state;
    bool     admin_enabled;
    uint8_t  probe_attempts;
    uint8_t  fault_bits;     // latched for diagnostics until next probe
    uint32_t cooldown_until_ms;
    uint32_t contract_mw;
    uint32_t denied_mw;      // contract that budget refused; unthrottle target
    uint32_t granted_ma;     // current ceiling currently programmed
    mpq4242_status_t mpq;
    ina226_reading_t ina;
} port_ctx_t;

static port_ctx_t ctx[NUM_PORTS];

const char *port_state_name(port_state_t s) {
    switch (s) {
    case PORT_STATE_ABSENT:    return "absent";
    case PORT_STATE_PROBE:     return "probe";
    case PORT_STATE_IDLE:      return "idle";
    case PORT_STATE_ACTIVE:    return "active";
    case PORT_STATE_THROTTLED: return "throttled";
    case PORT_STATE_FAULT:     return "fault";
    case PORT_STATE_DISABLED:  return "disabled";
    default:                   return "?";
    }
}

static void emit(uint8_t type, uint8_t port, uint16_t code, uint32_t arg) {
    engine_evt_t e = {.type = type, .port = port, .code = code, .arg = arg};
    ipc_evt_push(&e); // best-effort; dropped events only cost visibility
}

static void enter(uint8_t i, port_state_t next) {
    if (ctx[i].state == next) return;
    emit(EVT_STATE_CHANGE, i, (uint16_t)next, (uint16_t)ctx[i].state);
    ctx[i].state = next;
}

static void power_down(uint8_t i, port_state_t next) {
    tca9539_set_en(i, false);
    budget_release(i);
    ctx[i].contract_mw = 0;
    memset(&ctx[i].mpq, 0, sizeof(ctx[i].mpq));
    memset(&ctx[i].ina, 0, sizeof(ctx[i].ina));
    enter(i, next);
}

static void fault(uint8_t i, uint32_t now_ms, uint8_t fault_bits, uint32_t detail) {
    ctx[i].fault_bits = fault_bits ? fault_bits : ctx[i].fault_bits;
    emit(EVT_FAULT, i, fault_bits, detail);
    power_down(i, PORT_STATE_FAULT);
    ctx[i].cooldown_until_ms = now_ms + FAULT_COOLDOWN_MS;
}

static void start_probe(uint8_t i) {
    ctx[i].probe_attempts = 0;
    ctx[i].fault_bits = 0;
    enter(i, PORT_STATE_PROBE);
}

// One probe attempt per tick keeps the loop cadence flat.
static void do_probe(uint8_t i, uint32_t now_ms) {
    uint16_t fail = 0;
    if (!tca9548a_select(i)) {
        fail = PROBE_FAIL_MUX;
    } else if (!ina226_probe() || !ina226_configure() ||
               !ina226_set_alert_ma((g_settings.port_limit_ma[i] * 110) / 100)) {
        fail = PROBE_FAIL_INA226;
    } else if (!mpq4242_probe() ||
               !mpq4242_configure(g_settings.port_limit_ma[i])) {
        fail = PROBE_FAIL_MPQ4242;
    } else if (!tca9539_set_en(i, true)) {
        fail = PROBE_FAIL_EN;
    }

    if (!fail) {
        ctx[i].granted_ma = g_settings.port_limit_ma[i];
        budget_force_reserve(i, BUDGET_BASE_RESERVE_MW);
        enter(i, PORT_STATE_IDLE);
        return;
    }

    if (++ctx[i].probe_attempts >= PROBE_MAX_ATTEMPTS) {
        emit(EVT_PROBE_FAIL, i, fail, ctx[i].probe_attempts);
        fault(i, now_ms, 0, fail);
    }
}

// Reduce a port's programmed current limit so its contract fits the headroom
// left by everyone else. Placeholder policy: renegotiation currently clamps
// this port only; picking cheaper victims by priority is a TODO.
static void throttle(uint8_t i, uint32_t want_mw) {
    uint32_t grant_mw = budget_headroom() + budget_port_reservation(i);
    if (grant_mw < BUDGET_BASE_RESERVE_MW) grant_mw = BUDGET_BASE_RESERVE_MW;
    if (grant_mw > want_mw) grant_mw = want_mw;

    uint32_t mv = ctx[i].ina.bus_mv ? ctx[i].ina.bus_mv : 5000;
    uint32_t ma = (grant_mw * 1000) / mv;
    if (ma < 500) ma = 500;

    mpq4242_set_max_current_ma(ma);
    mpq4242_send_src_cap();
    ctx[i].granted_ma = ma;
    budget_force_reserve(i, grant_mw);
    ctx[i].contract_mw = grant_mw;
    ctx[i].denied_mw = want_mw;
    emit(EVT_THROTTLE, i, 0, grant_mw);
    enter(i, PORT_STATE_THROTTLED);
}

static void unthrottle(uint8_t i) {
    mpq4242_set_max_current_ma(g_settings.port_limit_ma[i]);
    mpq4242_send_src_cap();
    ctx[i].granted_ma = g_settings.port_limit_ma[i];
    enter(i, PORT_STATE_ACTIVE);
}

// Shared telemetry + fault polling for powered states. Returns false if the
// port just faulted or was depowered.
static bool poll_powered(uint8_t i, bool present, uint32_t now_ms) {
    if (!present) {
        power_down(i, PORT_STATE_ABSENT);
        return false;
    }
    if (!tca9548a_select(i)) return true; // transient; retry next tick

    ina226_read(&ctx[i].ina);
    if (!mpq4242_read_status(&ctx[i].mpq)) return true;

    if (ctx[i].mpq.fault_bits) {
        fault(i, now_ms, ctx[i].mpq.fault_bits, 0);
        return false;
    }
    return true;
}

static void track_contract(uint8_t i) {
    uint32_t want = ctx[i].mpq.contract_mw;
    if (want < BUDGET_BASE_RESERVE_MW) want = BUDGET_BASE_RESERVE_MW;
    if (want == ctx[i].contract_mw) return;

    if (budget_try_reserve(i, want)) {
        ctx[i].contract_mw = want;
        emit(EVT_CONTRACT, i, ctx[i].mpq.selected_pdo, want);
    } else {
        throttle(i, want);
    }
}

void port_fsm_init(void) {
    memset(ctx, 0, sizeof(ctx));
    for (int i = 0; i < NUM_PORTS; i++) {
        ctx[i].state = PORT_STATE_ABSENT;
        ctx[i].admin_enabled = true;
    }
}

void port_fsm_tick(uint8_t i, bool present, uint32_t now_ms,
                   port_telemetry_t *out) {
    port_ctx_t *p = &ctx[i];

    switch (p->state) {
    case PORT_STATE_ABSENT:
        if (present) {
            if (p->admin_enabled) start_probe(i);
            else enter(i, PORT_STATE_DISABLED);
        }
        break;

    case PORT_STATE_PROBE:
        if (!present) { power_down(i, PORT_STATE_ABSENT); break; }
        do_probe(i, now_ms);
        break;

    case PORT_STATE_IDLE:
        if (!poll_powered(i, present, now_ms)) break;
        if (p->mpq.attached) {
            enter(i, PORT_STATE_ACTIVE);
            track_contract(i);
        }
        break;

    case PORT_STATE_ACTIVE:
    case PORT_STATE_THROTTLED:
        if (!poll_powered(i, present, now_ms)) break;
        if (!p->mpq.attached) {
            budget_force_reserve(i, BUDGET_BASE_RESERVE_MW);
            p->contract_mw = 0;
            p->denied_mw = 0;
            if (p->granted_ma != g_settings.port_limit_ma[i]) {
                mpq4242_set_max_current_ma(g_settings.port_limit_ma[i]);
                p->granted_ma = g_settings.port_limit_ma[i];
            }
            enter(i, PORT_STATE_IDLE);
            break;
        }
        track_contract(i);
        if (p->state == PORT_STATE_THROTTLED && p->denied_mw) {
            // when other ports release enough budget to cover the contract
            // that was refused, restore the full advertisement and let the
            // sink renegotiate upward
            uint32_t res = budget_port_reservation(i);
            if (p->denied_mw > res && budget_headroom() >= p->denied_mw - res) {
                unthrottle(i);
                p->denied_mw = 0;
            }
        }
        break;

    case PORT_STATE_FAULT:
        if (!present) { enter(i, PORT_STATE_ABSENT); break; }
        if (p->admin_enabled && now_ms >= p->cooldown_until_ms) start_probe(i);
        break;

    case PORT_STATE_DISABLED:
        if (!present) enter(i, PORT_STATE_ABSENT);
        else if (p->admin_enabled) start_probe(i);
        break;

    default:
        enter(i, PORT_STATE_ABSENT);
        break;
    }

    out->state = (uint8_t)p->state;
    out->attached = p->mpq.attached;
    out->selected_pdo = p->mpq.selected_pdo;
    out->fault_bits = p->fault_bits;
    out->bus_mv = p->ina.bus_mv;
    out->current_ma = p->ina.current_ma;
    out->power_mw = p->ina.power_mw;
    out->contract_mw = budget_port_reservation(i);
}

void port_fsm_cmd(uint8_t i, const engine_cmd_t *cmd) {
    bool powered = ctx[i].state == PORT_STATE_IDLE ||
                   ctx[i].state == PORT_STATE_ACTIVE ||
                   ctx[i].state == PORT_STATE_THROTTLED;

    switch ((cmd_op_t)cmd->op) {
    case CMD_PORT_ENABLE:
        ctx[i].admin_enabled = true;
        break;
    case CMD_PORT_DISABLE:
        ctx[i].admin_enabled = false;
        if (ctx[i].state != PORT_STATE_ABSENT) power_down(i, PORT_STATE_DISABLED);
        break;
    case CMD_PORT_HARD_RESET:
        if (powered && tca9548a_select(i)) mpq4242_send_hard_reset();
        break;
    case CMD_PORT_SRC_CAP:
        if (powered && tca9548a_select(i)) mpq4242_send_src_cap();
        break;
    default:
        break;
    }
}

void port_fsm_alert_sweep(uint32_t now_ms) {
    for (uint8_t i = 0; i < NUM_PORTS; i++) {
        if (ctx[i].state != PORT_STATE_IDLE && ctx[i].state != PORT_STATE_ACTIVE &&
            ctx[i].state != PORT_STATE_THROTTLED)
            continue;
        if (!tca9548a_select(i)) continue;

        bool ina_trip = false;
        ina226_alert_tripped(&ina_trip);
        mpq4242_status_t st;
        bool mpq_ok = mpq4242_read_status(&st);

        if (ina_trip || (mpq_ok && st.fault_bits)) {
            fault(i, now_ms, mpq_ok ? st.fault_bits : 0, ina_trip);
        }
    }
}
