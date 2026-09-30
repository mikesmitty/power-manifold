#include "port_fsm.h"

#include <string.h>

#include "blade.h"
#include "blade_bundle.h"
#include "blade_regs.h"
#include "blade_update.h"
#include "budget.h"
#include "ipc.h"
#include "settings.h"
#include "tca9539.h"
#include "tca9548a.h"

#define FAULT_COOLDOWN_MS 5000
#define PROBE_MAX_ATTEMPTS 3
// Trips through a gen-3 blade's ROM bootloader (blade_update.h) without its
// firmware coming up and staying up on the version wanted. Past this the
// port is held in FAULT until it is reseated, re-enabled or told to update.
#define UPDATE_ROUNDS_MAX 3
// After telling a blade to reset into its bootloader (or the bootloader to
// start the firmware), the probe waits this long for the other party to
// answer before it starts counting failed attempts.
#define LOADER_SWITCH_MS 200
// A powered blade that has not answered a poll for this many ticks is
// silent (blade_silent): left powered, and looked for again this often.
#define SILENT_POLLS_MAX 10
#define SILENT_RECHECK_MS 1000
// What the controller wants done to a running blade's firmware (the bundled
// version, the boot option) takes the port down for a few seconds, so it
// waits until nothing has been plugged into the port for this long.
#ifndef UPDATE_IDLE_MS
#define UPDATE_IDLE_MS 10000
#endif
#ifndef BLADE_WAKE_MS // the host tests set 0: the wait is hardware, not FSM logic
#define BLADE_WAKE_MS 50 // EN high -> a gen-2 blade's MPQ4242 LDO + digital core up before the first I2C word
#endif

// Power-up stagger between the blades found seated at boot (port_fsm.h).
// Long enough for one sink's inrush and its first PD negotiation to settle
// before the next blade's EN rises; six blades take 1.25 s in all.
#define BOOT_STAGGER_MS 250
// Adoption pace at a warm start (port_fsm_boot_inventory): a powered blade
// has nothing to inrush, but adopting them one at a time in priority order,
// each far enough apart to reach ACTIVE and claim its contract before the
// next one holds a base reserve, lets the budget settle the way a cold boot
// would — the best port wins when the budget cannot hold everything.
#define WARM_STAGGER_MS 50

// Partial unthrottle: a throttled port takes freed budget in steps of at
// least this much (avoids renegotiation churn over crumbs), no more than one
// step per holdoff period.
#define UNTHROTTLE_STEP_MIN_MW     5000
#define UNTHROTTLE_STEP_HOLDOFF_MS 1000

typedef struct {
    port_state_t state;
    blade_gen_t gen;         // which generation answered the probe
    bool     admin_enabled;
    bool     warm;           // adopted at a warm start: EN is already on, probe without touching it
    uint8_t  probe_attempts;
    uint8_t  silent_polls;   // polls in a row the blade did not answer
    bool     silent;         // powered and not answering: left as it is, looked for again
    uint32_t silent_check_ms; // ...next at
    bool     id_ok;          // gen 3: id is what the firmware said of itself at the probe
    blade_identity_t id;
    uint32_t idle_since_ms;  // IDLE: since when
    uint16_t fault_bits;     // PORT_FAULT_*, latched for diagnostics until next probe
    uint32_t cooldown_until_ms;
    uint32_t enable_after_ms; // boot stagger: no probe before this (0 = none)
    uint32_t en_on_ms;        // cold probe: when EN went high (0 = not yet)
    uint32_t step_after_ms;  // next partial unthrottle step allowed at
    uint32_t attached_at_ms; // sleep timer base
    uint32_t switch_until_ms; // probe attempts are not counted before this (the blade is changing mode)
    bool     switch_grace;    // ...to be set from the next probe's clock: the mode change was just asked for
    uint8_t  update_rounds;  // bootloader trips since the firmware last came up as wanted
    bool     update_rewrite; // the next trip programs the bundle whatever the blade holds (CMD_PORT_UPDATE)
    bool     held;           // FAULT with no automatic retry (UPDATE_ROUNDS_MAX reached)
    bool     update_stuck;   // trips did not get the running firmware to what was wanted: left as it is
    uint32_t side_since_ms;  // draw has been on the current side of the charged floor since
    bool     low_side;       // ...and that side is "under the floor"
    bool     charged;
    uint32_t contract_mw;
    uint32_t denied_mw;      // contract that budget refused; unthrottle target
    uint32_t granted_ma;     // current ceiling currently programmed
    blade_status_t st;       // what the blade last reported
} port_ctx_t;

