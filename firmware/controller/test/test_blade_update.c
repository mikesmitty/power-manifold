#include <string.h>

#include "blade.h"
#include "blade_bundle.h"
#include "blade_regs.h"
#include "blade_update.h"
#include "budget.h"
#include "fake_bundle.h"
#include "fan_policy.h"
#include "fault_text.h"
#include "manifold.h"
#include "microtest.h"
#include "port_fsm.h"
#include "settings.h"
#include "tca9539.h"
#include "test_support.h"

// Gen-3 blade firmware through the ROM bootloader: what the port engine
// does with a blade it finds there, and with one running something other
// than the firmware the controller carries.

#define IMAGE_LEN (3 * BLADE_IMAGE_PAGE + 200) // pages 0-3, the last one partial

static void seat3(uint8_t slot) {
    sim_set_gen(slot, 3);
    sim_set_present(slot, true);
}

// Ticks until the port settles in a state that is not PROBE or UPDATE, or
// the allowance runs out; returns the ticks it took.
static uint32_t settle(uint8_t port, uint32_t max_ticks) {
    uint32_t n = 0;
    while (n < max_ticks) {
        tick(1);
        n++;
        uint8_t st = port_state(port);
        if (st != PORT_STATE_PROBE && st != PORT_STATE_UPDATE) break;
    }
    return n;
}

// The controller restarts; the backplane (the sim) stays as it was:
// engine_main's start-up again (see test_warm.c).
static void controller_reboot(void) {
    budget_init(g_settings.budget_mw);
    port_fsm_init();
    tca9539_attach();
    fan_policy_init(g_settings.fan_auto != 0, (tca9539_outputs() >> TCA9539_FAN_BIT) & 1u);
    evt_clear();
    uint16_t inputs = 0;
    tca9539_read_inputs(&inputs);
    bool present[NUM_PORTS];
    for (uint8_t i = 0; i < NUM_PORTS; i++) present[i] = tca9539_present_from(inputs, i);
    port_fsm_boot_inventory(present, (uint8_t)(tca9539_outputs() & 0x3F), now_ms);
}

static bool flash_holds_bundle(uint8_t slot) {
    const uint8_t *flash = sim_blade_flash(slot);
    const blade_image_header_t *h = blade_bundle_header();
    if (!h) return false;
    for (uint32_t i = 0; i < h->length; i++) {
        uint8_t want = i < blade_bundle_size() ? blade_bundle_data()[i] : 0xFF;
        if (flash[i] != want) return false;
    }
    return true;
}

static void test_crc_matches_the_stm32_unit(void) {
    // the CRC unit's well-known results for one and two zero words
    static const uint8_t zero[8] = {0};
    MT_ASSERT_EQ(blade_image_crc(zero, 4, 4), 0xC704DD7Bu);
    MT_ASSERT_EQ(blade_image_crc(zero, 8, 8), 0x6904BB59u);
    static const uint8_t w[4] = {0x78, 0x56, 0x34, 0x12}; // 0x12345678, least significant byte first
    MT_ASSERT_EQ(blade_image_crc(w, 4, 4), 0xDF8A8A2Bu);
    // bytes past the data are the erased flash's 0xFF
    static const uint8_t ff[4] = {0xFF, 0xFF, 0xFF, 0xFF};
    MT_ASSERT_EQ(blade_image_crc(zero, 0, 4), blade_image_crc(ff, 4, 4));
}

