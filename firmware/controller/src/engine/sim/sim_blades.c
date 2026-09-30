#include "sim_blades.h"

#include <string.h>

#include "blade3.h"
#include "blade_image.h"
#include "ina226.h"
#include "mpq4242.h"
#include "pins.h"
#include "stboot.h"
#include "tca9539.h"
#include "tca9548a.h"

// A gen-3 blade's flash, as its ROM bootloader lets the controller read,
// erase and write it. The host tests hold the whole part; the fake-blade
// firmware build keeps the first page only (there is no bundled image to
// write there, and a Pico has no room for six parts).
#ifndef SIM_FLASH_SIZE
#define SIM_FLASH_SIZE BLADE_IMAGE_MAX
#endif

// One simulated blade per mux channel, of either generation. The gen-2
// MPQ4242 model negotiates the way the real part does from the engine's
// point of view: the sink's request is granted up to the advertised current
// ceiling, and a contract only moves when the advertisement is re-sent
// (send_src_cap) or the sink re-attaches. The gen-3 model is the blade
// firmware's register file: the port is dark until the controller has
// configured it, it re-advertises by itself when its limits change, and a
// fault takes the port down and stays latched until BLADE_CMD_CLEAR_FAULTS.

typedef struct {
    bool     present;
    uint8_t  gen;          // 2 or 3
    bool     en;
    bool     ina_ok;       // gen 2: probe responds
    bool     mpq_ok;
    bool     blade_ok;     // gen 3: the register file answers
    bool     configured;   // gen 3: written since the blade's last start
    bool     port_en;      // gen 3: BLADE_CTL_PORT_EN
    uint16_t cond;         // gen 3: fault conditions the script holds present (re-latch on clear)
    uint16_t faults;       // gen 3: BLADE_FAULT_* latched
    uint32_t clears;       // gen 3: BLADE_CMD_CLEAR_FAULTS received
    uint32_t cfg_writes;   // gen 3: configuration transfers received
    bool     attached;     // sink plugged into the port
    uint16_t req_mv;       // sink's ask
    uint32_t req_ma;
    uint32_t adv_ma;       // advertised (programmed) current ceiling
    uint16_t adv_mv;       // voltage cap: highest PDO offered
    uint16_t con_mv;       // live contract
    uint32_t con_ma;
    uint8_t  fault_bits;   // sticky MPQ faults until cleared by the script
    bool     ocp_latch;    // latched INA226 alert; clears on read (LEN)
    uint32_t ina_alert_ma;
    uint8_t  load_pct;     // measured draw as % of contract current
    bool     temp_set;     // gen 3: the script pinned the thermometers (else modelled from the load)
    int16_t  temp_conv_dc; // gen 3: pinned readings, 0.1 degC
    int16_t  temp_plug_dc;
    // gen 3: the MCU and its ROM bootloader
    uint8_t  watch_s;      // gen 3: BLADE_REG_WATCH_S as written
    bool     loader;       // in the bootloader (else running whatever the flash holds)
    bool     boot_via_loader; // option bytes: every reset lands in the bootloader
    bool     boot_opt_stuck;  // the boot-option command resets the blade without the option taking
    bool     locked_up;    // started with no image: a HardFault loop until power is cut
    uint8_t  busy_polls;   // BUSY answers left on the flash operation in progress
    uint8_t  crashes_left; // the firmware crashes this many more starts
    uint32_t goes;         // Go commands accepted
    uint32_t erases, writes; // erase passes (each starts at the first page) and write commands
    uint32_t cut_after_writes; // a power cut lands after this many more write commands (0 = none)
    uint32_t crc_len;      // the range of the checksum command in progress
    uint8_t  flash[SIM_FLASH_SIZE];
    uint32_t src_caps;
    uint32_t hard_resets;
    uint32_t en_changes;   // every time EN actually moved
} sim_slot_t;

static sim_slot_t slots[NUM_PORTS];
static int8_t selected = -1;
static bool mux_fail, exp_fail;
static bool fan;
static uint32_t mux_resets;
static bool exp_programmed; // tca9539_init ran since the last power cycle

static sim_slot_t *sel(void) {
    if (selected < 0 || selected >= NUM_PORTS) return NULL; // spare channel
    return &slots[selected];
}

