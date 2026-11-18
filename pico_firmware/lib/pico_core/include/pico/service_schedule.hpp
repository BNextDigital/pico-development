#pragma once
#include <algorithm>
#include <cstdint>

namespace pico::schedule {
inline constexpr std::uint32_t kNoDeadline = UINT32_MAX;

// Absolute 64-bit uptime keeps second boundaries correct across 32-bit ms wrap.
// Relative service delays are queried at this same time, after service work.
inline std::uint32_t wait_ms(std::uint64_t now, std::uint32_t last_tick,
                            std::uint32_t animation, std::uint32_t music,
                            std::uint32_t motion, bool tuner_active)
{
    if (static_cast<std::uint32_t>(now / 1000U) != last_tick) return 0;
    return std::min({static_cast<std::uint32_t>(1000U - now % 1000U),
                     animation, music, motion, tuner_active ? 20U : kNoDeadline});
}
} // namespace pico::schedule
