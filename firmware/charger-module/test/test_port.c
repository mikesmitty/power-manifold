#include "blade.h"
#include "microtest.h"
#include "port.h"

static uint32_t fixed_rdo(unsigned pos, unsigned op_ma) {
    return ((uint32_t)pos << 28) | ((op_ma / 10) << 10) | (op_ma / 10);
}

static uint32_t pps_rdo(unsigned pos, unsigned mv, unsigned op_ma) {
    return ((uint32_t)pos << 28) | ((mv / 20) << 9) | (op_ma / 50);
}

static void nothing_to_offer_until_configured(void) {
    port_init();
    MT_ASSERT_EQ(port_table()->count, 0);
    MT_ASSERT(port_set_limits(5000, BLADE_MAX_MV_ALL));
    MT_ASSERT_EQ(port_table()->count, 7);
    MT_ASSERT_EQ(pdo_fixed_ma(port_table()->obj[4]), CABLE_DEFAULT_MA); // no e-marker seen
}

static void same_limits_change_nothing(void) {
    port_init();
    (void)port_set_limits(3000, 15000);
    MT_ASSERT(!port_set_limits(3000, 15000));
    MT_ASSERT(port_set_limits(2000, 15000));
    MT_ASSERT(port_set_limits(2000, 9000));
}

static void cable_raises_the_offer(void) {
    port_init();
    (void)port_set_limits(5000, BLADE_MAX_MV_ALL);
    port_attach();
    MT_ASSERT(port_set_cable_ma(5000));
    MT_ASSERT_EQ(pdo_fixed_ma(port_table()->obj[4]), 5000);

    regmap_live_t live = {0};
    port_report(&live);
    MT_ASSERT_EQ(live.status, BLADE_ST_ATTACHED | BLADE_ST_CABLE_5A);

    port_detach(); // the next cable starts from scratch
    MT_ASSERT_EQ(pdo_fixed_ma(port_table()->obj[4]), CABLE_DEFAULT_MA);
    MT_ASSERT(!port_set_cable_ma(CABLE_DEFAULT_MA));
}

static void cable_within_the_ceiling_changes_nothing(void) {
    port_init();
    (void)port_set_limits(2000, BLADE_MAX_MV_ALL);
    port_attach();
    MT_ASSERT(!port_set_cable_ma(5000)); // the controller's 2 A is what is offered either way
}

static void request_to_contract(void) {
    port_init();
    (void)port_set_limits(5000, BLADE_MAX_MV_ALL);
    MT_ASSERT(!port_request(fixed_rdo(1, 1000))); // nobody there
    port_attach();

    MT_ASSERT(port_request(fixed_rdo(4, 2500)));
    MT_ASSERT(port_pending() != NULL);
    MT_ASSERT_EQ(port_pending()->mv, 15000);
    MT_ASSERT(port_contract() == NULL); // not until the power is there

    regmap_live_t live = {0};
    port_report(&live);
    MT_ASSERT_EQ(live.status, BLADE_ST_ATTACHED);

    port_delivered();
    MT_ASSERT(port_pending() == NULL);
    MT_ASSERT_EQ(port_contract()->mv, 15000);
    port_report(&live);
    MT_ASSERT_EQ(live.status, BLADE_ST_ATTACHED | BLADE_ST_CONTRACT);
    MT_ASSERT_EQ(live.pdo, 4);
    MT_ASSERT_EQ(live.contract_mv, 15000);
    MT_ASSERT_EQ(live.contract_ma, 2500);
}

static void rejected_request_keeps_the_contract(void) {
    port_init();
    (void)port_set_limits(3000, BLADE_MAX_MV_ALL);
    port_attach();
    (void)port_request(fixed_rdo(2, 2000));
    port_delivered();
    MT_ASSERT(!port_request(fixed_rdo(2, 4000)));
    MT_ASSERT(port_pending() == NULL);
    MT_ASSERT_EQ(port_contract()->op_ma, 2000);
}

static void pps_contract_is_reported(void) {
    port_init();
    (void)port_set_limits(3000, BLADE_MAX_MV_ALL);
    port_attach();
    MT_ASSERT(port_request(pps_rdo(7, 12340, 1500)));
    port_delivered();
    regmap_live_t live = {.status = BLADE_ST_EN | BLADE_ST_VBUS_ON};
    port_report(&live);
    // the bits that are not the port's stay as they were
    MT_ASSERT_EQ(live.status, BLADE_ST_EN | BLADE_ST_VBUS_ON | BLADE_ST_ATTACHED |
                              BLADE_ST_CONTRACT | BLADE_ST_PPS);
    MT_ASSERT_EQ(live.contract_mv, 12340);
}

static void hard_reset_and_detach(void) {
    port_init();
    (void)port_set_limits(3000, BLADE_MAX_MV_ALL);
    port_attach();
    (void)port_request(fixed_rdo(5, 3000));
    port_delivered();

    port_contract_lost();
    MT_ASSERT(port_contract() == NULL);
    MT_ASSERT(port_attached());

    (void)port_request(fixed_rdo(5, 3000));
    port_delivered();
    port_detach();
    MT_ASSERT(port_contract() == NULL);
    regmap_live_t live = {.status = BLADE_ST_EN, .pdo = 5, .contract_mv = 20000};
    port_report(&live);
    MT_ASSERT_EQ(live.status, BLADE_ST_EN);
    MT_ASSERT_EQ(live.pdo, 0);
    MT_ASSERT_EQ(live.contract_mv, 0);
}

void run_port_tests(void) {
    mt_run("port: nothing to offer until configured", nothing_to_offer_until_configured);
    mt_run("port: the same limits change nothing", same_limits_change_nothing);
    mt_run("port: a 5 A cable raises the offer, for that cable", cable_raises_the_offer);
    mt_run("port: a cable within the ceiling changes nothing", cable_within_the_ceiling_changes_nothing);
    mt_run("port: request to contract", request_to_contract);
    mt_run("port: a rejected request keeps the contract", rejected_request_keeps_the_contract);
    mt_run("port: a PPS contract is reported", pps_contract_is_reported);
    mt_run("port: hard reset and detach", hard_reset_and_detach);
}
