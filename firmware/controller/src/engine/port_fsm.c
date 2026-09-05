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

// Power-up stagger between the blades found seated at boot (port_fsm.h).
// Long enough for one sink's inrush and its first PD negotiation to settle
// before the next blade's EN rises; six blades take 1.25 s in all.
#define BOOT_STAGGER_MS 250

// Partial unthrottle: a throttled port takes freed budget in steps of at
// least this much (avoids renegotiation churn over crumbs), no more than one
// step per holdoff period.
#define UNTHROTTLE_STEP_MIN_MW     5000
#define UNTHROTTLE_STEP_HOLDOFF_MS 1000

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
    uint32_t enable_after_ms; // boot stagger: no probe before this (0 = none)
    uint32_t step_after_ms;  // next partial unthrottle step allowed at
    uint32_t attached_at_ms; // sleep timer base
    uint32_t side_since_ms;  // draw has been on the current side of the charged floor since
    bool     low_side;       // ...and that side is "under the floor"
    bool     charged;
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
    ctx[i].enable_after_ms = 0; // the boot slot is spent; later seatings are immediate
    ctx[i].denied_mw = 0; // stale asks must not inflate a new throttle epoch
    ctx[i].step_after_ms = 0;
    enter(i, PORT_STATE_PROBE);
}

