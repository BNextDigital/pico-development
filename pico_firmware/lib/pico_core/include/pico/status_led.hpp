#pragma once
#include <cstdint>
namespace pico::status_led {
inline constexpr std::uint32_t kOnMs = 200;
inline constexpr std::uint32_t kOffMs = 5000;
inline bool on(std::uint64_t elapsed_ms) { return elapsed_ms % (kOnMs+kOffMs) < kOnMs; }
}
