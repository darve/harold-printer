cmake_minimum_required(VERSION 3.13)

set(PICO_BOARD pico2 CACHE STRING "Board type")

# Pico-PIO-USB is vendored as a submodule, pinned to 0.7.2: the first release
# with the RP2350-E9 errata workaround (without it the host can't detect a
# device being unplugged). Must be set before pico_sdk_init() so the SDK's
# TinyUSB integration creates the tinyusb_pico_pio_usb target.
set(PICO_PIO_USB_PATH ${CMAKE_CURRENT_LIST_DIR}/lib/Pico-PIO-USB)

include(pico_sdk_import.cmake)

project(harold_printer C CXX ASM)
set(CMAKE_C_STANDARD 11)

pico_sdk_init()

if (NOT EXISTS ${PICO_PIO_USB_PATH}/src/pio_usb.c)
  message(FATAL_ERROR "Pico-PIO-USB not found. Run: git submodule update --init")
endif()

add_executable(harold_printer
  src/main.c
  src/log.c
  src/usb_descriptors.c
  src/usb_probe.c
)

target_include_directories(harold_printer PRIVATE src)

target_link_libraries(harold_printer PRIVATE
  pico_stdlib
  pico_unique_id
  tinyusb_device
  tinyusb_host
  tinyusb_pico_pio_usb
)

pico_add_extra_outputs(harold_printer)