// The sink takes its ask when the advertisement reaches it, else the highest
// fixed PDO still offered under the cap (PDO1, 5 V, is always there).
static uint16_t offered_mv(const sim_slot_t *s) {
    uint32_t lim = s->adv_mv >= PORT_VOLT_MAX_MV ? 21000 : s->adv_mv; // no cap: the 21 V PPS range too
    if (s->req_mv <= lim) return s->req_mv;
    static const uint16_t FIXED[] = {20000, 15000, 12000, 9000};
    for (size_t k = 0; k < sizeof(FIXED) / sizeof(FIXED[0]); k++)
        if (FIXED[k] <= lim) return FIXED[k];
    return 5000;
}

// A gen-3 port runs only when the blade has EN, a configuration with a
// current ceiling, and nothing alerting latched.
static bool armed3(const sim_slot_t *s) {
    return s->en && s->configured && s->port_en && s->adv_ma != 0 &&
           (s->faults & BLADE_FAULT_ALERTING) == 0;
}

// Contract follows the advertisement: applied on attach and on src_cap (a
// gen-3 blade re-advertises on its own whenever its table changes).
static void renegotiate(sim_slot_t *s) {
    bool up = s->gen == 3 ? armed3(s) : s->en;
    if (!s->attached || !up) {
        s->con_mv = 0;
        s->con_ma = 0;
        return;
    }
    s->con_mv = offered_mv(s);
    // Above 20 V the contract can only be the 21 V PPS range, whose current
    // field the driver cuts back to stay within PORT_POWER_MAX_MW
    uint32_t adv = s->con_mv > 20000 ? mpq4242_pdo_current_cap_ma(21000, s->adv_ma, 50) : s->adv_ma;
    s->con_ma = s->req_ma < adv ? s->req_ma : adv;
}

static void set_en(sim_slot_t *s, bool on) {
    if (s->en != on) s->en_changes++;
    s->en = on;
    renegotiate(s);
}

static uint32_t status3_mw(const sim_slot_t *s) {
    uint32_t mw = ((uint32_t)s->con_mv * s->con_ma) / 1000;
    mw = (mw / 500) * 500;               // STATUS3 LSB is 0.5 W
    return mw > 127500 ? 127500 : mw;    // 8-bit register ceiling
}

static uint32_t contract_mw(const sim_slot_t *s) {
    if (s->gen == 3) return ((uint32_t)s->con_mv * s->con_ma) / 1000; // exact: two 16-bit registers
    return status3_mw(s);
}

static uint8_t pdo_of(const sim_slot_t *s) {
    if (!s->attached || s->con_mv == 0) return 0;
    if (s->con_mv <= 5000) return 1;
    if (s->con_mv <= 9000) return 2;
    if (s->con_mv <= 12000) return 3;
    if (s->con_mv <= 15000) return 4;
    if (s->con_mv <= 20000) return 5;
    return 7; // the 21 V PPS range
}

// A gen-3 blade latches a fault, drops its port and holds ALERT# until the
// controller clears it; a condition the script keeps present latches again
// at once.
static void latch3(sim_slot_t *s, uint16_t bits) {
    s->faults |= bits;
    renegotiate(s);
}

// ---- script-facing controls ------------------------------------------------

void sim_reset(void) {
    memset(slots, 0, sizeof(slots));
    for (int i = 0; i < NUM_PORTS; i++) {
        slots[i].gen = 2;
        slots[i].ina_ok = true;
        slots[i].mpq_ok = true;
        slots[i].blade_ok = true;
        slots[i].load_pct = 80;
        slots[i].adv_mv = PORT_VOLT_MAX_MV;
        slots[i].faults = BLADE_FAULT_RESET; // a gen-3 blade starts with its restart latched
    }
    selected = -1;
    mux_fail = exp_fail = false;
    fan = false;
    mux_resets = 0;
    exp_programmed = false;
}

// ---- gen-3 flash and boot -------------------------------------------------

static const blade_image_header_t *flash_header(const sim_slot_t *s) {
    return (const blade_image_header_t *)(s->flash + BLADE_IMAGE_HEADER_OFFSET);
}

static bool flash_has_image(const sim_slot_t *s) {
    const blade_image_header_t *h = flash_header(s);
    return h->magic == BLADE_IMAGE_MAGIC && h->length <= SIM_FLASH_SIZE;
}

static bool flash_page0_empty(const sim_slot_t *s) {
    for (int i = 0; i < 8; i++)
        if (s->flash[i] != 0xFF) return false;
    return true;
}

