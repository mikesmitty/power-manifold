#include "sim_blades.h"

#include <string.h>

#include "ina226.h"
#include "mpq4242.h"
#include "pins.h"
#include "tca9539.h"
#include "tca9548a.h"

// One simulated blade per mux channel. The MPQ4242 model negotiates the way
// the real part does from the engine's point of view: the sink's request is
// granted up to the advertised current ceiling, and a contract only moves
// when the advertisement is re-sent (send_src_cap) or the sink re-attaches.

typedef struct {
    bool     present;
    bool     en;
    bool     ina_ok;       // probe responds
    bool     mpq_ok;
    bool     attached;     // sink plugged into the port
    uint16_t req_mv;       // sink's ask
    uint32_t req_ma;
    uint32_t adv_ma;       // advertised (programmed) current ceiling
    uint16_t con_mv;       // live contract
    uint32_t con_ma;
    uint8_t  fault_bits;   // sticky MPQ faults until cleared by the script
    bool     ocp_latch;    // latched INA226 alert; clears on read (LEN)
    uint32_t ina_alert_ma;
    uint8_t  load_pct;     // measured draw as % of contract current
    uint32_t src_caps;
    uint32_t hard_resets;
} sim_slot_t;

static sim_slot_t slots[NUM_PORTS];
static int8_t selected = -1;
static bool mux_fail, exp_fail;
static bool fan;
static uint32_t mux_resets;

static sim_slot_t *sel(void) {
    if (selected < 0 || selected >= NUM_PORTS) return NULL; // spare channel
    return &slots[selected];
}

// Contract follows the advertisement: applied on attach and on src_cap.
static void renegotiate(sim_slot_t *s) {
    if (!s->attached || !s->en) {
        s->con_mv = 0;
        s->con_ma = 0;
        return;
    }
    s->con_mv = s->req_mv;
    s->con_ma = s->req_ma < s->adv_ma ? s->req_ma : s->adv_ma;
}

static uint32_t status3_mw(const sim_slot_t *s) {
    uint32_t mw = ((uint32_t)s->con_mv * s->con_ma) / 1000;
    mw = (mw / 500) * 500;               // STATUS3 LSB is 0.5 W
    return mw > 127500 ? 127500 : mw;    // 8-bit register ceiling
}

// ---- script-facing controls ------------------------------------------------

void sim_reset(void) {
    memset(slots, 0, sizeof(slots));
    for (int i = 0; i < NUM_PORTS; i++) {
        slots[i].ina_ok = true;
        slots[i].mpq_ok = true;
        slots[i].load_pct = 80;
    }
    selected = -1;
    mux_fail = exp_fail = false;
    fan = false;
    mux_resets = 0;
}

void sim_set_present(uint8_t slot, bool present) {
    slots[slot].present = present;
    if (!present) {
        slots[slot].en = false;
        renegotiate(&slots[slot]);
    }
}

void sim_attach(uint8_t slot, uint16_t req_mv, uint32_t req_ma) {
    sim_slot_t *s = &slots[slot];
    s->attached = true;
    s->req_mv = req_mv;
    s->req_ma = req_ma;
    renegotiate(s);
}

void sim_detach(uint8_t slot) {
    slots[slot].attached = false;
    renegotiate(&slots[slot]);
}

void sim_set_mpq_fault(uint8_t slot, uint8_t fault_bits) {
    slots[slot].fault_bits = fault_bits;
}

void sim_trip_ocp(uint8_t slot) {
    slots[slot].ocp_latch = true;
}

bool sim_alert_asserted(void) {
    for (int i = 0; i < NUM_PORTS; i++) {
        const sim_slot_t *s = &slots[i];
        if (s->present && s->en && (s->ocp_latch || s->fault_bits)) return true;
    }
    return false;
}

void sim_set_probe_ok(uint8_t slot, bool ina_ok, bool mpq_ok) {
    slots[slot].ina_ok = ina_ok;
    slots[slot].mpq_ok = mpq_ok;
}

void sim_set_mux_fail(bool fail) { mux_fail = fail; }
void sim_set_expander_fail(bool fail) { exp_fail = fail; }
void sim_set_load_pct(uint8_t slot, uint8_t pct) { slots[slot].load_pct = pct; }

bool sim_en(uint8_t slot) { return slots[slot].en; }
bool sim_fan(void) { return fan; }
uint32_t sim_advertised_ma(uint8_t slot) { return slots[slot].adv_ma; }
uint32_t sim_contract_mw(uint8_t slot) { return status3_mw(&slots[slot]); }
uint32_t sim_ina_alert_ma(uint8_t slot) { return slots[slot].ina_alert_ma; }
uint32_t sim_src_cap_count(uint8_t slot) { return slots[slot].src_caps; }
uint32_t sim_hard_reset_count(uint8_t slot) { return slots[slot].hard_resets; }
uint32_t sim_mux_reset_count(void) { return mux_resets; }

// ---- tca9548a --------------------------------------------------------------

void tca9548a_init(void) {
    tca9548a_hw_reset();
}

bool tca9548a_select(uint8_t channel) {
    if (mux_fail) {
        selected = -1;
        return false;
    }
    selected = (int8_t)channel;
    return true;
}

