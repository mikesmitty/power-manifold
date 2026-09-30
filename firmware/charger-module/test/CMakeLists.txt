# Host-side tests: everything in src/ that is not src/hw/ is hardware-free
# and runs natively, against a stand-in for the private I2C bus. No ARM
# toolchain needed.
#
#   cmake -S test -B test/build && cmake --build test/build
#   ctest --test-dir test/build --output-on-failure

cmake_minimum_required(VERSION 3.13)

project(charger_module_tests C)
set(CMAKE_C_STANDARD 11)

include(${CMAKE_CURRENT_LIST_DIR}/../cmake/version.cmake)

add_compile_options(-Wall -Wextra -g)

add_executable(blade_tests
    ../src/ovp.c
    ../src/pdo.c
    ../src/port.c
    ../src/power.c
    ../src/regmap.c
    ../src/sense.c
    ../src/supervisor.c
    ../src/tcpp02.c
    ../src/tps55288.c
    support/fake_board.c
    support/fake_bus.c
    support/fake_stack.c
    support/test_support.c
    test_ovp.c
    test_pdo.c
    test_port.c
    test_power.c
    test_regmap.c
    test_sense.c
    test_supervisor.c
    test_tcpp02.c
    test_tps55288.c
    test_main.c
)

target_include_directories(blade_tests PRIVATE
    ../include
    ../src
    ../src/hw
    support
)

target_compile_definitions(blade_tests PRIVATE ${FW_VERSION_DEFINITIONS})
target_link_libraries(blade_tests m)

enable_testing()
add_test(NAME blade_tests COMMAND blade_tests)
