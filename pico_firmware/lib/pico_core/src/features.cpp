#include <pico/features.hpp>

#include <array>

namespace pico {
namespace {

constexpr Capabilities operator|(Capability lhs, Capability rhs)
{
    return capability(lhs) | capability(rhs);
}

constexpr std::array<FeatureDescriptor, 7> kFeatures{{
    {Mode::Companion, "companion", capability(Capability::Display), true},
    {Mode::Focus, "focus", Capability::Display | Capability::Speaker, true},
    {Mode::Music, "music", Capability::Microphone | Capability::Speaker, false},
    {Mode::Cooking, "cooking", Capability::Display | Capability::Speaker, false},
    {Mode::Wayfinding, "wayfinding", Capability::Position | Capability::Compass, false},
    {Mode::Notes, "notes", capability(Capability::Microphone), false},
    {Mode::Driving, "driving", capability(Capability::CarDock), false},
}};

} // namespace

const FeatureDescriptor *features(std::size_t &count)
{
    count = kFeatures.size();
    return kFeatures.data();
}

const FeatureDescriptor *feature_for(Mode mode)
{
    const auto index = static_cast<std::size_t>(mode);
    return index < kFeatures.size() ? &kFeatures[index] : nullptr;
}

Mode next_user_mode(Mode current)
{
    // Driving is dock-selected; it is intentionally absent from manual cycling.
    constexpr std::array<Mode, 6> user_modes{
        Mode::Companion, Mode::Focus, Mode::Music,
        Mode::Cooking, Mode::Wayfinding, Mode::Notes,
    };

    for (std::size_t index = 0; index < user_modes.size(); ++index) {
        if (user_modes[index] == current) {
            return user_modes[(index + 1U) % user_modes.size()];
        }
    }

    return Mode::Companion;
}

} // namespace pico
