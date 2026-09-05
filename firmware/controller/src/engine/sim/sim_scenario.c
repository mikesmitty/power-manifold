#include "sim_scenario.h"

#include <stddef.h>

#include "manifold.h"
#include "sim_blades.h"

// A 60-second loop against the default settings (360 W budget, 5 A port
// limit, priority = port index). Slots are 0-based; ports 1-6 to the user.
//
//   t=0   ports 1-4 and 6 seated, port 5 empty; all sinks unplugged
//   t=4s  port 6 (lowest priority) takes a 100 W laptop     -> active
//   t=8s  port 4 takes 100 W                                -> active
//   t=12s port 3 takes 100 W                                -> active
//   t=16s port 1 (highest priority) takes 100 W: the budget is short, so
//         port 6 is shed to ~45 W                           -> throttled
//   t=24s a blade is seated in port 5                       -> probe, idle
//   t=28s port 5 takes a 20 W phone: port 6 is shed again for the last 5 W
//   t=34s port 3's sink unplugs: freed budget restores port 6 to 100 W
//   t=42s port 4 trips over-current                         -> fault,
//         5 s cooldown, automatic re-probe and renegotiation
//   t=50s port 5's blade is pulled                          -> absent
//   t=54s remaining sinks unplug                            -> idle
//   t=60s wrap

static void step_baseline(void) {
    for (uint8_t i = 0; i < NUM_PORTS; i++) {
        sim_detach(i);
        sim_set_mpq_fault(i, 0);
        sim_set_present(i, i != 4);
    }
}

static void step_attach_p6(void) { sim_attach(5, 20000, 5000); }
static void step_attach_p4(void) { sim_attach(3, 20000, 5000); }
static void step_attach_p3(void) { sim_attach(2, 20000, 5000); }
static void step_attach_p1(void) { sim_attach(0, 20000, 5000); }
static void step_seat_p5(void)   { sim_set_present(4, true); }
static void step_attach_p5(void) { sim_attach(4, 9000, 2220); }
static void step_detach_p3(void) { sim_detach(2); }
static void step_ocp_p4(void)    { sim_trip_ocp(3); }
static void step_pull_p5(void)   { sim_set_present(4, false); }

static void step_detach_rest(void) {
    sim_detach(0);
    sim_detach(3);
    sim_detach(5);
}

typedef struct {
    uint32_t at_ms;
    void (*fn)(void);
} step_t;

static const step_t STEPS[] = {
    {0,     step_baseline},
    {4000,  step_attach_p6},
    {8000,  step_attach_p4},
    {12000, step_attach_p3},
    {16000, step_attach_p1},
    {24000, step_seat_p5},
    {28000, step_attach_p5},
    {34000, step_detach_p3},
    {42000, step_ocp_p4},
    {50000, step_pull_p5},
    {54000, step_detach_rest},
};

#define N_STEPS (sizeof(STEPS) / sizeof(STEPS[0]))
#define CYCLE_MS 60000u

static uint32_t cycle_start;
static size_t next;
static bool running;
static bool paused;

void sim_scenario_set_running(bool run) {
    paused = !run;
    if (run) running = false; // restart from the baseline on the next tick
}

bool sim_scenario_running(void) {
    return !paused;
}

void sim_scenario_tick(uint32_t now_ms) {
    if (paused) return;
    if (!running) {
        running = true;
        cycle_start = now_ms;
        next = 0;
    }
    uint32_t phase = now_ms - cycle_start;
    while (next < N_STEPS && phase >= STEPS[next].at_ms) STEPS[next++].fn();
    if (phase >= CYCLE_MS) {
        cycle_start = now_ms;
        next = 0;
    }
}
