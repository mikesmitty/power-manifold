#include "blade.h"

#include "blade3.h"
#include "ina226.h"
#include "mpq4242.h"
#include "settings.h"
#include "stboot.h"

// ---- gen 3 -----------------------------------------------------------------

// The blade's fault word in the port vocabulary. A restart is not a fault:
// the port shows it as its configuration gone.
static uint16_t fold3(uint16_t f) {
    uint16_t u = 0;
    if (f & BLADE_FAULT_OVP)       u |= PORT_FAULT_OVP;
    if (f & BLADE_FAULT_OCP_VBUS)  u |= PORT_FAULT_OCP;
    if (f & BLADE_FAULT_OCP_VCONN) u |= PORT_FAULT_VCONN;
    if (f & BLADE_FAULT_OVP_CC)    u |= PORT_FAULT_CC;
    if (f & BLADE_FAULT_OTP_PORT)  u |= PORT_FAULT_PORT_HOT;
    if (f & (BLADE_FAULT_CONV_SCP | BLADE_FAULT_CONV_OCP | BLADE_FAULT_CONV_OVP))
        u |= PORT_FAULT_CONVERTER;
    if (f & BLADE_FAULT_OT_CONV)   u |= PORT_FAULT_CONV_HOT;
    if (f & BLADE_FAULT_OT_PLUG)   u |= PORT_FAULT_PLUG_HOT;
    if (f & (BLADE_FAULT_BUS | BLADE_FAULT_VBUS | BLADE_FAULT_PD))
        u |= PORT_FAULT_BLADE;
    return u;
}

static blade3_config_t config3(uint32_t max_ma, uint32_t max_mv) {
    blade3_config_t c = {
        .port_en = true,
        .max_ma = (uint16_t)(max_ma > BLADE_MAX_MA_LIMIT ? BLADE_MAX_MA_LIMIT : max_ma),
        .max_mv = (uint16_t)(max_mv > BLADE_MAX_MV_ALL ? BLADE_MAX_MV_ALL : max_mv),
        .watch_s = g_settings.blade_watch_s,
    };
    return c;
}

static bool poll3(blade_status_t *st) {
    blade3_status_t s;
    if (!blade3_read_status(&s)) return false;
    st->attached     = (s.status & BLADE_ST_ATTACHED) != 0;
    st->configured   = (s.status & BLADE_ST_CONFIGURED) != 0;
    st->selected_pdo = s.pdo;
    uint16_t raw     = (uint16_t)(s.faults & BLADE_FAULT_ALERTING);
    st->fault_bits   = fold3(raw);
    st->fault_detail = raw ? (PORT_FAULT_ARG_GEN3 | raw) : 0;
    st->contract_mw  = ((uint32_t)s.contract_mv * s.contract_ma) / 1000;
    st->bus_mv       = s.vbus_mv;
    st->current_ma   = s.iout_ma;
    st->power_mw     = ((uint32_t)s.vbus_mv * s.iout_ma) / 1000;
    st->has_temps    = true;
    st->temp_conv_dc = s.temp_conv_dc;
    st->temp_plug_dc = s.temp_plug_dc;
    st->temp_mcu_dc  = s.temp_mcu_dc;
    return true;
}

// A fresh start, as cycling EN gives a gen-2 part: whatever the blade
// latched before (its own restart included) is cleared before it is told
// its limits.
static uint16_t setup3(uint32_t max_ma, uint32_t max_mv) {
    blade3_config_t c = config3(max_ma, max_mv);
    if (!blade3_probe() || !blade3_command(BLADE_CMD_CLEAR_FAULTS) || !blade3_write_config(&c))
        return PROBE_FAIL_BLADE;
    return 0;
}

static uint16_t adopt3(uint32_t max_ma, uint32_t max_mv, blade_status_t *st) {
    if (!blade3_probe() || !poll3(st)) return PROBE_FAIL_BLADE;
    if (st->fault_bits) return 0; // the caller's fault path takes it from here
    blade3_config_t want = config3(max_ma, max_mv), have;
    if (!blade3_read_config(&have)) return PROBE_FAIL_BLADE;
    bool same = st->configured && have.port_en == want.port_en &&
                have.max_ma == want.max_ma && have.max_mv == want.max_mv &&
                have.watch_s == want.watch_s;
    if (!same && !blade3_write_config(&want)) return PROBE_FAIL_BLADE;
    return 0;
}

// ---- gen 2 -----------------------------------------------------------------

static bool poll2(bool alert, blade_status_t *st) {
    ina226_reading_t r;
    if (ina226_read(&r)) {
        st->bus_mv = r.bus_mv;
        st->current_ma = r.current_ma;
        st->power_mw = r.power_mw;
    }
    bool trip = false;
    if (alert) ina226_alert_tripped(&trip); // the read clears the latch
    mpq4242_status_t m;
    bool answered = mpq4242_read_status(&m);
    if (answered) {
        st->attached = m.attached;
        st->selected_pdo = m.selected_pdo;
        st->contract_mw = m.contract_mw;
        st->fault_bits = m.fault_bits;
    } else if (!trip) {
        return false;
    } else {
        st->fault_bits = 0; // the trip alone; the part's flags are unreadable
    }
    st->configured = true;
    if (trip) st->fault_bits |= PORT_FAULT_OCP;
    st->fault_detail = trip ? 1 : 0;
    return true;
}

