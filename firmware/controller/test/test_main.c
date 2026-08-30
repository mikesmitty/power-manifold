#include <stdio.h>

#include "microtest.h"

void run_budget_tests(void);
void run_port_fsm_tests(void);
void run_priority_tests(void);

int main(void) {
    printf("controller engine tests\n");
    run_budget_tests();
    run_port_fsm_tests();
    run_priority_tests();
    return mt_summary();
}
