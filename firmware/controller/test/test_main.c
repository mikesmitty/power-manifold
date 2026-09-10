#include <stdio.h>

#include "microtest.h"

void run_boot_tests(void);
void run_budget_tests(void);
void run_event_tests(void);
void run_fan_tests(void);
void run_improv_tests(void);
void run_jsonlite_tests(void);
void run_led_sched_tests(void);
void run_led_tests(void);
void run_log_ring_tests(void);
void run_port_fsm_tests(void);
void run_priority_tests(void);
void run_settings_json_tests(void);
void run_sim_inject_tests(void);
void run_update_image_tests(void);
void run_ups_tests(void);
void run_vin_tests(void);
void run_w6100_tests(void);
void run_warm_tests(void);

int main(void) {
    printf("controller engine tests\n");
    run_boot_tests();
    run_budget_tests();
    run_event_tests();
    run_fan_tests();
    run_improv_tests();
    run_jsonlite_tests();
    run_led_sched_tests();
    run_led_tests();
    run_log_ring_tests();
    run_port_fsm_tests();
    run_priority_tests();
    run_settings_json_tests();
    run_sim_inject_tests();
    run_update_image_tests();
    run_ups_tests();
    run_vin_tests();
    run_w6100_tests();
    run_warm_tests();
    return mt_summary();
}
