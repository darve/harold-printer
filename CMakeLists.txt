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

# Photos in photos/ are converted to a C table at build time.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
file(GLOB PHOTO_FILES CONFIGURE_DEPENDS ${CMAKE_CURRENT_LIST_DIR}/photos/*)
list(FILTER PHOTO_FILES EXCLUDE REGEX "README\\.md$")
set(PHOTOS_C ${CMAKE_CURRENT_BINARY_DIR}/generated/photos_data.c)
add_custom_command(
  OUTPUT ${PHOTOS_C}
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_LIST_DIR}/tools/convert_photos.py
          ${CMAKE_CURRENT_LIST_DIR}/photos --out ${PHOTOS_C}
  DEPENDS ${CMAKE_CURRENT_LIST_DIR}/tools/convert_photos.py ${PHOTO_FILES}
  COMMENT "Converting photos"
  VERBATIM
)

add_executable(harold_printer
  src/main.c
  src/button.c
  src/escpos.c
  src/log.c
  src/photos.c
  src/printer.c
  src/printer_usb.c
  src/shell.c
  src/test_page.c
  src/transport.c
  src/tspl.c
  src/usb_descriptors.c
  src/usb_probe.c
  ${PHOTOS_C}
)

target_include_directories(harold_printer PRIVATE src)

target_link_libraries(harold_printer PRIVATE
  pico_stdlib
  pico_unique_id
  pico_rand
  tinyusb_device
  tinyusb_host
  tinyusb_pico_pio_usb
)

pico_add_extra_outputs(harold_printer)
