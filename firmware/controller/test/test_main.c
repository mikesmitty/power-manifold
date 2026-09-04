#include <stdio.h>

#include "microtest.h"

void run_budget_tests(void);
void run_fan_tests(void);
void run_improv_tests(void);
void run_jsonlite_tests(void);
void run_led_tests(void);
void run_port_fsm_tests(void);
void run_priority_tests(void);
void run_update_image_tests(void);
void run_w6100_tests(void);

int main(void) {
    printf("controller engine tests\n");
    run_budget_tests();
    run_fan_tests();
    run_improv_tests();
    run_jsonlite_tests();
    run_led_tests();
    run_port_fsm_tests();
    run_priority_tests();
    run_update_image_tests();
    run_w6100_tests();
    return mt_summary();
}