// A blank blade (or one that lost its image) is programmed with the bundle,
// the first page last, and started.
static void test_blank_blade_is_programmed(void) {
    support_reset(360000);
    fake_bundle_make(0, 2, 0, IMAGE_LEN);
    sim_set_gen(0, 3);
    sim_blade_erase(0);
    sim_set_present(0, true);
    MT_ASSERT(sim_blade_in_loader(0));

    tick(2);
    MT_ASSERT_EQ(port_state(0), PORT_STATE_UPDATE);
    MT_ASSERT_EQ(tele.port[0].gen, 3);
    MT_ASSERT(!sim_en(0)); // dark while it is written
    bool saw_progress = false;
    for (int k = 0; k < 60 && port_state(0) == PORT_STATE_UPDATE; k++) {
        tick(1);
        if (tele.port[0].update_pct > 0 && tele.port[0].update_pct < 100) saw_progress = true;
    }
    MT_ASSERT(saw_progress);
    settle(0, 100);
    MT_ASSERT_EQ(port_state(0), PORT_STATE_IDLE);
    MT_ASSERT(!sim_blade_in_loader(0));
    MT_ASSERT(flash_holds_bundle(0));
    MT_ASSERT_EQ(sim_blade_flash_version(0), UPDATE_VERSION(0, 2, 0));
    MT_ASSERT_EQ(sim_blade_erase_count(0), 1);
    MT_ASSERT_EQ(sim_blade_write_count(0), (IMAGE_LEN + 255) / 256);
    MT_ASSERT_EQ(sim_blade_go_count(0), 1);
    const engine_evt_t *e = evt_last(EVT_UPDATE, 0);
    MT_ASSERT(e != NULL);
    MT_ASSERT_EQ(e->code, UPDATE_WRITTEN);
    MT_ASSERT_EQ(e->arg, UPDATE_VERSION(0, 2, 0));
    MT_ASSERT_EQ(evt_count(EVT_PROBE_FAIL, 0), 0);
    MT_ASSERT(sim_en(0)); // powered the normal way afterwards
}

// A blade set to boot through its bootloader and holding the bundled
// version is checked and started, nothing written.
static void test_matching_image_is_started(void) {
    support_reset(360000);
    const uint8_t *img = fake_bundle_make(0, 2, 0, IMAGE_LEN);
    sim_set_gen(0, 3);
    sim_blade_flash_image(0, img, IMAGE_LEN);
    sim_blade_set_boot_via_loader(0, true);
    sim_set_present(0, true);
    MT_ASSERT(sim_blade_in_loader(0));

    settle(0, 100);
    MT_ASSERT_EQ(port_state(0), PORT_STATE_IDLE);
    MT_ASSERT_EQ(sim_blade_erase_count(0), 0);
    MT_ASSERT_EQ(sim_blade_write_count(0), 0);
    MT_ASSERT_EQ(sim_blade_go_count(0), 1);
    const engine_evt_t *e = evt_last(EVT_UPDATE, 0);
    MT_ASSERT(e != NULL);
    MT_ASSERT_EQ(e->code, UPDATE_STARTED);

    // a restart lands it in the bootloader again: the same short trip
    sim_blade_restart(0);
    MT_ASSERT(sim_blade_in_loader(0));
    tick(12); // its silence is noticed after ten polls
    settle(0, 100);
    MT_ASSERT_EQ(port_state(0), PORT_STATE_IDLE);
    MT_ASSERT_EQ(sim_blade_go_count(0), 2);
    MT_ASSERT_EQ(sim_blade_erase_count(0), 0);
}

// A running blade on another version is sent to its bootloader and
// rewritten. There is no setting to keep what it holds.
static void test_other_version_is_rewritten(void) {
    support_reset(360000);
    fake_bundle_make(0, 2, 0, IMAGE_LEN);
    seat3(0); // the sim's fresh blade runs 0.0.1
    sim_attach(0, 9000, 2000); // plugged in before the port has had power: it loses nothing by waiting
    settle(0, 200);
    MT_ASSERT_EQ(port_state(0), PORT_STATE_IDLE);
    MT_ASSERT_EQ(sim_blade_flash_version(0), UPDATE_VERSION(0, 2, 0));
    MT_ASSERT(flash_holds_bundle(0));
    MT_ASSERT_EQ(sim_blade_erase_count(0), 1);
    MT_ASSERT_EQ(evt_count(EVT_PROBE_FAIL, 0), 0);
    tick(2);
    MT_ASSERT_EQ(port_state(0), PORT_STATE_ACTIVE); // and the sink is served again
}