// The MCU comes up (power-on, its own reset, a Go from the bootloader):
// configuration gone, port off, restart latched. Where it lands depends on
// the option bytes and whether the flash reads as empty (RM0444 2.5.4).
static void mcu_reset(sim_slot_t *s, bool from_go) {
    s->configured = false;
    s->port_en = false;
    s->adv_ma = 0;
    s->adv_mv = 0;
    s->faults = BLADE_FAULT_RESET;
    s->busy_polls = 0;
    s->loader = !from_go && (s->boot_via_loader || flash_page0_empty(s));
    s->locked_up = false;
    if (!s->loader) {
        if (!flash_has_image(s)) s->locked_up = true; // nothing to run: a fault loop nobody can reach
        else if (s->crashes_left) {
            s->crashes_left--;
            // the watchdog restarts it: into the bootloader when the option
            // bytes say so, else round again
            s->loader = s->boot_via_loader;
            if (!s->loader) s->locked_up = true; // a crash loop looks like nothing answering
        }
    }
    renegotiate(s);
}

// What a fresh gen-3 blade's flash holds: a good image of an old version
static void flash_default(sim_slot_t *s) {
    memset(s->flash, 0xFF, sizeof s->flash);
    blade_image_header_t h = {
        .magic = BLADE_IMAGE_MAGIC, .major = 0, .minor = 0, .patch = 1, .proto = BLADE_PROTO_VERSION,
        .length = 2 * BLADE_IMAGE_PAGE, .reserved = 0,
    };
    if (h.length > sizeof s->flash) h.length = sizeof s->flash;
    memset(s->flash, 0x11, h.length); // "code"
    memcpy(s->flash + BLADE_IMAGE_HEADER_OFFSET, &h, sizeof h);
}

void sim_set_present(uint8_t slot, bool present) {
    sim_slot_t *s = &slots[slot];
    s->present = present;
    if (!present) set_en(s, false);
    else if (s->gen == 3) mcu_reset(s, false); // seated: the MCU powers up from the slot's 5 V
}

void sim_set_gen(uint8_t slot, uint8_t gen) {
    slots[slot].gen = gen == 3 ? 3 : 2;
    if (gen == 3) flash_default(&slots[slot]);
}

// The blade's MCU restarts (its watchdog, a brown-out): the slot's EN is
// the backplane's and stays.
void sim_blade_restart(uint8_t slot) {
    mcu_reset(&slots[slot], false);
}

void sim_blade_power_cut(uint8_t slot) {
    mcu_reset(&slots[slot], false);
}

void sim_blade_erase(uint8_t slot) {
    memset(slots[slot].flash, 0xFF, sizeof slots[slot].flash);
    mcu_reset(&slots[slot], false);
}

void sim_blade_flash_image(uint8_t slot, const uint8_t *image, uint32_t len) {
    sim_slot_t *s = &slots[slot];
    memset(s->flash, 0xFF, sizeof s->flash);
    if (len > sizeof s->flash) len = sizeof s->flash;
    memcpy(s->flash, image, len);
    mcu_reset(s, false);
}

void sim_blade_set_boot_via_loader(uint8_t slot, bool set) { slots[slot].boot_via_loader = set; }
void sim_blade_set_boot_opt_stuck(uint8_t slot, bool stuck) { slots[slot].boot_opt_stuck = stuck; }

