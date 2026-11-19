#pragma once

#include <cstddef>
#include <cstdint>

#include <pico/types.hpp>

namespace pico {

enum class Capability : std::uint16_t {
    None = 0,
    Display = 1U << 0,
    Speaker = 1U << 1,
    Microphone = 1U << 2,
    HomeDock = 1U << 3,
    CarDock = 1U << 4,
    Position = 1U << 5,
    Compass = 1U << 6,
    Motion = 1U << 7,
    Locator = 1U << 8,
};

using Capabilities = std::uint16_t;

constexpr Capabilities capability(Capability value)
{
    return static_cast<Capabilities>(value);
}

struct FeatureDescriptor {
    Mode mode;
    const char *name;
    Capabilities useful_capabilities;
    bool prototype_ready;
};

const FeatureDescriptor *features(std::size_t &count);
const FeatureDescriptor *feature_for(Mode mode);
Mode next_user_mode(Mode current);

} // namespace pico
