#include "board.h"
#include "fake_bus.h"
#include "microtest.h"
#include "tcpp02.h"

// Control words against the ones UM2973 (X-NUCLEO-SRC1M1) lists, and the
// acknowledge check.

static void mode_words_match_st(void) {
    tcpp02_ctrl_t c = {0};
    c.mode = TCPP02_HIBERNATE;
    MT_ASSERT_EQ(tcpp02_ctrl_byte(&c), 0x08);
    c.mode = TCPP02_LOW_POWER;
    MT_ASSERT_EQ(tcpp02_ctrl_byte(&c), 0x28);
    c.mode = TCPP02_NORMAL;
    MT_ASSERT_EQ(tcpp02_ctrl_byte(&c), 0x18);
}

static void switches_and_discharge(void) {
    tcpp02_ctrl_t c = {.mode = TCPP02_NORMAL, .vbus_on = true};
    MT_ASSERT_EQ(tcpp02_ctrl_byte(&c), 0x1C);
    c.vconn = TCPP02_VCONN_CC1;
    MT_ASSERT_EQ(tcpp02_ctrl_byte(&c), 0x1D);
    c.vconn = TCPP02_VCONN_CC2;
    MT_ASSERT_EQ(tcpp02_ctrl_byte(&c), 0x1E); // never both
    c = (tcpp02_ctrl_t){.mode = TCPP02_NORMAL, .vbus_discharge = true, .vconn_discharge = true};
    MT_ASSERT_EQ(tcpp02_ctrl_byte(&c), 0xD8);
}

static void set_checks_the_acknowledge(void) {
    fake_bus_reset();
    tcpp02_ctrl_t c = {.mode = TCPP02_NORMAL, .vbus_on = true};
    MT_ASSERT(tcpp02_set(&c));
    MT_ASSERT_EQ(fake_bus_peek(ADDR_TCPP02, TCPP02_REG_CTRL), 0x1C);
    fake_bus_stick_tcpp02_ack(true);
    c.vbus_on = false;
    MT_ASSERT(!tcpp02_set(&c)); // written, not taken
}

static void probe_wants_a_tcpp02(void) {
    fake_bus_reset();
    MT_ASSERT(tcpp02_probe());
    fake_bus_poke(ADDR_TCPP02, TCPP02_REG_FLAGS, 0x00); // a TCPP03 reads 0 in bit 7
    MT_ASSERT(!tcpp02_probe());
    fake_bus_set_present(ADDR_TCPP02, false);
    MT_ASSERT(!tcpp02_probe());
}

void run_tcpp02_tests(void) {
    mt_run("tcpp02: power mode words match ST's", mode_words_match_st);
    mt_run("tcpp02: gate, VCONN and discharge bits", switches_and_discharge);
    mt_run("tcpp02: set checks the acknowledge register", set_checks_the_acknowledge);
    mt_run("tcpp02: probe wants a TCPP02", probe_wants_a_tcpp02);
}