// The blade's watch runs out (blade_regs.h WATCH_S): with EN low and a
// watch set, its MCU resets into the bootloader, and whatever kept it from
// answering is gone with the firmware that was running.
bool sim_blade_watch_expire(uint8_t slot) {
    sim_slot_t *s = &slots[slot];
    if (s->gen != 3 || s->en || !s->watch_s || s->loader) return false;
    mcu_reset(s, false);
    s->loader = true;
    s->blade_ok = true;
    return true;
}
void sim_blade_crash_next(uint8_t slot, uint8_t times) { slots[slot].crashes_left = times; }
void sim_blade_cut_power_after_writes(uint8_t slot, uint32_t writes) { slots[slot].cut_after_writes = writes; }
bool sim_blade_in_loader(uint8_t slot) { return slots[slot].loader; }
bool sim_blade_boot_via_loader(uint8_t slot) { return slots[slot].boot_via_loader; }
bool sim_blade_locked_up(uint8_t slot) { return slots[slot].locked_up; }
uint32_t sim_blade_go_count(uint8_t slot) { return slots[slot].goes; }
uint8_t  sim_blade_watch_s(uint8_t slot) { return slots[slot].watch_s; }
uint32_t sim_blade_erase_count(uint8_t slot) { return slots[slot].erases; }
uint32_t sim_blade_write_count(uint8_t slot) { return slots[slot].writes; }
const uint8_t *sim_blade_flash(uint8_t slot) { return slots[slot].flash; }
uint32_t sim_blade_flash_version(uint8_t slot) {
    const sim_slot_t *s = &slots[slot];
    if (!flash_has_image(s)) return 0xFFFFFFFFu;
    const blade_image_header_t *h = flash_header(s);
    return (uint32_t)h->major << 16 | (uint32_t)h->minor << 8 | h->patch;
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

void sim_set_blade_fault(uint8_t slot, uint16_t fault_bits) {
    slots[slot].cond = fault_bits;
    latch3(&slots[slot], fault_bits);
}

void sim_trip_ocp(uint8_t slot) {
    if (slots[slot].gen == 3) latch3(&slots[slot], BLADE_FAULT_OCP_VBUS);
    else slots[slot].ocp_latch = true;
}

bool sim_alert_asserted(void) {
    for (int i = 0; i < NUM_PORTS; i++) {
        const sim_slot_t *s = &slots[i];
        if (!s->present) continue;
        // a gen-3 blade's MCU runs from the slot's 5 V: its ALERT# does not need EN
        if (s->gen == 3 ? (s->faults & BLADE_FAULT_ALERTING) != 0
                        : s->en && (s->ocp_latch || s->fault_bits))
            return true;
    }
    return false;
}

void sim_set_probe_ok(uint8_t slot, bool ina_ok, bool mpq_ok) {
    slots[slot].ina_ok = ina_ok;
    slots[slot].mpq_ok = mpq_ok;
}

void sim_set_blade_ok(uint8_t slot, bool ok) {
    slots[slot].blade_ok = ok;
}

void sim_set_mux_fail(bool fail) { mux_fail = fail; }
void sim_set_expander_fail(bool fail) { exp_fail = fail; }
void sim_set_load_pct(uint8_t slot, uint8_t pct) { slots[slot].load_pct = pct; }

void sim_set_temps(uint8_t slot, int16_t conv_dc, int16_t plug_dc) {
    slots[slot].temp_set = true;
    slots[slot].temp_conv_dc = conv_dc;
    slots[slot].temp_plug_dc = plug_dc;
}

void sim_model_temps(uint8_t slot) { slots[slot].temp_set = false; }

bool sim_en(uint8_t slot) { return slots[slot].en; }
bool sim_fan(void) { return fan; }
uint32_t sim_advertised_ma(uint8_t slot) { return slots[slot].adv_ma; }
uint16_t sim_advertised_mv(uint8_t slot) { return slots[slot].adv_mv; }
uint32_t sim_contract_mw(uint8_t slot) { return contract_mw(&slots[slot]); }
bool     sim_blade_port_en(uint8_t slot) { return slots[slot].port_en; }
uint16_t sim_blade_faults(uint8_t slot) { return slots[slot].faults; }
uint32_t sim_blade_clear_count(uint8_t slot) { return slots[slot].clears; }
uint32_t sim_blade_config_writes(uint8_t slot) { return slots[slot].cfg_writes; }
uint32_t sim_ina_alert_ma(uint8_t slot) { return slots[slot].ina_alert_ma; }
uint32_t sim_src_cap_count(uint8_t slot) { return slots[slot].src_caps; }
uint32_t sim_hard_reset_count(uint8_t slot) { return slots[slot].hard_resets; }
uint32_t sim_mux_reset_count(void) { return mux_resets; }
uint32_t sim_en_change_count(uint8_t slot) { return slots[slot].en_changes; }

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
    for (int i = 0; i < NUM_PORTS; i++) set_en(&slots[i], false);
    fan = false;
    exp_programmed = true;
    return true;
}

tca9539_found_t tca9539_find(void) {
    // programmed since the last power cycle (sim_reset): the outputs stand
    if (exp_fail) return TCA9539_NO_ANSWER;
    return exp_programmed ? TCA9539_OURS : TCA9539_POWER_ON;
}

bool tca9539_attach(void) {
    return tca9539_find() == TCA9539_OURS;
}

uint16_t tca9539_outputs(void) {
    uint16_t word = 0;
    for (int i = 0; i < NUM_PORTS; i++)
        if (slots[i].en) word |= 1u << TCA9539_EN_BIT(i);
    if (fan) word |= 1u << TCA9539_FAN_BIT;
    return word;
}

