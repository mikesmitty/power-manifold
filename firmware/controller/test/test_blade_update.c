#include <string.h>

#include "blade.h"
#include "blade_bundle.h"
#include "blade_regs.h"
#include "blade_update.h"
#include "fake_bundle.h"
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

// A running blade on another version is sent to its bootloader and rewritten
// when the setting says so, and left alone when it does not.
static void test_other_version_follows_the_setting(void) {
    support_reset(360000);
    fake_bundle_make(0, 2, 0, IMAGE_LEN);
    seat3(0); // the sim's fresh blade runs 0.0.1
    settle(0, 100);
    MT_ASSERT_EQ(port_state(0), PORT_STATE_IDLE);
    MT_ASSERT_EQ(sim_blade_flash_version(0), UPDATE_VERSION(0, 0, 1)); // auto-update off: as it was
    MT_ASSERT_EQ(sim_blade_erase_count(0), 0);

    support_reset(360000);
    g_settings.blade_auto_update = 1;
    fake_bundle_make(0, 2, 0, IMAGE_LEN);
    seat3(0);
    sim_attach(0, 9000, 2000); // even with a sink on: the chassis keeps its blades on its version
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

    // with a bundle as well: rewritten and set, in whichever order, within the allowance
    support_reset(360000);
    g_settings.blade_boot_via_loader = 1;
    g_settings.blade_auto_update = 1;
    fake_bundle_make(0, 2, 0, IMAGE_LEN);
    seat3(0);
    settle(0, 300);
    MT_ASSERT_EQ(port_state(0), PORT_STATE_IDLE);
    MT_ASSERT(sim_blade_boot_via_loader(0));
    MT_ASSERT_EQ(sim_blade_flash_version(0), UPDATE_VERSION(0, 2, 0));
    MT_ASSERT_EQ(evt_count(EVT_PROBE_FAIL, 0), 0);
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
    tick(14);
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
    g_settings.blade_auto_update = 1;
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
    mt_run("update: another version follows the setting", test_other_version_follows_the_setting);
    mt_run("update: the boot option is programmed once", test_boot_option_is_programmed_once);
    mt_run("update: a power cut mid-write is recovered", test_power_cut_mid_write_is_recovered);
    mt_run("update: a controller reboot mid-write is recovered", test_controller_reboot_mid_write_is_recovered);
    mt_run("update: a crash loop is held", test_crash_loop_is_held);
    mt_run("update: a blank blade without a bundle is a fault", test_blank_blade_without_a_bundle_is_a_fault);
    mt_run("update: the update command rewrites", test_update_command_rewrites);
    mt_run("update: the watch setting, and gen 2 untouched", test_watch_setting_and_gen2);
}
