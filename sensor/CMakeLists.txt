cmake_minimum_required(VERSION 3.13)

include(pico_sdk_import.cmake)

project(sensor_project C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
pico_sdk_init()

add_executable(sensor
picow_http_client.c
)

target_compile_definitions(sensor PRIVATE
WIFI_SSID=\"${WIFI_SSID}\"
WIFI_PASSWORD=\"${WIFI_PASSWORD}\"
HTTP_SERVER_IP=\"${HTTP_SERVER_IP}\"
)

pico_enable_stdio_usb(sensor 1)
pico_enable_stdio_uart(sensor 1)

pico_add_extra_outputs(sensor)

target_include_directories(sensor PRIVATE ${CMAKE_CURRENT_LIST_DIR} )

target_link_libraries(sensor
    pico_cyw43_arch_lwip_threadsafe_background
    pico_stdlib
    hardware_adc
)