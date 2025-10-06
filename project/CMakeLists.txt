# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.2.0)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.2.0)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico_w CACHE STRING "Board type")

cmake_minimum_required(VERSION 3.12)
include(pico_sdk_import.cmake)
project(imu_features C CXX ASM)

pico_sdk_init()

# Only compile the firmware sources here; the lab algorithm demos have their
# own standalone `main` functions and would conflict at link time.
file(GLOB PROJECT_SOURCES
    ${CMAKE_CURRENT_LIST_DIR}/src/*.c
    ${CMAKE_CURRENT_LIST_DIR}/src/*.cpp
    ${CMAKE_CURRENT_LIST_DIR}/external/icm20948/*.c
)

set(SD_CARD_EXAMPLE_DIR ${CMAKE_CURRENT_LIST_DIR}/../sd_card_example)
add_subdirectory(${SD_CARD_EXAMPLE_DIR}/sd_card_driver ${CMAKE_CURRENT_BINARY_DIR}/sd_card_driver)

add_library(sd_card_driver INTERFACE)
target_link_libraries(sd_card_driver INTERFACE sd_custom_driver)

add_executable(imu_features
    ${PROJECT_SOURCES}
    ${SD_CARD_EXAMPLE_DIR}/config/hw_config.c
)

target_include_directories(imu_features PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/include
    ${CMAKE_CURRENT_LIST_DIR}/src
    ${CMAKE_CURRENT_LIST_DIR}/lab_algorithms
    ${CMAKE_CURRENT_LIST_DIR}/external/icm20948
    ${SD_CARD_EXAMPLE_DIR}/sd_card_driver/include
    ${SD_CARD_EXAMPLE_DIR}/config
)

target_link_libraries(imu_features
    pico_stdlib
    hardware_i2c
    sd_card_driver
)

pico_enable_stdio_usb(imu_features 1)
pico_enable_stdio_uart(imu_features 0)

pico_add_extra_outputs(imu_features)
