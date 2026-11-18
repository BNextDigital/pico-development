#pragma once

#include <cstddef>
#include <cstdint>

#include <pico/features.hpp>
#include <pico/types.hpp>

namespace pico::protocol {

inline constexpr std::size_t kStatusSize = 16;

enum class Command : std::uint8_t {
    Interact = 0x01,
    SetMode = 0x02,
    SetMood = 0x03,
    StartTimer = 0x04,
    CancelTimer = 0x05,
    RequestStatus = 0x06,
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

DecodeResult decode_command(const std::uint8_t *data, std::size_t size);
void encode_status(const State &state, Capabilities capabilities,
                   std::uint8_t (&output)[kStatusSize]);

} // namespace pico::protocol
