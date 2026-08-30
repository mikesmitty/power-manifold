#include "budget.h"
#include "microtest.h"

static void test_reserve_release_headroom(void) {
    budget_init(100000);
    MT_ASSERT_EQ(budget_total(), 100000);
    MT_ASSERT_EQ(budget_headroom(), 100000);

    MT_ASSERT(budget_try_reserve(0, 60000));
    MT_ASSERT_EQ(budget_reserved(), 60000);
    MT_ASSERT_EQ(budget_headroom(), 40000);
    MT_ASSERT_EQ(budget_port_reservation(0), 60000);

    MT_ASSERT(!budget_try_reserve(1, 50000)); // would exceed
    MT_ASSERT_EQ(budget_port_reservation(1), 0); // unchanged on failure

    budget_release(0);
    MT_ASSERT_EQ(budget_headroom(), 100000);
}

static void test_try_reserve_replaces_own(void) {
    budget_init(100000);
    MT_ASSERT(budget_try_reserve(0, 90000));
    // shrinking or regrowing your own reservation only counts the others
    MT_ASSERT(budget_try_reserve(0, 95000));
    MT_ASSERT_EQ(budget_port_reservation(0), 95000);
    MT_ASSERT(!budget_try_reserve(0, 100001));
    MT_ASSERT_EQ(budget_port_reservation(0), 95000);
}

static void test_force_reserve_overrides(void) {
    budget_init(50000);
    budget_force_reserve(0, 40000);
    budget_force_reserve(1, 40000); // force may oversubscribe by design
    MT_ASSERT_EQ(budget_reserved(), 80000);
    MT_ASSERT_EQ(budget_headroom(), 0);
    budget_set_total(120000);
    MT_ASSERT_EQ(budget_headroom(), 40000);
}

void run_budget_tests(void) {
    mt_run("budget: reserve/release/headroom", test_reserve_release_headroom);
    mt_run("budget: try_reserve replaces own", test_try_reserve_replaces_own);
    mt_run("budget: force_reserve overrides", test_force_reserve_overrides);
}
