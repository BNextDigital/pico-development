#pragma once
#include <pico/types.hpp>
namespace pico::display {
inline bool auto_sleep_allowed(const State &s)
{
    return !s.display_asleep && s.display_timeout_minutes && !s.timer_completed &&
        !s.tuner_running && !s.metronome_running && !s.reference_playing && s.screen != Screen::SettingEdit &&
        s.screen != Screen::DurationEdit;
}
inline unsigned brightness_percent(const State &state)
{
    return state.screen == Screen::SettingEdit && state.setting_key == SettingKey::Brightness
        ? state.setting_draft : state.display_brightness_percent;
}
inline std::uint8_t contrast(const State &state)
{ return static_cast<std::uint8_t>((brightness_percent(state) * 255U + 50U) / 100U); }
} // namespace pico::display
