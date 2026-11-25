#pragma once

#include <cstddef>
#include <cstdint>

#include <pico/features.hpp>
#include <pico/types.hpp>

namespace pico::protocol {

inline constexpr std::size_t kStatusSize = 16;
inline constexpr std::size_t kLocatorStatusSize = 4;
inline constexpr std::size_t kClockStatusSize = 20;
inline constexpr std::size_t kParkingStatusSize = 9;

enum class Command : std::uint8_t {
    Interact = 0x01,
    SetMode = 0x02,
    SetMood = 0x03,
    StartTimer = 0x04,
    CancelTimer = 0x05,
    RequestStatus = 0x06,
    SetTheme = 0x07,
    PauseTimer = 0x08,
    ResumeTimer = 0x09,
    SetTimerDuration = 0x0a,
    OpenMenu = 0x10,
    NavigateNext = 0x11,
    NavigatePrevious = 0x12,
    Select = 0x13,
    Back = 0x14,
    OpenSettings = 0x15,
    StartFocusCycle = 0x20,
    NextFocusPhase = 0x21,
    SetBreakDuration = 0x22,
    TestSound = 0x23,
    SetMuted = 0x24,
    SetVolume = 0x25,
    StartMetronome = 0x30,
    StopMetronome = 0x31,
    SetMetronomeBpm = 0x32,
    SetMetronomeBeats = 0x33,
    TapTempo = 0x34,
    StartTuner = 0x35,
    StopTuner = 0x36,
    SetMotionConfig = 0x40,
    StartFinding = 0x50,
    StopFinding = 0x51,
    SyncTime = 0x60,
    SetClockOptions = 0x61,
    ConfigureAlarm = 0x62,
    DismissAlarm = 0x63,
    StopwatchStart = 0x64,
    StopwatchPause = 0x65,
    StopwatchResume = 0x66,
    StopwatchReset = 0x67,
    OpenClock = 0x68,
    ParkingReady = 0x70,
    ParkingResult = 0x71,
};

enum class DecodeStatus : std::uint8_t {
    Ok = 0,
    Empty,
    UnknownCommand,
    WrongLength,
    InvalidValue,
};

struct DecodeResult {
    DecodeStatus status{DecodeStatus::Empty};
    Event event{};
};

void encode_parking(const State &state, std::uint8_t (&output)[kParkingStatusSize]);
void encode_clock(const State &state,std::uint8_t (&output)[kClockStatusSize]);
void encode_locator(const State &state, Capabilities capabilities, std::uint8_t (&output)[kLocatorStatusSize]);
DecodeResult decode_command(const std::uint8_t *data, std::size_t size);
void encode_status(const State &state, Capabilities capabilities,
                   std::uint8_t (&output)[kStatusSize]);

} // namespace pico::protocol
