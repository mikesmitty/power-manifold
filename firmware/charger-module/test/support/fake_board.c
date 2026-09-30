#include "fake_board.h"

#include "board.h"
#include "dac.h"
#include "fake_bus.h"
#include "hw.h"
#include "meter.h"
#include "tcpp02.h"
#include "tps55288.h"

#define SLEW_MV_PER_MS       2500 // the converter's, TPS55288_SR_2V5_PER_MS
#define DISCHARGE_MV_PER_MS  100

static uint32_t now_ms;
static uint32_t vdda_mv;
static uint16_t dac_code;
static bool en_drive, held, latched;
static bool loaded, stuck, port_flt, conv_flt, tcpp_fitted;
static bool en_line, alert;
static int16_t temp_conv, temp_plug;
static unsigned feeds;
static uint32_t vout_mv, vbus_mv, peak_mv;
static bool ever_tripped, ever_bare_output;

static bool boot_via_loader;
static unsigned loader_resets, boot_option_writes;

static void tcpp_follows_en(void);
static void comparator(void);
static void written(void);

void fake_board_reset(void) {
    boot_via_loader = false;
    loader_resets = boot_option_writes = 0;
    now_ms = 0;
    vdda_mv = 3300;
    dac_code = 0;
    en_drive = held = latched = false;
    loaded = stuck = port_flt = conv_flt = false;
    tcpp_fitted = true;
    en_line = alert = false;
    temp_conv = temp_plug = 250;
    feeds = 0;
    vout_mv = vbus_mv = peak_mv = 0;
    ever_tripped = ever_bare_output = false;
    fake_bus_reset();
    fake_bus_on_write(written);
    tcpp_follows_en();
}

void fake_board_set_load(bool l) { loaded = l; }
void fake_board_set_en(bool high) { en_line = high; }
void fake_board_set_temps_dc(int16_t conv, int16_t plug) {
    temp_conv = conv;
    temp_plug = plug;
}
bool fake_board_alert(void) { return alert; }
unsigned fake_board_watchdog_feeds(void) { return feeds; }
void fake_board_fit_tcpp02(bool fitted) { tcpp_fitted = fitted; }
void fake_board_set_vout_mv(uint32_t mv) {
    vout_mv = mv;
    comparator();
}
void fake_board_set_vdda_mv(uint32_t mv) { vdda_mv = mv; }
void fake_board_stick_vbus(bool s) { stuck = s; }
void fake_board_set_port_fault(bool low) { port_flt = low; }
void fake_board_set_converter_fault(bool low) { conv_flt = low; }
uint32_t fake_board_vbus_mv(void) { return vbus_mv; }
uint32_t fake_board_vout_mv(void) { return vout_mv; }
uint32_t fake_board_peak_vbus_mv(void) { return peak_mv; }
bool fake_board_comparator_tripped(void) { return ever_tripped; }
bool fake_board_output_on_with_switch_open(void) { return ever_bare_output; }

uint32_t fake_board_trip_mv(void) {
    return (uint32_t)(((uint64_t)dac_code * vdda_mv * SENSE_DIV_NUM) / 4095);
}

static uint32_t toward(uint32_t v, uint32_t target, uint32_t step) {
    if (v < target) return target - v < step ? target : v + step;
    return v - target < step ? target : v - step;
}

// TCPP_EN low is the TCPP02's OFF state: registers reset, nothing on the bus
static void tcpp_follows_en(void) {
    bool on = tcpp_fitted && en_drive && !held;
    fake_bus_set_present(ADDR_TCPP02, on);
    if (on) return;
    fake_bus_poke(ADDR_TCPP02, TCPP02_REG_CTRL, 0);
    fake_bus_poke(ADDR_TCPP02, TCPP02_REG_ACK, 0);
}

static void comparator(void) {
    bool over = vout_mv > fake_board_trip_mv();
    if (over && en_drive && !held) {
        latched = true;
        ever_tripped = true;
    }
    held = over;
    tcpp_follows_en();
}