// The boot option is programmed once, when the setting asks for it, whether
// or not there is an image to bring the blade to.
static void test_boot_option_is_programmed_once(void) {
    support_reset(360000);
    g_settings.blade_boot_via_loader = 1;
    seat3(0);
    MT_ASSERT(!sim_blade_boot_via_loader(0));
    settle(0, 100);
    MT_ASSERT_EQ(port_state(0), PORT_STATE_IDLE);
    MT_ASSERT(sim_blade_boot_via_loader(0));
    MT_ASSERT_EQ(sim_blade_go_count(0), 1); // the reset landed in the bootloader; started from there
    MT_ASSERT_EQ(sim_blade_erase_count(0), 0); // nothing bundled: what it holds is what runs
    MT_ASSERT_EQ(evt_count(EVT_UPDATE, 0), 2); // the option, then the start
    const engine_evt_t *e = evt_last(EVT_UPDATE, 0);
    MT_ASSERT_EQ(e->code, UPDATE_STARTED);

    sim_blade_restart(0);
    tick(12);
    settle(0, 100);
    MT_ASSERT_EQ(port_state(0), PORT_STATE_IDLE);
    MT_ASSERT_EQ(evt_count(EVT_UPDATE, 0), 3); // only the start: the option holds

    // with a bundle as well: the option first, the image under it, one trip for both
    support_reset(360000);
    g_settings.blade_boot_via_loader = 1;
    fake_bundle_make(0, 2, 0, IMAGE_LEN);
    seat3(0);
    tick(2);
    MT_ASSERT(sim_blade_boot_via_loader(0)); // set before anything is erased
    MT_ASSERT_EQ(sim_blade_erase_count(0), 0);
    settle(0, 300);
    MT_ASSERT_EQ(port_state(0), PORT_STATE_IDLE);
    MT_ASSERT_EQ(sim_blade_flash_version(0), UPDATE_VERSION(0, 2, 0));
    MT_ASSERT_EQ(sim_blade_go_count(0), 1);
    MT_ASSERT_EQ(evt_count(EVT_PROBE_FAIL, 0), 0);
}

// An option that will not take costs three tries and no more, and never
// the port: the blade runs, so the port is served.
static void test_a_stuck_option_leaves_the_port_working(void) {
    support_reset(360000);
    g_settings.blade_boot_via_loader = 1;
    seat3(0);
    sim_blade_set_boot_opt_stuck(0, true);
    sim_attach(0, 9000, 2000);
    settle(0, 400);
    tick(2);
    MT_ASSERT_EQ(port_state(0), PORT_STATE_ACTIVE);
    MT_ASSERT(!sim_blade_boot_via_loader(0));
    MT_ASSERT_EQ(evt_count(EVT_UPDATE, 0), 3); // UPDATE_ROUNDS_MAX asks
    MT_ASSERT_EQ(evt_count(EVT_PROBE_FAIL, 0), 1);
    const engine_evt_t *e = evt_last(EVT_PROBE_FAIL, 0);
    MT_ASSERT_EQ(e->code, PROBE_FAIL_UPDATE);
    MT_ASSERT_EQ(e->arg, UPDATE_FAIL_STUCK);
    fault_rec_t r = {.type = EVT_PROBE_FAIL, .code = e->code, .arg = e->arg};
    char text[48];
    fault_text(&r, text, sizeof text);
    MT_ASSERT(!strcmp(text, "probe: update (not taking)"));
    MT_ASSERT(!tele.port[0].update_due);

    sim_detach(0);
    tick_ms(60000); // idle for as long as it likes: not asked again
    MT_ASSERT_EQ(port_state(0), PORT_STATE_IDLE);
    MT_ASSERT_EQ(evt_count(EVT_UPDATE, 0), 3);
    MT_ASSERT_EQ(evt_count(EVT_PROBE_FAIL, 0), 1);
}

