cmake_minimum_required(VERSION 3.20.0)
if(NOT PICO_NRF_DIR OR NOT EXISTS "${PICO_NRF_DIR}/samples/bluetooth/fast_pair/locator_tag/src/main.c")
  message(FATAL_ERROR "Set PICO_NRF_DIR to the nRF Connect SDK 3.4.2 nrf directory; use build.py")
endif()
set(FHN_SAMPLE "${PICO_NRF_DIR}/samples/bluetooth/fast_pair/locator_tag")
set(ENV{PICO_FHN_SAMPLE_DIR} "${FHN_SAMPLE}")
set(CONF_FILE "${FHN_SAMPLE}/configuration/prj.conf;${CMAKE_CURRENT_SOURCE_DIR}/prj.conf")
set(DTC_OVERLAY_FILE "${CMAKE_CURRENT_SOURCE_DIR}/xiao.overlay")
if(PICO_FHN_AUDIO)
  list(APPEND CONF_FILE "${CMAKE_CURRENT_SOURCE_DIR}/audio.conf")
  list(APPEND DTC_OVERLAY_FILE "${CMAKE_CURRENT_SOURCE_DIR}/../../overlays/xiao_ble_audio.overlay")
endif()
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(pico_find_hub_probe LANGUAGES C CXX)
if(NOT CONFIG_BOARD_TARGET STREQUAL "xiao_ble/nrf52840/sense")
  message(FATAL_ERROR "This experiment is only mapped for xiao_ble/nrf52840/sense")
endif()
set(PICO_FW "${CMAKE_CURRENT_SOURCE_DIR}/../..")
target_include_directories(app PRIVATE "${FHN_SAMPLE}/include" "${FHN_SAMPLE}/src/ui"
  "${PICO_FW}/lib/pico_core/include" "${PICO_FW}/src")
# Reuse the version-pinned SDK protocol, state machine and DULT implementation;
# replace DK-only buttons, battery emulation and ADXL362 hardware adapters.
target_sources(app PRIVATE "${FHN_SAMPLE}/src/main.c" "${FHN_SAMPLE}/src/factory_reset.c"
  "${FHN_SAMPLE}/src/ring.c" "${FHN_SAMPLE}/src/ui/common.c" src/xiao.cpp
  "${PICO_FW}/src/platform/pico_display.cpp" "${PICO_FW}/src/platform/pico_audio.cpp"
  "${PICO_FW}/src/platform/pico_motion.cpp" "${PICO_FW}/src/platform/pico_status_led.cpp"
  "${PICO_FW}/lib/pico_core/src/display_model.cpp" "${PICO_FW}/lib/pico_core/src/clock.cpp"
  "${PICO_FW}/lib/pico_core/src/audio.cpp"
  "${PICO_FW}/lib/pico_core/src/pitch.cpp" "${PICO_FW}/lib/pico_core/src/motion.cpp")
zephyr_linker_sources(SECTIONS "${FHN_SAMPLE}/src/factory_reset_callbacks.ld")
zephyr_linker_sources(DATA_SECTIONS "${FHN_SAMPLE}/src/ui/request_listener.ld")