bool tca9548a_deselect_all(void) {
    if (mux_fail) return false;
    selected = -1;
    return true;
}

void tca9548a_hw_reset(void) {
    // a hardware reset clears whatever was hanging the bus
    mux_resets++;
    selected = -1;
    mux_fail = false;
    exp_fail = false;
}

// ---- tca9539 ---------------------------------------------------------------

bool tca9539_init(void) {
    if (exp_fail) return false;
    // mirrors the real init: outputs all low first — every EN drops
    for (int i = 0; i < NUM_PORTS; i++) {
        slots[i].en = false;
        renegotiate(&slots[i]);
    }
    fan = false;
    return true;
}

bool tca9539_set_en(uint8_t port, bool on) {
    if (exp_fail || port >= NUM_PORTS) return false;
    slots[port].en = on;
    renegotiate(&slots[port]);
    return true;
}

bool tca9539_set_fan(bool on) {
    if (exp_fail) return false;
    fan = on;
    return true;
}

bool tca9539_all_en_off(void) {
    if (exp_fail) return false;
    for (int i = 0; i < NUM_PORTS; i++) {
        slots[i].en = false;
        renegotiate(&slots[i]);
    }
    return true;
}

bool tca9539_read_inputs(uint16_t *inputs) {
    if (exp_fail) return false;
    uint16_t word = 0;
    for (int i = 0; i < NUM_PORTS; i++) {
        if (slots[i].en) word |= 1u << TCA9539_EN_BIT(i);
        if (!slots[i].present) word |= 1u << TCA9539_PRES_BIT(i); // active low
    }
    *inputs = word;
    return true;
}

// ---- ina226 ----------------------------------------------------------------

bool ina226_probe(void) {
    const sim_slot_t *s = sel();
    return s && s->present && s->ina_ok;
}

bool ina226_configure(void) {
    const sim_slot_t *s = sel();
    return s && s->present && s->ina_ok;
}

bool ina226_read(ina226_reading_t *r) {
    const sim_slot_t *s = sel();
    if (!s || !s->present || !s->ina_ok) return false;
    if (!s->en) {
        r->bus_mv = 0;
        r->current_ma = 0;
        r->power_mw = 0;
    } else if (!s->attached) {
        r->bus_mv = 5000; // vSafe5V, nothing drawing
        r->current_ma = 0;
        r->power_mw = 0;
    } else {
        r->bus_mv = s->con_mv;
        r->current_ma = (int32_t)((s->con_ma * s->load_pct) / 100);
        r->power_mw = ((uint32_t)r->bus_mv * (uint32_t)r->current_ma) / 1000;
    }
    return true;
}

bool ina226_set_alert_ma(uint32_t ma) {
    sim_slot_t *s = sel();
    if (!s || !s->present || !s->ina_ok) return false;
    s->ina_alert_ma = ma;
    return true;
}

bool ina226_alert_tripped(bool *tripped) {
    sim_slot_t *s = sel();
    if (!s || !s->present || !s->ina_ok) return false;
    *tripped = s->ocp_latch;
    s->ocp_latch = false; // reading Mask/Enable clears the latch
    return true;
}

// ---- mpq4242 ---------------------------------------------------------------

static sim_slot_t *mpq(void) {
    sim_slot_t *s = sel();
    return (s && s->present && s->mpq_ok) ? s : NULL;
}

bool mpq4242_probe(void) { return mpq() != NULL; }
bool mpq4242_unlock(void) { return mpq() != NULL; }

bool mpq4242_configure(uint32_t max_ma) {
    sim_slot_t *s = mpq();
    if (!s) return false;
    s->adv_ma = max_ma;
    return true;
}

bool mpq4242_read_status(mpq4242_status_t *st) {
    const sim_slot_t *s = mpq();
    if (!s) return false;
    st->attached = s->attached;
    st->fault_bits = s->fault_bits;
    st->contract_mw = status3_mw(s);
    if (!s->attached || s->con_mv == 0) st->selected_pdo = 0;
    else if (s->con_mv <= 5000) st->selected_pdo = 1;
    else if (s->con_mv <= 9000) st->selected_pdo = 2;
    else if (s->con_mv <= 12000) st->selected_pdo = 3;
    else if (s->con_mv <= 15000) st->selected_pdo = 4;
    else st->selected_pdo = 5;
    return true;
}

bool mpq4242_set_max_current_ma(uint32_t ma) {
    sim_slot_t *s = mpq();
    if (!s) return false;
    s->adv_ma = ma; // takes effect at the next src_cap, like the real part
    return true;
}

bool mpq4242_set_pdo_fixed(uint8_t pdo, uint16_t mv, uint32_t ma, bool enabled) {
    (void)pdo; (void)mv; (void)ma; (void)enabled;
    return mpq() != NULL;
}

bool mpq4242_set_pdo_enabled(uint8_t pdo, bool enabled) {
    (void)pdo; (void)enabled;
    return mpq() != NULL;
}

bool mpq4242_send_src_cap(void) {
    sim_slot_t *s = mpq();
    if (!s) return false;
    s->src_caps++;
    renegotiate(s);
    return true;
}

bool mpq4242_send_hard_reset(void) {
    sim_slot_t *s = mpq();
    if (!s) return false;
    s->hard_resets++;
    return true;
}