// A port with something plugged in is not taken down for its blade's
// firmware. The controller comes back from its own update carrying a newer
// blade image; the port that is charging keeps charging, shows the update
// as due, and gets it once it has sat empty for a while.
static void test_a_busy_port_waits_for_its_update(void) {
    support_reset(360000);
    seat3(0); // runs 0.0.1; nothing bundled yet
    settle(0, 100);
    sim_attach(0, 20000, 3000);
    tick(3);
    MT_ASSERT_EQ(port_state(0), PORT_STATE_ACTIVE);
    uint32_t en = sim_en_change_count(0);

    g_settings.blade_boot_via_loader = 1;
    fake_bundle_make(0, 2, 0, IMAGE_LEN);
    controller_reboot();
    tick_ms(60000);
    MT_ASSERT_EQ(port_state(0), PORT_STATE_ACTIVE);
    MT_ASSERT_EQ(sim_en_change_count(0), en);
    MT_ASSERT_EQ(budget_port_reservation(0), 60000);
    MT_ASSERT_EQ(sim_blade_flash_version(0), UPDATE_VERSION(0, 0, 1));
    MT_ASSERT(!sim_blade_boot_via_loader(0));
    MT_ASSERT_EQ(sim_blade_go_count(0), 0);
    MT_ASSERT_EQ(evt_count(EVT_UPDATE, 0), 0);
    MT_ASSERT(tele.port[0].update_due);

    // unplugged and plugged back in within the wait: still nothing
    sim_detach(0);
    tick_ms(9000);
    MT_ASSERT_EQ(port_state(0), PORT_STATE_IDLE);
    sim_attach(0, 20000, 3000);
    tick_ms(9000);
    MT_ASSERT_EQ(port_state(0), PORT_STATE_ACTIVE);
    MT_ASSERT_EQ(sim_blade_go_count(0), 0);

    // left empty: the option, the image, one trip
    sim_detach(0);
    tick_ms(9000);
    MT_ASSERT_EQ(port_state(0), PORT_STATE_IDLE);
    MT_ASSERT_EQ(sim_blade_go_count(0), 0);
    tick_ms(1100);
    settle(0, 400);
    MT_ASSERT_EQ(port_state(0), PORT_STATE_IDLE);
    MT_ASSERT(sim_blade_boot_via_loader(0));
    MT_ASSERT(flash_holds_bundle(0));
    MT_ASSERT_EQ(sim_blade_go_count(0), 1);
    MT_ASSERT_EQ(evt_count(EVT_PROBE_FAIL, 0), 0);
    MT_ASSERT(!tele.port[0].update_due);
}

// While the controller's own image is on trial it changes nothing about a
// blade's firmware: an update that gets reverted has touched no blade. A
// blade already in its bootloader is started on what it holds. Once the
// image commits, idle ports get what was waiting.
static void test_a_trial_image_leaves_the_blades_alone(void) {
    support_reset(360000);
    g_settings.blade_boot_via_loader = 1;
    fake_bundle_make(0, 2, 0, IMAGE_LEN);
    port_fsm_hold_updates(true);
    seat3(0);
    sim_set_gen(1, 3);
    sim_blade_set_boot_via_loader(1, true);
    sim_set_present(1, true); // in its bootloader, holding 0.0.1
    settle(0, 100);
    settle(1, 100);
    tick_ms(30000);
    MT_ASSERT_EQ(port_state(0), PORT_STATE_IDLE);
    MT_ASSERT_EQ(sim_blade_go_count(0), 0);
    MT_ASSERT(!sim_blade_boot_via_loader(0));
    MT_ASSERT_EQ(sim_blade_flash_version(0), UPDATE_VERSION(0, 0, 1));
    MT_ASSERT(!tele.port[0].update_due);
    MT_ASSERT_EQ(port_state(1), PORT_STATE_IDLE);
    MT_ASSERT_EQ(sim_blade_go_count(1), 1);
    MT_ASSERT_EQ(sim_blade_erase_count(1), 0);
    MT_ASSERT_EQ(sim_blade_flash_version(1), UPDATE_VERSION(0, 0, 1));

    port_fsm_hold_updates(false); // committed
    tick(2);
    settle(0, 400);
    settle(1, 400);
    MT_ASSERT_EQ(port_state(0), PORT_STATE_IDLE);
    MT_ASSERT_EQ(port_state(1), PORT_STATE_IDLE);
    MT_ASSERT(sim_blade_boot_via_loader(0));
    MT_ASSERT(flash_holds_bundle(0));
    MT_ASSERT(flash_holds_bundle(1));
    MT_ASSERT_EQ(evt_count(EVT_PROBE_FAIL, 0xFF), 0);
}