// The INA226 first: it is the part that answers on a dark segment, and its
// emergency trip is fixed at 125 % of the blade's ceiling.
static uint16_t setup2(uint32_t max_ma, uint32_t max_mv) {
    if (!ina226_probe() || !ina226_configure() ||
        !ina226_set_alert_ma((PORT_HW_MAX_MA * 125) / 100))
        return PROBE_FAIL_INA226;
    mpq4242_status_t m = {0};
    if (!mpq4242_probe() || !mpq4242_read_status(&m) || !mpq4242_configure(max_ma, max_mv) ||
        (m.attached && !mpq4242_send_src_cap()))
        return PROBE_FAIL_MPQ4242;
    return 0;
}

static uint16_t adopt2(uint32_t max_ma, uint32_t max_mv, blade_status_t *st) {
    if (!ina226_probe()) return PROBE_FAIL_INA226;
    if (!mpq4242_probe()) return PROBE_FAIL_MPQ4242;
    if (!poll2(true, st)) return PROBE_FAIL_MPQ4242;
    if (st->fault_bits) return 0;
    if (!ina226_configure() || // idempotent: the same values it already holds
        !ina226_set_alert_ma((PORT_HW_MAX_MA * 125) / 100))
        return PROBE_FAIL_INA226;
    bool matches = false;
    if (!mpq4242_config_matches(max_ma, max_mv, &matches)) return PROBE_FAIL_MPQ4242;
    if (!matches && (!mpq4242_configure(max_ma, max_mv) ||
                     (st->attached && !mpq4242_send_src_cap())))
        return PROBE_FAIL_MPQ4242;
    return 0;
}

// ---- the interface ---------------------------------------------------------

blade_gen_t blade_detect(void) {
    if (blade3_probe()) return BLADE_GEN_3;
    uint8_t version;
    if (stboot_version(&version) == STBOOT_OK) return BLADE_GEN_LOADER;
    if (ina226_probe()) return BLADE_GEN_2;
    return BLADE_GEN_NONE;
}

bool blade_identity(blade_gen_t gen, blade_identity_t *id) {
    if (gen != BLADE_GEN_3) return false;
    blade3_identity_t b;
    if (!blade3_read_identity(&b)) return false;
    id->proto = b.proto;
    id->major = b.major;
    id->minor = b.minor;
    id->patch = b.patch;
    id->boot = b.boot;
    return true;
}

bool blade_request_loader(blade_gen_t gen, bool boot_option) {
    if (gen != BLADE_GEN_3) return false;
    return blade3_command(boot_option ? BLADE_CMD_BOOT_OPT : BLADE_CMD_RESET);
}

uint16_t blade_setup(blade_gen_t gen, uint32_t max_ma, uint32_t max_mv) {
    switch (gen) {
    case BLADE_GEN_2: return setup2(max_ma, max_mv);
    case BLADE_GEN_3: return setup3(max_ma, max_mv);
    default:          return PROBE_FAIL_NONE;
    }
}

uint16_t blade_adopt(blade_gen_t gen, uint32_t max_ma, uint32_t max_mv, blade_status_t *st) {
    switch (gen) {
    case BLADE_GEN_2: return adopt2(max_ma, max_mv, st);
    case BLADE_GEN_3: return adopt3(max_ma, max_mv, st);
    default:          return PROBE_FAIL_NONE;
    }
}

bool blade_poll(blade_gen_t gen, bool alert, blade_status_t *st) {
    switch (gen) {
    case BLADE_GEN_2: return poll2(alert, st);
    case BLADE_GEN_3: return poll3(st);
    default:          return false;
    }
}

bool blade_set_limits(blade_gen_t gen, uint32_t ma, uint32_t mv, bool readvertise) {
    switch (gen) {
    case BLADE_GEN_2:
        if (!mpq4242_set_max_current_ma(ma) || !mpq4242_set_max_voltage_mv(mv)) return false;
        return !readvertise || mpq4242_send_src_cap();
    case BLADE_GEN_3: {
        blade3_config_t c = config3(ma, mv);
        return blade3_write_config(&c);
    }
    default:
        return false;
    }
}

bool blade_send_src_cap(blade_gen_t gen) {
    switch (gen) {
    case BLADE_GEN_2: return mpq4242_send_src_cap();
    case BLADE_GEN_3: return blade3_command(BLADE_CMD_SRC_CAP);
    default:          return false;
    }
}

bool blade_hard_reset(blade_gen_t gen) {
    switch (gen) {
    case BLADE_GEN_2: return mpq4242_send_hard_reset();
    case BLADE_GEN_3: return blade3_command(BLADE_CMD_HARD_RESET);
    default:          return false;
    }
}

void blade_faulted(blade_gen_t gen) {
    if (gen == BLADE_GEN_3) blade3_command(BLADE_CMD_CLEAR_FAULTS); // best effort: the re-probe clears again
}