static port_ctx_t ctx[NUM_PORTS];
static bool hold_updates; // port_fsm_hold_updates

const char *port_state_name(port_state_t s) {
    switch (s) {
    case PORT_STATE_ABSENT:    return "absent";
    case PORT_STATE_PROBE:     return "probe";
    case PORT_STATE_IDLE:      return "idle";
    case PORT_STATE_ACTIVE:    return "active";
    case PORT_STATE_THROTTLED: return "throttled";
    case PORT_STATE_FAULT:     return "fault";
    case PORT_STATE_DISABLED:  return "disabled";
    case PORT_STATE_UPDATE:    return "updating";
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
    ctx[i].silent = false;
    memset(&ctx[i].st, 0, sizeof(ctx[i].st));
    if (next == PORT_STATE_ABSENT) {
        ctx[i].gen = BLADE_GEN_NONE;
        ctx[i].id_ok = false;
        ctx[i].update_rounds = 0; // whatever comes next is a different blade
        ctx[i].update_rewrite = false;
        ctx[i].update_stuck = false;
        ctx[i].held = false;
    }
    enter(i, next);
}

// The caller has the port's mux channel selected (or found that it could
// not, in which case the blade is beyond reach anyway).
static void fault(uint8_t i, uint32_t now_ms, uint16_t fault_bits, uint32_t detail) {
    ctx[i].fault_bits = fault_bits ? fault_bits : ctx[i].fault_bits;
    emit(EVT_FAULT, i, fault_bits, detail);
    power_down(i, PORT_STATE_FAULT);
    blade_faulted(ctx[i].gen);
    ctx[i].cooldown_until_ms = now_ms + FAULT_COOLDOWN_MS;
}

static void start_probe(uint8_t i) {
    ctx[i].probe_attempts = 0;
    ctx[i].silent_polls = 0;
    ctx[i].silent = false;
    ctx[i].fault_bits = 0;
    ctx[i].gen = BLADE_GEN_NONE;
    ctx[i].id_ok = false;
    ctx[i].enable_after_ms = 0; // the boot slot is spent; later seatings are immediate
    ctx[i].en_on_ms = 0;
    ctx[i].denied_mw = 0; // stale asks must not inflate a new throttle epoch
    ctx[i].step_after_ms = 0;
    enter(i, PORT_STATE_PROBE); // ctx.warm stands until the probe concludes
}

static void probe_failed(uint8_t i, uint32_t now_ms, uint16_t fail) {
    if ((int32_t)(now_ms - ctx[i].switch_until_ms) < 0) return; // the blade is between modes
    if (++ctx[i].probe_attempts < PROBE_MAX_ATTEMPTS) return;
    emit(EVT_PROBE_FAIL, i, fail, ctx[i].probe_attempts);
    ctx[i].warm = false;
    fault(i, now_ms, 0, fail);
}

// The blade is in its ROM bootloader: a trip through it (blade_update.h)
// unless it has had its share of them. It leaves on the bundled firmware
// when the chassis keeps its blades on that (the setting, and a controller
// image that has committed); otherwise whatever good image it holds is
// started as it is, and only a blade without one is written.
static void begin_update(uint8_t i, uint32_t now_ms) {
    ctx[i].warm = false;
    ctx[i].silent = false;
    tca9539_set_en(i, false); // dark anyway; the firmware's start brings it back up the normal way
    if (ctx[i].update_rounds >= UPDATE_ROUNDS_MAX) {
        emit(EVT_PROBE_FAIL, i, PROBE_FAIL_UPDATE, UPDATE_FAIL_LOOP);
        ctx[i].held = true;
        fault(i, now_ms, 0, PROBE_FAIL_UPDATE);
        return;
    }
    ctx[i].update_rounds++;
    blade_update_begin(i, ctx[i].update_rewrite ? BLADE_UPDATE_REWRITE
                          : g_settings.blade_auto_update && !hold_updates ? BLADE_UPDATE_MATCH
                          : BLADE_UPDATE_KEEP);
    ctx[i].update_rewrite = false;
    enter(i, PORT_STATE_UPDATE);
}

static void read_identity(uint8_t i) {
    ctx[i].id_ok = blade_identity(ctx[i].gen, &ctx[i].id);
}

#define WANT_REWRITE  1u // the bundle written over what it runs
#define WANT_BOOT_OPT 2u // its option bytes set to boot through the bootloader

// What the controller would change about a running gen-3 blade's firmware,
// from what the blade said of itself at the probe: the bundled version,
// when the setting says the chassis keeps its blades on it or a rewrite was
// asked for, and the boot option, when that setting says so. Either costs
// the port a trip through the bootloader, so the caller picks the moment.
// Nothing is wanted on the controller's own account while its image is on
// trial (port_fsm_hold_updates), nor after the trips failed to deliver it.
static unsigned blade_wants(uint8_t i) {
    const port_ctx_t *p = &ctx[i];
    if (p->gen != BLADE_GEN_3 || !p->id_ok) return 0;
    if (p->id.proto < 2) return 0; // before the commands existed: what it runs is what it is
    unsigned w = p->update_rewrite ? WANT_REWRITE : 0;
    if (hold_updates || p->update_stuck) return w;
    const blade_image_header_t *want = blade_bundle_header();
    if (g_settings.blade_auto_update && want &&
        (p->id.major != want->major || p->id.minor != want->minor || p->id.patch != want->patch))
        w |= WANT_REWRITE;
    if (g_settings.blade_boot_via_loader && !(p->id.boot & BLADE_BOOT_VIA_LOADER)) w |= WANT_BOOT_OPT;
    return w;
}

// Send a running blade to its bootloader for what blade_wants() found. The
// port goes dark with it and back to PROBE, which finds the bootloader next
// and makes the trip. When the boot option is wanted it is what is asked
// for, a rewrite with it or not: programming it ends in the bootloader all
// the same, and an image written under that option survives any reset on
// the way (the bootloader clears the empty-flash flag the plain reset
// relies on: AN2606, 48.3.1). False when nothing was sent and the port
// carries on as it was.
static bool send_to_loader(uint8_t i, uint32_t now_ms, unsigned wants) {
    port_ctx_t *p = &ctx[i];
    if (p->update_rounds >= UPDATE_ROUNDS_MAX) {
        // It runs, and the trips have not made it what was wanted. A port
        // that works is not given up for that: say so and leave it be.
        emit(EVT_PROBE_FAIL, i, PROBE_FAIL_UPDATE, UPDATE_FAIL_STUCK);
        p->update_stuck = true;
        p->update_rewrite = false;
        p->update_rounds = 0;
        return false;
    }
    bool boot_opt = (wants & WANT_BOOT_OPT) != 0;
    if (!blade_request_loader(p->gen, boot_opt)) return false;
    if (boot_opt) {
        p->update_rounds++; // counts like a trip: an option that will not take must not loop
        emit(EVT_UPDATE, i, UPDATE_BOOT_OPT, UPDATE_VERSION(p->id.major, p->id.minor, p->id.patch));
    }
    budget_release(i);
    p->contract_mw = 0;
    memset(&p->st, 0, sizeof(p->st));
    start_probe(i);
    p->switch_until_ms = now_ms + LOADER_SWITCH_MS;
    return true;
}

// The port has a working blade and nothing plugged in.
static void go_idle(uint8_t i, uint32_t now_ms) {
    ctx[i].idle_since_ms = now_ms;
    enter(i, PORT_STATE_IDLE);
}

// A blade found powered at a warm start that will not answer stays powered.
// It ran unsupervised while the controller was away and whatever it is
// charging is still charging; switching it off would be the controller's
// restart costing a port its power. It is reported once, shown as silent,
// and tried again every SILENT_RECHECK_MS until it answers, its bootloader
// does, or someone switches the port off.
static void warm_probe_failed(uint8_t i, uint32_t now_ms, uint16_t fail) {
    port_ctx_t *p = &ctx[i];
    if ((int32_t)(now_ms - p->switch_until_ms) < 0) return; // the blade is between modes
    if (p->silent || ++p->probe_attempts < PROBE_MAX_ATTEMPTS) return;
    emit(EVT_PROBE_FAIL, i, fail, p->probe_attempts);
    p->silent = true;
    budget_force_reserve(i, BUDGET_BASE_RESERVE_MW); // what it draws is not known; it is not nothing
}

// A blade found powered at a warm start: bring it under supervision without
// touching EN. A fault latched while nobody was watching counts as a fault
// now (the usual path: EN off, cooldown, re-probe). The blade's
// configuration is checked against the settings and rewritten — and
// re-advertised to an attached sink — only when it differs, so a live
// contract normally rides through untouched. Its firmware is left alone
// here whatever the controller would change about it: that waits for the
// port to be idle (blade_wants). A blade that will not answer stays
// powered too (warm_probe_failed).
static void warm_probe(uint8_t i, uint32_t now_ms) {
    if (ctx[i].silent) {
        if ((int32_t)(now_ms - ctx[i].silent_check_ms) < 0) return;
        ctx[i].silent_check_ms = now_ms + SILENT_RECHECK_MS;
    }
    uint16_t fail = 0;
    if (!tca9548a_select(i)) {
        fail = PROBE_FAIL_MUX;
    } else {
        if (ctx[i].gen == BLADE_GEN_NONE || ctx[i].silent) ctx[i].gen = blade_detect();
        if (ctx[i].gen == BLADE_GEN_LOADER) { begin_update(i, now_ms); return; }
        if (ctx[i].gen == BLADE_GEN_3) {
            read_identity(i);
            if (ctx[i].id_ok && !blade_wants(i)) ctx[i].update_rounds = 0; // running what it should
        }
        fail = blade_adopt(ctx[i].gen, g_settings.port_limit_ma[i], g_settings.port_max_mv[i],
                           &ctx[i].st);
        if (!fail && ctx[i].st.fault_bits) {
            ctx[i].warm = false;
            ctx[i].silent = false;
            fault(i, now_ms, ctx[i].st.fault_bits, ctx[i].st.fault_detail);
            return;
        }
    }

    if (fail) {
        warm_probe_failed(i, now_ms, fail);
        return;
    }
    ctx[i].warm = false;
    ctx[i].silent = false;
    ctx[i].granted_ma = g_settings.port_limit_ma[i];
    budget_force_reserve(i, BUDGET_BASE_RESERVE_MW);
    go_idle(i, now_ms); // an attached sink moves it on to ACTIVE next tick
}

// One probe attempt per tick keeps the loop cadence flat.
static void do_probe(uint8_t i, uint32_t now_ms) {
    if (ctx[i].switch_grace) {
        ctx[i].switch_grace = false;
        ctx[i].switch_until_ms = now_ms + LOADER_SWITCH_MS;
    }
    if (ctx[i].warm) {
        warm_probe(i, now_ms);
        return;
    }
    // The MPQ4242's I2C interface is off while its EN pin is low (datasheet,
    // PWR_CTL1: "when the external EN pin is low, the converter is off and the
    // I2C shuts down"), and on the V2 blade the dark part drags the segment it
    // shares with the INA226, so nothing on a gen-2 blade answers until it is
    // powered. Power it first, give the LDO and digital core a moment, then
    // find out what answers and configure it. A sink attached during that
    // window negotiates the part's OTP defaults, so the table is
    // re-advertised once ours is written — the same path a warm start takes.
    // A gen-3 blade answers with EN low or high and keeps its port dark
    // until configured, so the same order serves it — and since the port is
    // dark at this point whichever way it got here, this is also where a
    // blade is sent to its bootloader at no cost to anything plugged into
    // it (blade_wants). A failed probe still ends with EN off (power_down
    // via fault).
    if (!ctx[i].en_on_ms) {
        if (!tca9539_set_en(i, true)) {
            probe_failed(i, now_ms, PROBE_FAIL_EN);
            return;
        }
        ctx[i].en_on_ms = now_ms ? now_ms : 1;
        if (BLADE_WAKE_MS) return;
    }
    if (now_ms - ctx[i].en_on_ms < BLADE_WAKE_MS) return;

    uint16_t fail = 0;
    if (!tca9548a_select(i)) {
        fail = PROBE_FAIL_MUX;
    } else {
        ctx[i].gen = blade_detect();
        if (ctx[i].gen == BLADE_GEN_LOADER) { begin_update(i, now_ms); return; }
        if (ctx[i].gen == BLADE_GEN_3) {
            read_identity(i);
            unsigned wants = blade_wants(i);
            if (wants && send_to_loader(i, now_ms, wants)) return;
            if (ctx[i].id_ok && !wants) ctx[i].update_rounds = 0; // running what it should: the count starts over
        }
        fail = blade_setup(ctx[i].gen, g_settings.port_limit_ma[i], g_settings.port_max_mv[i]);
    }

    if (!fail) {
        ctx[i].granted_ma = g_settings.port_limit_ma[i];
        budget_force_reserve(i, BUDGET_BASE_RESERVE_MW);
        go_idle(i, now_ms);
        return;
    }
    probe_failed(i, now_ms, fail);
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
    uint32_t mv = ctx[i].st.bus_mv ? ctx[i].st.bus_mv : 5000;
    uint32_t ma = (grant_mw * 1000) / mv;
    if (ma < 500) ma = 500;

    blade_set_limits(ctx[i].gen, ma, g_settings.port_max_mv[i], true);
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
            if (ctx[j].silent) continue; // cannot be told anything
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
    blade_set_limits(ctx[i].gen, g_settings.port_limit_ma[i], g_settings.port_max_mv[i], true);
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
        bool low = p->st.power_mw < floor;
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

// A powered blade that stops answering is left as it is: EN stays, the
// budget keeps what the port last held, and nothing is decided from
// readings that have gone stale. Silence is not a reason to take a port
// down — the blade enforces its own limits, and whatever it is charging
// keeps charging. It is reported once, shown as silent, and asked again
// every SILENT_RECHECK_MS (a bus that is timing out should not be leaned
// on every tick). A gen-3 blade is also looked for in its bootloader,
// which is where a reset of its MCU (its watchdog, a brown-out) leaves it
// when it boots that way: that port is dark already, and the trip starts
// its firmware again.
static void blade_silent(uint8_t i, uint32_t now_ms) {
    port_ctx_t *p = &ctx[i];
    if (!p->silent && ++p->silent_polls < SILENT_POLLS_MAX) return; // transient; retry next tick
    if (p->gen == BLADE_GEN_3 && blade_detect() == BLADE_GEN_LOADER) {
        budget_release(i);
        p->contract_mw = 0;
        memset(&p->st, 0, sizeof(p->st));
        begin_update(i, now_ms);
        return;
    }
    if (!p->silent) {
        p->silent = true;
        emit(EVT_PROBE_FAIL, i, PROBE_FAIL_SILENT, 0);
    }
    p->silent_check_ms = now_ms + SILENT_RECHECK_MS;
}

// Shared telemetry + fault polling for powered states. Returns false if the
// port just faulted, was depowered, went back to PROBE, or is silent (the
// state stands, on nothing newer than its last answer).
static bool poll_powered(uint8_t i, bool present, uint32_t now_ms) {
    if (!present) {
        power_down(i, PORT_STATE_ABSENT);
        return false;
    }
    if (ctx[i].silent && (int32_t)(now_ms - ctx[i].silent_check_ms) < 0) return false;
    if (!tca9548a_select(i)) return !ctx[i].silent; // transient; retry next tick

    // A blade that answers again after a silence is taken back the way a
    // warm start takes one: its limits checked against what the port
    // should have (anything sent meanwhile was lost), rewritten only if
    // they differ.
    bool answered = ctx[i].silent
        ? !blade_adopt(ctx[i].gen, ctx[i].granted_ma, g_settings.port_max_mv[i], &ctx[i].st)
        : blade_poll(ctx[i].gen, false, &ctx[i].st);
    if (!answered) {
        port_state_t was = ctx[i].state;
        blade_silent(i, now_ms);
        return ctx[i].state == was && !ctx[i].silent;
    }
    ctx[i].silent_polls = 0;
    ctx[i].silent = false;

    if (ctx[i].st.fault_bits) {
        fault(i, now_ms, ctx[i].st.fault_bits, ctx[i].st.fault_detail);
        return false;
    }
    // A gen-3 blade that restarted (a watchdog, a brown-out on the slot's
    // 5 V) comes back with its port off and its configuration gone. Its
    // contract went with it; the port goes back through the probe, which
    // configures it again without an EN cut.
    if (!ctx[i].st.configured) {
        budget_force_reserve(i, BUDGET_BASE_RESERVE_MW);
        ctx[i].contract_mw = 0;
        start_probe(i);
        return false;
    }
    return true;
}

static void track_contract(uint8_t i) {
    uint32_t want = ctx[i].st.contract_mw;
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
    emit(EVT_CONTRACT, i, ctx[i].st.selected_pdo, want);
}

// Administrative state at power-up, per the port's boot policy (settings)
static bool boot_enabled(uint8_t i) {
    switch (g_settings.port_boot[i]) {
    case PORT_BOOT_OFF:  return false;
    case PORT_BOOT_LAST: return !(g_settings.port_off_mask & (1u << i));
    default:             return true;
    }
}

void port_fsm_hold_updates(bool hold) {
    hold_updates = hold;
}

void port_fsm_init(void) {
    memset(ctx, 0, sizeof(ctx));
    hold_updates = false;
    for (uint8_t i = 0; i < NUM_PORTS; i++) {
        ctx[i].state = PORT_STATE_ABSENT;
        ctx[i].admin_enabled = boot_enabled(i);
    }
}

uint8_t port_fsm_boot_inventory(const bool *present, uint8_t powered, uint32_t now_ms) {
    uint8_t adopted = 0;
    // Blades found powered (warm start) keep their EN where the policy
    // allows: adopted, they go through warm_probe instead of a power-up.
    for (uint8_t i = 0; i < NUM_PORTS; i++) {
        ctx[i].enable_after_ms = 0;
        ctx[i].warm = false;
        if (!(powered & (1u << i))) continue;
        if (!present[i]) {
            tca9539_set_en(i, false); // EN on with no blade seated behind it: stale
        } else if (!ctx[i].admin_enabled) {
            power_down(i, PORT_STATE_DISABLED); // boot policy says off
        } else {
            ctx[i].warm = true;
            adopted |= (uint8_t)(1u << i);
        }
    }
    // Adopted blades first, WARM_STAGGER_MS apart; then the rest come up one
    // at a time, BOOT_STAGGER_MS apart. Both queues rank by priority, slot
    // order among equals.
    unsigned n_warm = 0;
    for (uint8_t i = 0; i < NUM_PORTS; i++) n_warm += ctx[i].warm ? 1 : 0;
    for (uint8_t i = 0; i < NUM_PORTS; i++) {
        if (!present[i] || !ctx[i].admin_enabled) continue;
        unsigned rank = 0;
        for (uint8_t j = 0; j < NUM_PORTS; j++) {
            if (j == i || !present[j] || !ctx[j].admin_enabled || ctx[j].warm != ctx[i].warm) continue;
            if (prio(j) < prio(i) || (prio(j) == prio(i) && j < i)) rank++;
        }
        ctx[i].enable_after_ms = ctx[i].warm
            ? now_ms + rank * WARM_STAGGER_MS
            : now_ms + n_warm * WARM_STAGGER_MS + rank * BOOT_STAGGER_MS;
    }
    return adopted;
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
        if (p->st.attached) {
            attach_begin(i, now_ms);
            enter(i, PORT_STATE_ACTIVE);
            track_contract(i);
        } else if (now_ms - p->idle_since_ms >= UPDATE_IDLE_MS) {
            // nothing plugged in for a while: the moment for whatever the
            // blade's firmware was waiting for
            unsigned wants = blade_wants(i);
            if (wants) send_to_loader(i, now_ms, wants);
        }
        break;

    case PORT_STATE_ACTIVE:
    case PORT_STATE_THROTTLED:
        if (!poll_powered(i, present, now_ms)) break;
        if (!p->st.attached) {
            budget_force_reserve(i, BUDGET_BASE_RESERVE_MW);
            p->contract_mw = 0;
            p->denied_mw = 0;
            if (p->granted_ma != g_settings.port_limit_ma[i]) {
                blade_set_limits(p->gen, g_settings.port_limit_ma[i], g_settings.port_max_mv[i], false);
                p->granted_ma = g_settings.port_limit_ma[i];
            }
            go_idle(i, now_ms);
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
        if (!present) { power_down(i, PORT_STATE_ABSENT); break; }
        if (p->held) break; // until reseated, re-enabled or told to update
        if (p->admin_enabled && now_ms >= p->cooldown_until_ms) start_probe(i);
        break;

    case PORT_STATE_UPDATE:
        if (!present) { power_down(i, PORT_STATE_ABSENT); break; }
        if (!tca9548a_select(i)) break; // transient; the bootloader waits
        switch (blade_update_step(i)) {
        case BLADE_UPDATE_BUSY:
            break;
        case BLADE_UPDATE_STARTED:
            emit(EVT_UPDATE, i, blade_update_wrote(i) ? UPDATE_WRITTEN : UPDATE_STARTED,
                 blade_update_version(i));
            start_probe(i); // the firmware answers at its own address once up
            p->switch_grace = true;
            break;
        case BLADE_UPDATE_FAILED:
            emit(EVT_PROBE_FAIL, i, PROBE_FAIL_UPDATE, blade_update_fail(i));
            fault(i, now_ms, 0, PROBE_FAIL_UPDATE);
            break;
        }
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
    out->gen = blade_gen_number(p->gen);
    out->update_pct = p->state == PORT_STATE_UPDATE ? blade_update_pct(i) : 0;
    out->update_due = (p->state == PORT_STATE_IDLE || p->state == PORT_STATE_ACTIVE ||
                       p->state == PORT_STATE_THROTTLED) && blade_wants(i) != 0;
    out->silent = p->silent;
    out->attached = p->st.attached;
    out->charged = p->st.attached && p->charged;
    out->selected_pdo = p->st.selected_pdo;
    out->fault_bits = p->fault_bits;
    out->bus_mv = p->st.bus_mv;
    out->current_ma = p->st.current_ma;
    out->power_mw = p->st.power_mw;
    out->contract_mw = budget_port_reservation(i);
    out->temp_conv_dc = p->st.has_temps ? p->st.temp_conv_dc : PORT_TEMP_NONE;
    out->temp_plug_dc = p->st.has_temps ? p->st.temp_plug_dc : PORT_TEMP_NONE;
    out->temp_mcu_dc = p->st.has_temps ? p->st.temp_mcu_dc : PORT_TEMP_NONE;
}

void port_fsm_cmd(uint8_t i, const engine_cmd_t *cmd) {
    bool powered = ctx[i].state == PORT_STATE_IDLE ||
                   ctx[i].state == PORT_STATE_ACTIVE ||
                   ctx[i].state == PORT_STATE_THROTTLED;

    switch ((cmd_op_t)cmd->op) {
    case CMD_PORT_ENABLE:
        ctx[i].admin_enabled = true;
        ctx[i].update_stuck = false;
        if (ctx[i].held) { // a held fault gets another go
            ctx[i].held = false;
            ctx[i].update_rounds = 0;
            ctx[i].cooldown_until_ms = 0;
        }
        break;
    case CMD_PORT_UPDATE:
        // The bundle goes onto the blade whatever it runs, and now,
        // whatever is plugged into it: a running firmware is sent to its
        // bootloader, one already there (or a held port) gets the trip on
        // its next probe. A blade that is not answering cannot be told: its
        // EN is taken low instead, its watch takes it to the bootloader
        // from there (blade_regs.h WATCH_S), and the probe that follows
        // each cooldown finds it when it has.
        if (!blade_bundle_header() || ctx[i].gen == BLADE_GEN_2) break;
        ctx[i].update_rewrite = true;
        ctx[i].update_rounds = 0;
        ctx[i].update_stuck = false;
        ctx[i].held = false;
        if (ctx[i].silent) {
            ctx[i].warm = false;
            power_down(i, PORT_STATE_FAULT);
            ctx[i].cooldown_until_ms = 0;
        } else if (powered && tca9548a_select(i) && blade_request_loader(ctx[i].gen, false)) {
            budget_release(i);
            ctx[i].contract_mw = 0;
            memset(&ctx[i].st, 0, sizeof(ctx[i].st));
            start_probe(i);
            ctx[i].switch_grace = true;
        } else if (ctx[i].state == PORT_STATE_FAULT) {
            ctx[i].cooldown_until_ms = 0;
        }
        break;
    case CMD_PORT_DISABLE:
        ctx[i].admin_enabled = false;
        if (ctx[i].state != PORT_STATE_ABSENT) power_down(i, PORT_STATE_DISABLED);
        break;
    case CMD_PORT_HARD_RESET:
        if (powered && tca9548a_select(i)) blade_hard_reset(ctx[i].gen);
        break;
    case CMD_PORT_SRC_CAP:
        if (powered && tca9548a_select(i)) blade_send_src_cap(ctx[i].gen);
        break;
    case CMD_PORT_LIMIT: {
        // core 0 stored the new setting first; a port that is not powered
        // picks it up when it next probes. The INA226 trip stays put.
        uint32_t limit = cmd->arg;
        if (!powered || !tca9548a_select(i)) break;
        if (ctx[i].state == PORT_STATE_THROTTLED && ctx[i].granted_ma <= limit)
            break; // the budget clamp is tighter; recovery restores to the new limit
        // renegotiated now when a sink is on
        blade_set_limits(ctx[i].gen, limit, g_settings.port_max_mv[i], ctx[i].st.attached);
        ctx[i].granted_ma = limit;
        break;
    }
    case CMD_PORT_VOLT:
        // same shape: stored by core 0, re-advertised now if the port is
        // powered; the current ceiling in force (a budget clamp included) stays
        if (!powered || !tca9548a_select(i)) break;
        blade_set_limits(ctx[i].gen, ctx[i].granted_ma, cmd->arg, ctx[i].st.attached);
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

        if (blade_poll(ctx[i].gen, true, &ctx[i].st) && ctx[i].st.fault_bits)
            fault(i, now_ms, ctx[i].st.fault_bits, ctx[i].st.fault_detail);
    }
}