// A device that is switched off, its draw under the charged threshold for
// the charged time, gives its port up for the update as an empty port would:
// a few dark seconds, then the sink is served again. One that still draws
// above the threshold keeps its power.
static void test_a_switched_off_device_lets_its_blade_update(void) {
    support_reset(360000);
    g_settings.charged_mw = 500;
    g_settings.charged_min = 10;
    seat3(0); // runs 0.0.1; nothing bundled yet
    settle(0, 100);
    sim_set_load_pct(0, 20);
    sim_attach(0, 5000, 3000); // 3 W
    tick(3);
    MT_ASSERT_EQ(port_state(0), PORT_STATE_ACTIVE);

    fake_bundle_make(0, 2, 0, IMAGE_LEN);
    controller_reboot();
    tick_ms(11 * 60000);
    MT_ASSERT_EQ(port_state(0), PORT_STATE_ACTIVE); // drawing: it waits
    MT_ASSERT(!tele.port[0].charged);
    MT_ASSERT(tele.port[0].update_due);
    MT_ASSERT_EQ(sim_blade_go_count(0), 0);
    MT_ASSERT_EQ(tele.port[0].blade_fw, UPDATE_VERSION(0, 0, 1));

    sim_set_load_pct(0, 2); // switched off: 300 mW
    tick_ms(9 * 60000);
    MT_ASSERT_EQ(sim_blade_go_count(0), 0); // not under the threshold for long enough yet
    tick_ms(60000 + 1000);
    settle(0, 400);
    MT_ASSERT(flash_holds_bundle(0));
    MT_ASSERT_EQ(sim_blade_go_count(0), 1);
    MT_ASSERT_EQ(evt_count(EVT_PROBE_FAIL, 0), 0);
    tick(3);
    MT_ASSERT_EQ(port_state(0), PORT_STATE_ACTIVE); // and the sink is served again
    MT_ASSERT(!tele.port[0].update_due);
    MT_ASSERT_EQ(tele.port[0].blade_fw, UPDATE_VERSION(0, 2, 0));
    MT_ASSERT(!tele.port[0].charged); // a new attach: it counts as charged again only after the wait
}

// A blade that stops answering while its port is powered keeps the port
// powered: EN stays, the budget keeps its share, it is said once and shown.
// When it answers again nothing is redone. Told to update while deaf, the
// only word it can take is EN low: its watch takes it to the bootloader,
// and the probe finds it there.
static void test_a_silent_blade_keeps_its_power(void) {
    support_reset(360000);
    g_settings.blade_watch_s = 120;
    fake_bundle_make(0, 2, 0, IMAGE_LEN);
    seat3(0);
    settle(0, 100);
    sim_attach(0, 20000, 3000);
    tick(3);
    MT_ASSERT_EQ(port_state(0), PORT_STATE_ACTIVE);
    uint32_t en = sim_en_change_count(0);

    sim_set_blade_ok(0, false);
    tick_ms(30000);
    MT_ASSERT_EQ(port_state(0), PORT_STATE_ACTIVE);
    MT_ASSERT(sim_en(0));
    MT_ASSERT_EQ(sim_en_change_count(0), en);
    MT_ASSERT_EQ(sim_contract_mw(0), 60000);
    MT_ASSERT_EQ(budget_port_reservation(0), 60000);
    MT_ASSERT(tele.port[0].silent);
    MT_ASSERT_EQ(evt_count(EVT_FAULT, 0), 0);
    MT_ASSERT_EQ(evt_count(EVT_PROBE_FAIL, 0), 1);
    const engine_evt_t *e = evt_last(EVT_PROBE_FAIL, 0);
    MT_ASSERT_EQ(e->code, PROBE_FAIL_SILENT);
    fault_rec_t r = {.type = EVT_PROBE_FAIL, .code = e->code, .arg = e->arg};
    char text[48];
    fault_text(&r, text, sizeof text);
    MT_ASSERT(!strcmp(text, "probe: stopped answering"));

    uint32_t cfg = sim_blade_config_writes(0);
    sim_set_blade_ok(0, true);
    tick_ms(1100); // asked once a second while silent
    MT_ASSERT_EQ(port_state(0), PORT_STATE_ACTIVE);
    MT_ASSERT(!tele.port[0].silent);
    MT_ASSERT_EQ(sim_blade_config_writes(0), cfg); // what it holds is what it should
    MT_ASSERT_EQ(sim_en_change_count(0), en);

    sim_set_blade_ok(0, false);
    tick(12);
    MT_ASSERT(tele.port[0].silent);
    MT_ASSERT(!sim_blade_watch_expire(0)); // EN is high: its watch does nothing
    engine_cmd_t c = {.op = CMD_PORT_UPDATE, .port = 0};
    port_fsm_cmd(0, &c);
    tick(10);
    MT_ASSERT_EQ(port_state(0), PORT_STATE_FAULT);
    MT_ASSERT(!sim_en(0));
    MT_ASSERT(sim_blade_watch_expire(0)); // two minutes later, on the blade
    tick_ms(5100);
    settle(0, 400);
    MT_ASSERT_EQ(port_state(0), PORT_STATE_IDLE);
    MT_ASSERT(flash_holds_bundle(0));
    tick(2);
    MT_ASSERT_EQ(port_state(0), PORT_STATE_ACTIVE);
}

