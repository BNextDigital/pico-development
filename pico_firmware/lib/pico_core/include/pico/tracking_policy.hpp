#pragma once
#include <cstdint>
namespace pico::tracking {
// Identity 0 remains Pico's companion; identity 1 is reserved for Fast Pair.
constexpr bool companion_identity(std::uint8_t id) { return id == 0; }
constexpr bool companion_advertising(unsigned companions, unsigned total,
                                      unsigned capacity, unsigned network_reserved)
{
    return companions == 0 && total < capacity && capacity - total > network_reserved;
}
// Contract for the subsequent live activation milestone. No second timer service.
enum class Alert : std::uint8_t { None, LocalFind, OwnerRing, Timer, UnwantedTracking };
constexpr Alert alert(bool local, bool owner, bool timer, bool unwanted, bool alarm=false)
{
    return unwanted ? Alert::UnwantedTracking : (timer || alarm) ? Alert::Timer :
        owner ? Alert::OwnerRing : local ? Alert::LocalFind : Alert::None;
}
constexpr bool capture_microphone(bool requested, Alert priority)
{
    return requested && priority < Alert::OwnerRing;
}
constexpr std::uint8_t motion_mask(std::uint8_t user, std::uint8_t required)
{
    return user | required;
}
}
