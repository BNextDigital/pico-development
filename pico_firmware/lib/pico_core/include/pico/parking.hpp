#pragma once

#include <cstdint>

namespace pico::parking {

inline constexpr std::uint32_t kTimeoutSeconds = 45;
inline constexpr std::uint32_t kSequenceMask = 0x00ffffffU;

enum class Phase : std::uint8_t {
    Idle, Saving, Saved, OpenApp, NoLocation, Failed, Cancelled, TimedOut, Interrupted, Permission,
};

constexpr bool app_result(Phase phase)
{
    return (phase >= Phase::Saved && phase <= Phase::Cancelled) || phase == Phase::Permission;
}

struct State {
    std::uint32_t boot_id{1};
    std::uint32_t sequence{};
    Phase phase{Phase::Idle};
    bool reaction{};
};

} // namespace pico::parking