// Power lost in the middle of a write: the blade comes back blank in its
// bootloader, the controller writes it again, and the torn chunk never makes
// it into a started image.
static void test_power_cut_mid_write_is_recovered(void) {
    support_reset(360000);
    fake_bundle_make(0, 2, 0, IMAGE_LEN);
    sim_set_gen(0, 3);
    sim_blade_erase(0);
    sim_blade_cut_power_after_writes(0, 5);
    sim_set_present(0, true);
    tick_ms(20000); // the torn write is a failed trip; the next one, after the cooldown, starts clean
    MT_ASSERT_EQ(port_state(0), PORT_STATE_IDLE);
    MT_ASSERT(flash_holds_bundle(0));
    MT_ASSERT_EQ(sim_blade_erase_count(0), 2);
    MT_ASSERT_EQ(sim_blade_go_count(0), 1);
    const engine_evt_t *e = evt_last(EVT_PROBE_FAIL, 0);
    MT_ASSERT(e != NULL);
    MT_ASSERT_EQ(e->code, PROBE_FAIL_UPDATE);
    MT_ASSERT_EQ(e->arg, UPDATE_FAIL_WRITE);
}

// The controller reboots in the middle of a write: the blade sits in its
// bootloader, the next probe finds it there and starts over.
static void test_controller_reboot_mid_write_is_recovered(void) {
    support_reset(360000);
    fake_bundle_make(0, 2, 0, IMAGE_LEN);
    sim_set_gen(0, 3);
    sim_blade_erase(0);
    sim_set_present(0, true);
    tick(50); // past the erase, a page at a time, and into the write
    MT_ASSERT_EQ(port_state(0), PORT_STATE_UPDATE);
    uint32_t written = sim_blade_write_count(0);
    MT_ASSERT(written > 0 && written < (IMAGE_LEN + 255) / 256);

    // engine_main's start-up again, the sim as it stands (see test_warm.c)
    port_fsm_init();
    tca9539_attach();
    evt_clear();
    uint16_t inputs = 0;
    tca9539_read_inputs(&inputs);
    bool present[NUM_PORTS];
    for (uint8_t i = 0; i < NUM_PORTS; i++) present[i] = tca9539_present_from(inputs, i);
    port_fsm_boot_inventory(present, (uint8_t)(tca9539_outputs() & 0x3F), now_ms);

    settle(0, 400);
    MT_ASSERT_EQ(port_state(0), PORT_STATE_IDLE);
    MT_ASSERT(flash_holds_bundle(0));
    MT_ASSERT_EQ(sim_blade_go_count(0), 1);
}

