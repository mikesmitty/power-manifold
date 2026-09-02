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
    ../src/engine/budget.c
    ../src/engine/fan_policy.c
    ../src/engine/port_fsm.c
    ../src/engine/sim/sim_blades.c
    ../src/update_image.c
    support/test_support.c
    test_budget.c
    test_fan.c
    test_port_fsm.c
    test_priority.c
    test_update_image.c
    test_main.c
)

target_include_directories(engine_tests PRIVATE
    ../src
    ../src/engine
    support
)

enable_testing()
add_test(NAME engine_tests COMMAND engine_tests)
