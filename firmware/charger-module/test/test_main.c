#include <stdio.h>

#include "microtest.h"

void run_ovp_tests(void);
void run_pdo_tests(void);
void run_port_tests(void);
void run_power_tests(void);
void run_regmap_tests(void);
void run_sense_tests(void);
void run_supervisor_tests(void);
void run_tcpp02_tests(void);
void run_tps55288_tests(void);

int main(void) {
    printf("charger module tests\n");
    run_ovp_tests();
    run_pdo_tests();
    run_port_tests();
    run_power_tests();
    run_regmap_tests();
    run_sense_tests();
    run_supervisor_tests();
    run_tcpp02_tests();
    run_tps55288_tests();
    return mt_summary();
}