bool tca9539_set_en(uint8_t port, bool on) {
    if (exp_fail || port >= NUM_PORTS) return false;
    set_en(&slots[port], on);
    return true;
}

bool tca9539_set_fan(bool on) {
    if (exp_fail) return false;
    fan = on;
    return true;
}

bool tca9539_all_en_off(void) {
    if (exp_fail) return false;
    for (int i = 0; i < NUM_PORTS; i++) set_en(&slots[i], false);
    return true;
}

bool tca9539_config_lost(void) { return false; }
// The reset un-wedges the part; the engine writes the EN pattern straight back
bool tca9539_recover(void) {
    exp_fail = false;
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

static const sim_slot_t *ina(void) {
    const sim_slot_t *s = sel();
    return (s && s->present && s->gen == 2 && s->ina_ok) ? s : NULL;
}

bool ina226_probe(void) { return ina() != NULL; }
bool ina226_configure(void) { return ina() != NULL; }

bool ina226_read(ina226_reading_t *r) {
    const sim_slot_t *s = ina();
    if (!s) return false;
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
    sim_slot_t *s = (sim_slot_t *)ina();
    if (!s) return false;
    s->ina_alert_ma = ma;
    return true;
}

bool ina226_alert_tripped(bool *tripped) {
    sim_slot_t *s = (sim_slot_t *)ina();
    if (!s) return false;
    *tripped = s->ocp_latch;
    s->ocp_latch = false; // reading Mask/Enable clears the latch
    return true;
}

// ---- mpq4242 ---------------------------------------------------------------

static sim_slot_t *mpq(void) {
    sim_slot_t *s = sel();
    return (s && s->present && s->gen == 2 && s->mpq_ok) ? s : NULL;
}

bool mpq4242_probe(void) { return mpq() != NULL; }
bool mpq4242_unlock(void) { return mpq() != NULL; }

bool mpq4242_configure(uint32_t max_ma, uint32_t max_mv) {
    sim_slot_t *s = mpq();
    if (!s) return false;
    s->adv_ma = max_ma;
    s->adv_mv = (uint16_t)max_mv;
    return true;
}

bool mpq4242_config_matches(uint32_t max_ma, uint32_t max_mv, bool *matches) {
    const sim_slot_t *s = mpq();
    if (!s) return false;
    *matches = s->adv_ma == max_ma && s->adv_mv == (uint16_t)max_mv;
    return true;
}

bool mpq4242_read_status(mpq4242_status_t *st) {
    const sim_slot_t *s = mpq();
    if (!s) return false;
    st->attached = s->attached;
    st->fault_bits = s->fault_bits;
    st->contract_mw = status3_mw(s);
    st->selected_pdo = pdo_of(s);
    if (st->selected_pdo == 7) st->selected_pdo = 5; // STATUS2 reports the 21 V range as PDO5 here
    return true;
}

bool mpq4242_set_max_current_ma(uint32_t ma) {
    sim_slot_t *s = mpq();
    if (!s) return false;
    s->adv_ma = ma; // takes effect at the next src_cap, like the real part
    return true;
}

bool mpq4242_set_max_voltage_mv(uint32_t max_mv) {
    sim_slot_t *s = mpq();
    if (!s) return false;
    s->adv_mv = (uint16_t)max_mv; // takes effect at the next src_cap, like the real part
    return true;
}

bool mpq4242_set_pdo_fixed(uint8_t pdo, uint16_t mv, uint32_t ma, bool enabled) {
    (void)pdo; (void)mv; (void)ma; (void)enabled;
    return mpq() != NULL;
}

bool mpq4242_set_pdo_pps(uint8_t pdo, uint16_t min_mv, uint16_t max_mv, uint32_t ma, bool enabled) {
    (void)pdo; (void)min_mv; (void)max_mv; (void)ma; (void)enabled;
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

// ---- gen-3 blade -----------------------------------------------------------

static sim_slot_t *blade3(void) {
    sim_slot_t *s = sel();
    return (s && s->present && s->gen == 3 && s->blade_ok && !s->loader && !s->locked_up) ? s : NULL;
}

static sim_slot_t *loader3(void) {
    sim_slot_t *s = sel();
    return (s && s->present && s->gen == 3 && s->blade_ok && s->loader) ? s : NULL;
}

bool blade3_probe(void) { return blade3() != NULL; }

bool blade3_read_identity(blade3_identity_t *id) {
    const sim_slot_t *s = blade3();
    if (!s) return false;
    const blade_image_header_t *h = flash_header(s);
    id->proto = h->proto;
    id->major = h->major;
    id->minor = h->minor;
    id->patch = h->patch;
    id->reset_cause = BLADE_RESET_POWER;
    id->caps = BLADE_CAP_PPS;
    id->boot = s->boot_via_loader ? BLADE_BOOT_VIA_LOADER : 0;
    return true;
}

bool blade3_read_status(blade3_status_t *st) {
    const sim_slot_t *s = blade3();
    if (!s) return false;
    bool up = armed3(s);
    bool contract = up && s->attached && s->con_mv != 0;
    st->status = (uint8_t)((s->attached ? BLADE_ST_ATTACHED : 0) |
                           (contract ? BLADE_ST_CONTRACT : 0) |
                           (contract && s->con_mv > 5000 && pdo_of(s) == 7 ? BLADE_ST_PPS : 0) |
                           (up && s->attached ? BLADE_ST_VBUS_ON : 0) |
                           (s->configured ? BLADE_ST_CONFIGURED : 0) |
                           (s->faults ? BLADE_ST_FAULT : 0) |
                           (s->en ? BLADE_ST_EN : 0));
    st->pdo = contract ? pdo_of(s) : 0;
    st->faults = s->faults;
    st->contract_mv = contract ? s->con_mv : 0;
    st->contract_ma = contract ? (uint16_t)s->con_ma : 0;
    if (!up || !s->attached) {
        st->vbus_mv = 0;
        st->iout_ma = 0;
    } else {
        st->vbus_mv = s->con_mv ? s->con_mv : 5000; // vSafe5V before an explicit contract
        st->iout_ma = (uint16_t)((s->con_ma * s->load_pct) / 100);
    }
    st->vout_mv = up ? (s->con_mv ? s->con_mv : 5000) : 0;
    // Thermometers: room temperature plus warming with the load (100 W puts
    // the converter at 50 degC, 5 A the receptacle at 50 degC), unless the
    // script pinned them.
    uint32_t p_mw = ((uint32_t)st->vbus_mv * st->iout_ma) / 1000;
    st->temp_conv_dc = s->temp_set ? s->temp_conv_dc : (int16_t)(250 + p_mw / 400);
    st->temp_plug_dc = s->temp_set ? s->temp_plug_dc : (int16_t)(250 + st->iout_ma / 20);
    st->temp_mcu_dc = (int16_t)(300 + p_mw / 2000);
    return true;
}

bool blade3_read_config(blade3_config_t *c) {
    const sim_slot_t *s = blade3();
    if (!s) return false;
    c->port_en = s->port_en;
    c->max_ma = (uint16_t)s->adv_ma;
    c->max_mv = s->adv_mv;
    c->watch_s = s->watch_s;
    return true;
}

bool blade3_write_config(const blade3_config_t *c) {
    sim_slot_t *s = blade3();
    if (!s) return false;
    s->port_en = c->port_en;
    s->adv_ma = c->max_ma > BLADE_MAX_MA_LIMIT ? BLADE_MAX_MA_LIMIT : c->max_ma; // stored clamped
    s->adv_mv = c->max_mv;
    s->watch_s = c->watch_s;
    s->configured = true;
    s->cfg_writes++;
    renegotiate(s); // the blade re-advertises whenever its table changes
    return true;
}

bool blade3_command(uint8_t cmd) {
    sim_slot_t *s = blade3();
    if (!s) return false;
    switch (cmd) {
    case BLADE_CMD_SRC_CAP:
        s->src_caps++;
        renegotiate(s);
        break;
    case BLADE_CMD_HARD_RESET:
        s->hard_resets++;
        break;
    case BLADE_CMD_CLEAR_FAULTS:
        s->clears++;
        s->faults = s->cond; // a condition still present latches straight back
        renegotiate(s);
        break;
    case BLADE_CMD_RESET: // the flash declared empty for the boot that follows
        mcu_reset(s, false);
        s->loader = true;
        break;
    case BLADE_CMD_BOOT_OPT:
        if (!s->boot_opt_stuck) s->boot_via_loader = true;
        mcu_reset(s, false);
        break;
    default:
        break;
    }
    return true;
}

// ---- the ROM bootloader (stboot.h) -----------------------------------------

// Flash operations answer BUSY this many times before they are done, so the
// engine's polling gets exercised.
#define SIM_BUSY_POLLS 2

static stboot_result_t busy_or_ok(sim_slot_t *s) {
    if (s->busy_polls) return STBOOT_BUSY;
    return STBOOT_OK;
}

stboot_result_t stboot_version(uint8_t *version) {
    if (!loader3()) return STBOOT_SILENT;
    *version = 0x12;
    return STBOOT_OK;
}

stboot_result_t stboot_id(uint16_t *pid) {
    if (!loader3()) return STBOOT_SILENT;
    *pid = BLADE_LOADER_DEVICE_ID;
    return STBOOT_OK;
}

stboot_result_t stboot_read(uint32_t addr, uint8_t *buf, size_t n) {
    const sim_slot_t *s = loader3();
    if (!s) return STBOOT_SILENT;
    if (addr < BLADE_IMAGE_BASE || addr - BLADE_IMAGE_BASE + n > SIM_FLASH_SIZE) return STBOOT_NACK;
    memcpy(buf, s->flash + (addr - BLADE_IMAGE_BASE), n);
    return STBOOT_OK;
}

stboot_result_t stboot_erase(uint16_t first, uint16_t count, bool no_stretch) {
    sim_slot_t *s = loader3();
    if (!s) return STBOOT_SILENT;
    if ((uint32_t)(first + count) * BLADE_IMAGE_PAGE > SIM_FLASH_SIZE) return STBOOT_NACK;
    memset(s->flash + first * BLADE_IMAGE_PAGE, 0xFF, count * BLADE_IMAGE_PAGE);
    if (first == 0) s->erases++;
    s->busy_polls = no_stretch ? SIM_BUSY_POLLS : 0;
    return busy_or_ok(s);
}

stboot_result_t stboot_write(uint32_t addr, const uint8_t *data, size_t n, bool no_stretch) {
    sim_slot_t *s = loader3();
    if (!s) return STBOOT_SILENT;
    if (addr < BLADE_IMAGE_BASE || addr - BLADE_IMAGE_BASE + n > SIM_FLASH_SIZE) return STBOOT_NACK;
    for (size_t k = 0; k < n; k++) // flash programs erased words only (PROGERR otherwise)
        if (s->flash[addr - BLADE_IMAGE_BASE + k] != 0xFF) return STBOOT_NACK;
    if (s->cut_after_writes && --s->cut_after_writes == 0) {
        // the lights go out halfway through this chunk: half of it lands
        memcpy(s->flash + (addr - BLADE_IMAGE_BASE), data, n / 2);
        mcu_reset(s, false);
        return STBOOT_SILENT;
    }
    memcpy(s->flash + (addr - BLADE_IMAGE_BASE), data, n);
    s->writes++;
    s->busy_polls = no_stretch ? 1 : 0;
    return busy_or_ok(s);
}

stboot_result_t stboot_checksum(uint32_t addr, uint32_t len) {
    sim_slot_t *s = loader3();
    if (!s) return STBOOT_SILENT;
    if (addr < BLADE_IMAGE_BASE || len == 0 || len & 3 || addr - BLADE_IMAGE_BASE + len > SIM_FLASH_SIZE)
        return STBOOT_NACK;
    s->crc_len = len;
    s->busy_polls = SIM_BUSY_POLLS;
    return STBOOT_BUSY;
}

// The CRC as the controller computes it (blade_update.c): the model of
// the CRC unit both sides share
uint32_t blade_image_crc(const uint8_t *data, uint32_t size, uint32_t len);

stboot_result_t stboot_checksum_result(uint32_t *crc) {
    const sim_slot_t *s = loader3();
    if (!s) return STBOOT_SILENT;
    *crc = blade_image_crc(s->flash, s->crc_len, s->crc_len);
    return STBOOT_OK;
}

stboot_result_t stboot_go(uint32_t addr) {
    sim_slot_t *s = loader3();
    if (!s) return STBOOT_SILENT;
    if (addr != BLADE_IMAGE_BASE) return STBOOT_NACK;
    s->goes++;
    mcu_reset(s, true);
    return STBOOT_OK;
}

stboot_result_t stboot_poll(void) {
    sim_slot_t *s = loader3();
    if (!s) return STBOOT_SILENT;
    if (s->busy_polls) s->busy_polls--;
    return busy_or_ok(s);
}
