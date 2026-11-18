cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(pico_firmware)

target_include_directories(app PRIVATE
    lib/pico_core/include
    src
)

target_sources(app PRIVATE
    src/main.cpp
    src/platform/pico_motion.cpp
    lib/pico_core/src/motion.cpp
    src/platform/pico_microphone.cpp
    lib/pico_core/src/pitch.cpp
    src/platform/pico_audio.cpp
    lib/pico_core/src/audio.cpp
    src/platform/pico_ble.cpp
    src/platform/pico_display.cpp
    src/platform/pico_hardware.cpp
    src/platform/pico_storage.cpp
    lib/pico_core/src/core.cpp
    lib/pico_core/src/timer.cpp
    lib/pico_core/src/personality.cpp
    lib/pico_core/src/display_model.cpp
    lib/pico_core/src/features.cpp
    lib/pico_core/src/persistence.cpp
    lib/pico_core/src/protocol.cpp
)