// Firmware that keeps crashing after its start gets its allowance of trips
// and is then held in FAULT, out of the retry loop, until someone acts.
static void test_crash_loop_is_held(void) {
    support_reset(360000);
    const uint8_t *img = fake_bundle_make(0, 2, 0, IMAGE_LEN);
    sim_set_gen(0, 3);
    sim_blade_flash_image(0, img, IMAGE_LEN);
    sim_blade_set_boot_via_loader(0, true);
    sim_blade_crash_next(0, 10);
    sim_set_present(0, true);

    tick_ms(30000);
    MT_ASSERT_EQ(port_state(0), PORT_STATE_FAULT);
    MT_ASSERT_EQ(sim_blade_go_count(0), 3); // UPDATE_ROUNDS_MAX
    const engine_evt_t *e = evt_last(EVT_PROBE_FAIL, 0);
    MT_ASSERT(e != NULL);
    MT_ASSERT_EQ(e->code, PROBE_FAIL_UPDATE);
    MT_ASSERT_EQ(e->arg, UPDATE_FAIL_LOOP);
    fault_rec_t r = {.type = EVT_PROBE_FAIL, .code = e->code, .arg = e->arg};
    char text[48];
    fault_text(&r, text, sizeof text);
    MT_ASSERT(!strcmp(text, "probe: update (crash loop)"));
    int fails = evt_count(EVT_PROBE_FAIL, 0);
    tick_ms(20000);
    MT_ASSERT_EQ(port_state(0), PORT_STATE_FAULT); // held: no retry every cooldown
    MT_ASSERT_EQ(evt_count(EVT_PROBE_FAIL, 0), fails);
    MT_ASSERT_EQ(sim_blade_go_count(0), 3);

    // re-enabling gives it another allowance; by now the firmware has
    // crashed its last (7 more crashes: 3 + 3 rounds, the 7th start holds)
    engine_cmd_t c = {.op = CMD_PORT_ENABLE, .port = 0};
    port_fsm_cmd(0, &c);
    tick_ms(30000);
    MT_ASSERT_EQ(port_state(0), PORT_STATE_FAULT);
    MT_ASSERT_EQ(sim_blade_go_count(0), 6);
    port_fsm_cmd(0, &c);
    tick_ms(30000);
    MT_ASSERT_EQ(port_state(0), PORT_STATE_FAULT);
    MT_ASSERT_EQ(sim_blade_go_count(0), 9);
    port_fsm_cmd(0, &c);
    settle(0, 200);
    MT_ASSERT_EQ(port_state(0), PORT_STATE_IDLE); // the 11th start sticks
    MT_ASSERT_EQ(sim_blade_go_count(0), 11);

    // a reseat starts the count over as well
    sim_blade_crash_next(0, 10);
    sim_blade_restart(0);
    tick_ms(30000);
    MT_ASSERT_EQ(port_state(0), PORT_STATE_FAULT);
    sim_set_present(0, false);
    tick(2);
    sim_set_present(0, true);
    tick_ms(30000);
    MT_ASSERT_EQ(port_state(0), PORT_STATE_FAULT);
    MT_ASSERT_EQ(sim_blade_go_count(0), 17); // three more after the reseat
}

// A blank blade in a controller with nothing to give it is a fault that
// says so; the port is held after its allowance of tries.
static void test_blank_blade_without_a_bundle_is_a_fault(void) {
    support_reset(360000);
    sim_set_gen(0, 3);
    sim_blade_erase(0);
    sim_set_present(0, true);
    tick(5);
    MT_ASSERT_EQ(port_state(0), PORT_STATE_FAULT);
    const engine_evt_t *e = evt_last(EVT_PROBE_FAIL, 0);
    MT_ASSERT(e != NULL);
    MT_ASSERT_EQ(e->code, PROBE_FAIL_UPDATE);
    MT_ASSERT_EQ(e->arg, UPDATE_FAIL_NO_IMAGE);
    fault_rec_t r = {.type = EVT_PROBE_FAIL, .code = e->code, .arg = e->arg};
    char text[48];
    fault_text(&r, text, sizeof text);
    MT_ASSERT(!strcmp(text, "probe: update (no image)"));
    MT_ASSERT_EQ(sim_blade_go_count(0), 0); // never started something that is not there
    MT_ASSERT(!sim_blade_locked_up(0));
    tick_ms(60000);
    MT_ASSERT_EQ(port_state(0), PORT_STATE_FAULT);
    MT_ASSERT(evt_count(EVT_PROBE_FAIL, 0) <= 4); // three tries, then held

    // a bundle arriving with the next controller firmware programs it on the next go
    fake_bundle_make(0, 2, 0, IMAGE_LEN);
    engine_cmd_t c = {.op = CMD_PORT_ENABLE, .port = 0};
    port_fsm_cmd(0, &c);
    settle(0, 300);
    MT_ASSERT_EQ(port_state(0), PORT_STATE_IDLE);
    MT_ASSERT(flash_holds_bundle(0));
}

