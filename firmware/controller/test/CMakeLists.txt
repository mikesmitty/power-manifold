# Host-side engine tests: compiles the port state machine and budget arbiter
# against the simulated drivers (src/engine/sim/) and runs them natively.
# No pico-sdk needed — the engine core is deliberately hardware-free.
#
#   cmake -S test -B test/build && cmake --build test/build
#   ctest --test-dir test/build --output-on-failure

cmake_minimum_required(VERSION 3.13)

project(controller_engine_tests C)
set(CMAKE_C_STANDARD 11)

add_compile_options(-Wall -Wextra -g)

add_executable(engine_tests
    ../src/boot_reason.c
    ../src/button.c
    ../src/civil_time.c
    ../src/event_kind.c
    ../src/fault_text.c
    ../src/ip4_text.c
    ../src/led_sched.c
    ../src/log_ring.c
    ../src/settings_json.c
    ../src/settings_util.c
    ../src/engine/budget.c
    ../src/engine/fan_policy.c
    ../src/engine/led_pattern.c
    ../src/engine/port_fsm.c
    ../src/engine/sim/sim_blades.c
    ../src/engine/sim/sim_inject.c
    ../src/net/improv_proto.c
    ../src/net/jsonlite.c
    ../src/net/w6100.c
    ../src/ups/lad_proto.c
    ../src/ups/ups.c
    ../src/vin.c
    ../src/update_image.c
    support/test_support.c
    test_boot.c
    test_budget.c
    test_button.c
    test_events.c
    test_fan.c
    test_improv.c
    test_jsonlite.c
    test_led_sched.c
    test_leds.c
    test_log_ring.c
    test_port_fsm.c
    test_priority.c
    test_settings_json.c
    test_sim_inject.c
    test_update_image.c
    test_ups.c
    test_vin.c
    test_w6100.c
    test_warm.c
    test_main.c
)

target_include_directories(engine_tests PRIVATE
    ../src
    ../src/engine
    ../src/net
    ../src/ups
    support
)

enable_testing()
add_test(NAME engine_tests COMMAND engine_tests)
