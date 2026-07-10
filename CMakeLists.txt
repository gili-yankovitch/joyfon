cmake_minimum_required(VERSION 3.13)

# Pull in the Pico SDK (must be before project()).
# PICO_SDK_PATH defaults to ../pico-sdk relative to this project, but can be
# overridden via the PICO_SDK_PATH environment variable or -DPICO_SDK_PATH=...
if (NOT DEFINED ENV{PICO_SDK_PATH} AND NOT PICO_SDK_PATH)
    get_filename_component(PICO_SDK_PATH "${CMAKE_CURRENT_LIST_DIR}/../pico-sdk" ABSOLUTE)
endif ()

include(pico_sdk_import.cmake)

project(joyfon C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Initialize the SDK
pico_sdk_init()

add_executable(joyfon
        src/main.c
        src/usb_descriptors.c
        )

# Make sure TinyUSB can find tusb_config.h
target_include_directories(joyfon PRIVATE ${CMAKE_CURRENT_LIST_DIR})

# pico_stdlib for GPIO/clocks, tinyusb_device for the USB device stack.
target_link_libraries(joyfon PRIVATE
        pico_stdlib
        pico_unique_id
        tinyusb_device
        )

# RP2040 (original Pico) needs the USB enumeration errata fix (RP2040-E5).
# It uses GPIO15 internally; harmless on RP2350. Leave it on for compatibility.
if (PICO_PLATFORM STREQUAL "rp2040" OR NOT DEFINED PICO_PLATFORM)
    target_compile_definitions(joyfon PRIVATE PICO_RP2040_USB_DEVICE_ENUMERATION_FIX=1)
endif ()

# Create map/bin/hex/uf2 files
pico_add_extra_outputs(joyfon)