// The update command writes the bundle over whatever the blade runs, the
// bundled version included; without a bundle it does nothing.
static void test_update_command_rewrites(void) {
    support_reset(360000);
    seat3(0);
    settle(0, 100);
    engine_cmd_t c = {.op = CMD_PORT_UPDATE, .port = 0};
    port_fsm_cmd(0, &c);
    tick(5);
    MT_ASSERT_EQ(port_state(0), PORT_STATE_IDLE); // nothing to write: ignored
    MT_ASSERT(!sim_blade_in_loader(0));

    const uint8_t *img = fake_bundle_make(0, 2, 0, IMAGE_LEN);
    sim_blade_flash_image(0, img, IMAGE_LEN); // already on it
    tick(3);
    settle(0, 100);
    MT_ASSERT_EQ(port_state(0), PORT_STATE_IDLE);
    sim_attach(0, 20000, 3000);
    tick(3);
    MT_ASSERT_EQ(port_state(0), PORT_STATE_ACTIVE);
    port_fsm_cmd(0, &c);
    tick(2);
    MT_ASSERT_EQ(port_state(0), PORT_STATE_UPDATE);
    settle(0, 300);
    MT_ASSERT_EQ(port_state(0), PORT_STATE_IDLE);
    MT_ASSERT_EQ(sim_blade_erase_count(0), 1);
    MT_ASSERT(flash_holds_bundle(0));
    tick(2);
    MT_ASSERT_EQ(port_state(0), PORT_STATE_ACTIVE);
}

// The watch register goes out with the limits, and a gen-2 blade is never
// sent anywhere.
static void test_watch_setting_and_gen2(void) {
    support_reset(360000);
    g_settings.blade_watch_s = 120;
    g_settings.blade_boot_via_loader = 1;
    fake_bundle_make(0, 2, 0, IMAGE_LEN);
    seat3(0);
    sim_set_present(1, true); // gen 2
    settle(0, 300);
    settle(1, 100);
    MT_ASSERT_EQ(port_state(0), PORT_STATE_IDLE);
    MT_ASSERT_EQ(sim_blade_watch_s(0), 120);
    MT_ASSERT_EQ(port_state(1), PORT_STATE_IDLE);
    MT_ASSERT_EQ(tele.port[1].gen, 2);
    MT_ASSERT_EQ(evt_count(EVT_UPDATE, 1), 0);
}

void run_blade_update_tests(void) {
    mt_run("update: the CRC matches the STM32 unit", test_crc_matches_the_stm32_unit);
    mt_run("update: a blank blade is programmed", test_blank_blade_is_programmed);
    mt_run("update: a matching image is started", test_matching_image_is_started);
    mt_run("update: another version is rewritten", test_other_version_is_rewritten);
    mt_run("update: the boot option is programmed once", test_boot_option_is_programmed_once);
    mt_run("update: a stuck option leaves the port working", test_a_stuck_option_leaves_the_port_working);
    mt_run("update: a busy port waits for its update", test_a_busy_port_waits_for_its_update);
    mt_run("update: a trial image leaves the blades alone", test_a_trial_image_leaves_the_blades_alone);
    mt_run("update: a switched-off device lets its blade update", test_a_switched_off_device_lets_its_blade_update);
    mt_run("update: a silent blade keeps its power", test_a_silent_blade_keeps_its_power);
    mt_run("update: a power cut mid-write is recovered", test_power_cut_mid_write_is_recovered);
    mt_run("update: a controller reboot mid-write is recovered", test_controller_reboot_mid_write_is_recovered);
    mt_run("update: a crash loop is held", test_crash_loop_is_held);
    mt_run("update: a blank blade without a bundle is a fault", test_blank_blade_without_a_bundle_is_a_fault);
    mt_run("update: the update command rewrites", test_update_command_rewrites);
    mt_run("update: the watch setting, and gen 2 untouched", test_watch_setting_and_gen2);
}