// One probe attempt per tick keeps the loop cadence flat.
static void do_probe(uint8_t i, uint32_t now_ms) {
    uint16_t fail = 0;
    if (!tca9548a_select(i)) {
        fail = PROBE_FAIL_MUX;
    } else if (!ina226_probe() || !ina226_configure() ||
               !ina226_set_alert_ma((PORT_HW_MAX_MA * 125) / 100)) { // emergency trip, fixed
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

static uint8_t prio(uint8_t i) {
    return g_settings.port_priority[i]; // 0 = highest
}

// Program port i's advertised current ceiling so its contract fits grant_mw,
// take the reservation, and keep want_mw as the recovery target. evt_code
// distinguishes a clamp from a partial step back up. The caller must have
// port i's mux channel selected.
static void apply_throttle(uint8_t i, uint32_t grant_mw, uint32_t want_mw,
                           uint16_t evt_code) {
    uint32_t mv = ctx[i].ina.bus_mv ? ctx[i].ina.bus_mv : 5000;
    uint32_t ma = (grant_mw * 1000) / mv;
    if (ma < 500) ma = 500;

    mpq4242_set_max_current_ma(ma);
    mpq4242_send_src_cap();
    ctx[i].granted_ma = ma;
    budget_force_reserve(i, grant_mw);
    ctx[i].contract_mw = grant_mw;
    if (want_mw > ctx[i].denied_mw) ctx[i].denied_mw = want_mw;
    emit(EVT_THROTTLE, i, evt_code, grant_mw);
    enter(i, PORT_STATE_THROTTLED);
}

// Reduce this port's own current limit so its contract fits the headroom left
// by everyone else. Last resort, after shed_lower_priority() found no victims.
static void throttle(uint8_t i, uint32_t want_mw) {
    uint32_t grant_mw = budget_headroom() + budget_port_reservation(i);
    if (grant_mw < BUDGET_BASE_RESERVE_MW) grant_mw = BUDGET_BASE_RESERVE_MW;
    if (grant_mw > want_mw) grant_mw = want_mw;
    apply_throttle(i, grant_mw, want_mw, THROTTLE_CLAMPED);
}

// Make room for claimant's want_mw by clamping strictly lower-priority powered
// ports, worst priority first (ties: biggest reservation first). Equal
// priority never sheds — first come, first served among peers. Victims keep
// their original ask in denied_mw and recover through the same path as a
// self-clamped port. Restores the claimant's mux channel before returning.
static void shed_lower_priority(uint8_t claimant, uint32_t want_mw) {
    uint32_t others = budget_reserved() - budget_port_reservation(claimant);
    if (others + want_mw <= budget_total()) return;
    uint32_t shortfall = others + want_mw - budget_total();

    bool tried[NUM_PORTS] = {false};
    while (shortfall > 0) {
        int v = -1;
        for (uint8_t j = 0; j < NUM_PORTS; j++) {
            if (j == claimant || tried[j]) continue;
            if (ctx[j].state != PORT_STATE_ACTIVE &&
                ctx[j].state != PORT_STATE_THROTTLED)
                continue;
            if (prio(j) <= prio(claimant)) continue;
            if (budget_port_reservation(j) <= BUDGET_BASE_RESERVE_MW) continue;
            if (v < 0 || prio(j) > prio((uint8_t)v) ||
                (prio(j) == prio((uint8_t)v) &&
                 budget_port_reservation(j) > budget_port_reservation((uint8_t)v)))
                v = j;
        }
        if (v < 0) break; // nobody left to shed; caller clamps itself

        tried[v] = true;
        uint32_t res = budget_port_reservation((uint8_t)v);
        uint32_t reclaim = res - BUDGET_BASE_RESERVE_MW;
        if (reclaim > shortfall) reclaim = shortfall;
        if (!tca9548a_select((uint8_t)v)) continue; // unreachable: skip it

        apply_throttle((uint8_t)v, res - reclaim, ctx[v].contract_mw,
                       THROTTLE_CLAMPED);
        shortfall -= reclaim;
    }
    tca9548a_select(claimant);
}

// Any strictly higher-priority throttled port that still wants more goes
// first: since recovery can happen in partial steps, it can use whatever
// headroom exists, so freed watts always flow top-down by priority.
static bool recovery_should_yield(uint8_t i) {
    for (uint8_t j = 0; j < NUM_PORTS; j++) {
        if (j == i || ctx[j].state != PORT_STATE_THROTTLED) continue;
        if (prio(j) >= prio(i)) continue;
        if (ctx[j].denied_mw > budget_port_reservation(j)) return true;
    }
    return false;
}

// Restore the full advertisement. The caller has already claimed the budget
// for the recovered contract, so a rival can't take it mid-renegotiation.
static void unthrottle(uint8_t i) {
    mpq4242_set_max_current_ma(g_settings.port_limit_ma[i]);
    mpq4242_send_src_cap();
    ctx[i].granted_ma = g_settings.port_limit_ma[i];
    emit(EVT_THROTTLE, i, THROTTLE_RESTORED, ctx[i].contract_mw);
    enter(i, PORT_STATE_ACTIVE);
}

// The port switches itself off: an admin disable the engine initiates, so
// core 0 records it like any other (main.c) for the "last" boot policy.
static void auto_off(uint8_t i, uint32_t why) {
    emit(EVT_CHARGE, i, CHARGE_AUTO_OFF, why);
    ctx[i].admin_enabled = false;
    power_down(i, PORT_STATE_DISABLED);
}

static void attach_begin(uint8_t i, uint32_t now_ms) {
    ctx[i].attached_at_ms = now_ms;
    ctx[i].side_since_ms = now_ms;
    ctx[i].low_side = false;
    ctx[i].charged = false;
}

// Charge-complete detection and the sleep timer, every tick while a sink is
// attached. "Charged" means the measured draw stayed under settings
// charged_mw for charged_min; it clears symmetrically (draw back above the
// floor for as long), so a device that drains while plugged in reads as
// charging again. Returns false when the port switched itself off.
static bool charge_track(uint8_t i, uint32_t now_ms) {
    port_ctx_t *p = &ctx[i];
    uint32_t floor = g_settings.charged_mw;
    if (floor) {
        bool low = p->ina.power_mw < floor;
        if (low != p->low_side) {
            p->low_side = low;
            p->side_since_ms = now_ms;
        } else if (now_ms - p->side_since_ms >= (uint32_t)g_settings.charged_min * 60000u &&
                   low != p->charged) {
            p->charged = low;
            emit(EVT_CHARGE, i, low ? CHARGE_DONE : CHARGE_RESUMED,
                 (now_ms - p->attached_at_ms) / 60000u);
            if (low && (g_settings.port_auto_off & (1u << i))) {
                auto_off(i, AUTO_OFF_CHARGED);
                return false;
            }
        }
    }
    uint32_t sleep = g_settings.port_sleep_min[i];
    if (sleep && now_ms - p->attached_at_ms >= sleep * 60000u) {
        auto_off(i, AUTO_OFF_SLEEP);
        return false;
    }
    return true;
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

    if (!budget_try_reserve(i, want)) {
        shed_lower_priority(i, want);
        if (!budget_try_reserve(i, want)) {
            throttle(i, want);
            return;
        }
    }
    ctx[i].contract_mw = want;
    emit(EVT_CONTRACT, i, ctx[i].mpq.selected_pdo, want);
}

// Administrative state at power-up, per the port's boot policy (settings)
static bool boot_enabled(uint8_t i) {
    switch (g_settings.port_boot[i]) {
    case PORT_BOOT_OFF:  return false;
    case PORT_BOOT_LAST: return !(g_settings.port_off_mask & (1u << i));
    default:             return true;
    }
}

void port_fsm_init(void) {
    memset(ctx, 0, sizeof(ctx));
    for (uint8_t i = 0; i < NUM_PORTS; i++) {
        ctx[i].state = PORT_STATE_ABSENT;
        ctx[i].admin_enabled = boot_enabled(i);
    }
}

void port_fsm_boot_inventory(const bool *present, uint32_t now_ms) {
    for (uint8_t i = 0; i < NUM_PORTS; i++) {
        ctx[i].enable_after_ms = 0;
        if (!present[i] || !ctx[i].admin_enabled) continue;
        // rank among the seated, enabled blades: better priority first,
        // slot order among equals
        unsigned rank = 0;
        for (uint8_t j = 0; j < NUM_PORTS; j++) {
            if (j == i || !present[j] || !ctx[j].admin_enabled) continue;
            if (prio(j) < prio(i) || (prio(j) == prio(i) && j < i)) rank++;
        }
        ctx[i].enable_after_ms = now_ms + rank * BOOT_STAGGER_MS;
    }
}

void port_fsm_tick(uint8_t i, bool present, uint32_t now_ms,
                   port_telemetry_t *out) {
    port_ctx_t *p = &ctx[i];

    switch (p->state) {
    case PORT_STATE_ABSENT:
        if (present) {
            if (!p->admin_enabled) enter(i, PORT_STATE_DISABLED);
            else if ((int32_t)(now_ms - p->enable_after_ms) >= 0) start_probe(i);
            // else: waiting for its boot slot
        }
        break;

    case PORT_STATE_PROBE:
        if (!present) { power_down(i, PORT_STATE_ABSENT); break; }
        do_probe(i, now_ms);
        break;

    case PORT_STATE_IDLE:
        if (!poll_powered(i, present, now_ms)) break;
        if (p->mpq.attached) {
            attach_begin(i, now_ms);
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
        if (!charge_track(i, now_ms)) break;
        if (p->state == PORT_STATE_THROTTLED && p->denied_mw) {
            // Freed budget flows back by priority. When everything the port
            // was refused fits, claim it and restore the full advertisement;
            // when only part of it does, step the clamp up by the available
            // headroom (rate-limited) and let the sink renegotiate upward —
            // recovery no longer waits for the full ask to fit at once.
            uint32_t res = budget_port_reservation(i);
            uint32_t need = p->denied_mw > res ? p->denied_mw - res : 0;
            uint32_t headroom = budget_headroom();
            if (need && !recovery_should_yield(i)) {
                if (headroom >= need) {
                    budget_force_reserve(i, p->denied_mw);
                    p->contract_mw = p->denied_mw;
                    p->denied_mw = 0;
                    unthrottle(i);
                } else if (headroom >= UNTHROTTLE_STEP_MIN_MW &&
                           (int32_t)(now_ms - p->step_after_ms) >= 0) {
                    apply_throttle(i, res + headroom, p->denied_mw,
                                   THROTTLE_STEP);
                    p->step_after_ms = now_ms + UNTHROTTLE_STEP_HOLDOFF_MS;
                }
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
    out->charged = p->mpq.attached && p->charged;
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
    case CMD_PORT_LIMIT: {
        // core 0 stored the new setting first; a port that is not powered
        // picks it up when it next probes. The INA226 trip stays put.
        uint32_t limit = cmd->arg;
        if (!powered || !tca9548a_select(i)) break;
        if (ctx[i].state == PORT_STATE_THROTTLED && ctx[i].granted_ma <= limit)
            break; // the budget clamp is tighter; recovery restores to the new limit
        mpq4242_set_max_current_ma(limit);
        ctx[i].granted_ma = limit;
        if (ctx[i].mpq.attached) mpq4242_send_src_cap(); // renegotiate now
        break;
    }
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