static bool output_enabled(void) {
    return (fake_bus_peek(ADDR_TPS55288, TPS55288_REG_MODE) & TPS55288_MODE_OE) != 0;
}

static uint8_t tcpp_ctrl(void) {
    return (en_drive && !held) ? fake_bus_peek(ADDR_TCPP02, TCPP02_REG_ACK) : 0;
}

// after every register write, not only once a millisecond: two writes in the
// wrong order have no time between them
static void written(void) {
    if (output_enabled() && !(tcpp_ctrl() & TCPP02_CTRL_GDP)) ever_bare_output = true;
}

static void step_ms(void) {
    now_ms++;
    uint8_t mode = fake_bus_peek(ADDR_TPS55288, TPS55288_REG_MODE);
    uint8_t ctrl = tcpp_ctrl();
    bool output = output_enabled();
    bool closed = (ctrl & TCPP02_CTRL_GDP) != 0;
    uint16_t ref = (uint16_t)(fake_bus_peek(ADDR_TPS55288, TPS55288_REG_REF_LSB) |
                              (fake_bus_peek(ADDR_TPS55288, TPS55288_REG_REF_MSB) << 8));
    uint32_t target = tps55288_ref_mv(ref);

    if (output && !closed) ever_bare_output = true;
    if (stuck) return;

    if (output) {
        bool can_sink = (mode & TPS55288_MODE_BY_REG) && (mode & TPS55288_MODE_FPWM);
        if (vout_mv < target || can_sink || (loaded && closed))
            vout_mv = toward(vout_mv, target, SLEW_MV_PER_MS);
    } else if (mode & TPS55288_MODE_DISCHG) {
        vout_mv = toward(vout_mv, 0, DISCHARGE_MV_PER_MS);
    }

    if (closed) {
        vbus_mv = vout_mv;
    } else if ((ctrl & TCPP02_CTRL_VBUSD) || loaded) {
        vbus_mv = toward(vbus_mv, 0, DISCHARGE_MV_PER_MS);
    }
    if (vbus_mv > peak_mv) peak_mv = vbus_mv;
    comparator();
}

// hw.h
uint32_t hw_ms(void) { return now_ms; }
void hw_delay_ms(uint32_t ms) {
    while (ms--) step_ms();
}
void hw_tcpp_en(bool on) {
    en_drive = on;
    comparator();
}
bool hw_tcpp_en_sense(void) { return en_drive && !held; }
bool hw_ovp_trip_latched(void) { return latched; }
void hw_ovp_trip_clear(void) { latched = false; }
bool hw_port_fault(void) { return port_flt; }
bool hw_en(void) { return en_line; }
void hw_alert(bool asserted) { alert = asserted; }
void hw_led(bool on) { (void)on; }
void hw_watchdog_feed(void) { feeds++; }
void fake_board_set_boot_via_loader(bool set) { boot_via_loader = set; }
unsigned fake_board_loader_resets(void) { return loader_resets; }
unsigned fake_board_boot_option_writes(void) { return boot_option_writes; }
bool hw_boot_via_loader(void) { return boot_via_loader; }
void hw_reset_to_loader(void) { loader_resets++; }
void hw_program_boot_via_loader(void) {
    boot_option_writes++;
    boot_via_loader = true;
}
void hw_backplane_lock(void) {}
void hw_backplane_unlock(void) {}
bool hw_converter_fault(void) { return conv_flt; }

// dac.h
void dac_set(uint16_t code) {
    dac_code = code;
    comparator();
}

// meter.h
uint32_t meter_vdda_mv(void) { return vdda_mv; }
uint32_t meter_vbus_mv(void) { return vbus_mv; }
uint32_t meter_vout_mv(void) { return vout_mv; }
uint32_t meter_iout_ma(void) { return loaded ? 1000 : 0; }
int16_t meter_temp_conv_dc(void) { return temp_conv; }
int16_t meter_temp_plug_dc(void) { return temp_plug; }
int16_t meter_temp_mcu_dc(void) { return 300; }
