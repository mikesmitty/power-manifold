cmake_minimum_required(VERSION 3.13)

set(PICO_BOARD pico2_w CACHE STRING "Board type")

include(pico_sdk_import.cmake)

project(power_manifold_controller C CXX ASM)
set(CMAKE_C_STANDARD 11)

pico_sdk_init()

add_executable(controller
    src/main.c
    src/ipc.c
    src/settings.c
    src/cli.c
    src/engine/engine.c
    src/engine/port_fsm.c
    src/engine/budget.c
    src/engine/mpq4242.c
    src/engine/ina226.c
    src/engine/tca9548a.c
    src/engine/tca9539.c
    src/engine/leds.c
    src/net/net.c
    src/net/mqtt.c
    src/net/http.c
)

pico_generate_pio_header(controller ${CMAKE_CURRENT_LIST_DIR}/src/engine/ws2812.pio)

target_include_directories(controller PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/src
    ${CMAKE_CURRENT_LIST_DIR} # lwipopts.h
)

target_compile_definitions(controller PRIVATE
    PICO_STACK_SIZE=4096
    PICO_CORE1_STACK_SIZE=4096
)

target_link_libraries(controller
    pico_stdlib
    pico_multicore
    pico_flash
    pico_unique_id
    hardware_i2c
    hardware_pio
    hardware_flash
    hardware_watchdog
    pico_cyw43_arch_lwip_threadsafe_background
    pico_lwip_mqtt
    pico_lwip_mdns
    pico_lwip_sntp
)

pico_enable_stdio_usb(controller 1)
pico_enable_stdio_uart(controller 0)

pico_add_extra_outputs(controller)
