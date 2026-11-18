#include <pico/core.hpp>

namespace pico {

Effects Core::open_duration_editor(bool break_duration)
{
    state_.duration_return_screen = state_.screen == Screen::Settings ? Screen::Settings : Screen::Utility;
    state_.editing_break_duration = break_duration;
    const auto seconds = break_duration ? state_.break_duration_seconds : state_.configured_duration_seconds;
    // Existing BLE/legacy second-resolution settings remain untouched until Save.
    state_.draft_duration_seconds = ((seconds+59U)/60U)*60U;
    state_.duration_action = 1;
    state_.screen = Screen::DurationEdit;
    return effect(Effect::Render);
}
Effects Core::edit_duration(EventKind kind)
{
    if (kind == EventKind::Back || kind == EventKind::OpenMenu) {
        state_.screen = kind == EventKind::Back ? state_.duration_return_screen : Screen::Menu;
    } else if (kind == EventKind::NavigateNext || kind == EventKind::NavigatePrevious) {
        state_.duration_action = static_cast<std::uint8_t>((state_.duration_action +
            (kind == EventKind::NavigateNext ? 1 : 3)) % 4);
    } else if (kind == EventKind::Select) {
        switch (state_.duration_action) {
        case 0:
            if (state_.draft_duration_seconds > 60U) state_.draft_duration_seconds -= 60U;
            break;
        case 1:
            if (state_.draft_duration_seconds < kMaxTimerSeconds) state_.draft_duration_seconds += 60U;
            break;
        case 2: {
            const auto event = state_.editing_break_duration ? EventKind::SetBreakDuration : EventKind::SetTimerDuration;
            state_.screen = state_.duration_return_screen;
            return handle({event,state_.draft_duration_seconds}) | Effect::Render;
        }
        case 3: state_.screen = state_.duration_return_screen; break;
        }
    }
    return effect(Effect::Render);
}
Effects Core::open_setting(SettingKey key)
{
    state_.setting_key = key;
    state_.setting_draft = key == SettingKey::Theme ? static_cast<unsigned>(state_.theme) :
        key == SettingKey::Mute ? static_cast<unsigned>(state_.sound_muted) :
        key == SettingKey::Volume ? state_.sound_volume_percent :
        key == SettingKey::Brightness ? state_.display_brightness_percent :
        key == SettingKey::DisplayTimeout ? state_.display_timeout_minutes : state_.motion_config;
    state_.setting_action = 1;
    state_.screen = Screen::SettingEdit;
    return effect(Effect::Render);
}
Effects Core::edit_setting(EventKind kind)
{
    if (kind == EventKind::Back || kind == EventKind::OpenMenu) {
        state_.screen = kind == EventKind::Back ? Screen::Settings : Screen::Menu;
    } else if (kind == EventKind::NavigateNext || kind == EventKind::NavigatePrevious) {
        state_.setting_action = static_cast<std::uint8_t>((state_.setting_action +
            (kind == EventKind::NavigateNext ? 1 : 3)) % 4);
    } else if (kind == EventKind::Select) {
        const unsigned maximum = state_.setting_key == SettingKey::Mute ? 1 :
            state_.setting_key == SettingKey::Volume || state_.setting_key == SettingKey::Brightness ? 100 :
            state_.setting_key == SettingKey::DisplayTimeout ? 30 : 7;
        const unsigned step = state_.setting_key == SettingKey::Volume || state_.setting_key == SettingKey::Brightness ? 5 : 1;
        const unsigned minimum = state_.setting_key == SettingKey::Brightness ? 5 : 0;
        switch (state_.setting_action) {
        case 0: state_.setting_draft = state_.setting_draft > step + minimum ? state_.setting_draft-step : minimum; break;
        case 1: state_.setting_draft = state_.setting_draft+step < maximum ? state_.setting_draft+step : maximum; break;
        case 2: {
            const auto event = state_.setting_key == SettingKey::Theme ? EventKind::SetTheme :
                state_.setting_key == SettingKey::Mute ? EventKind::SetMuted :
                state_.setting_key == SettingKey::Volume ? EventKind::SetVolume :
                state_.setting_key == SettingKey::Brightness ? EventKind::SetBrightness :
                state_.setting_key == SettingKey::DisplayTimeout ? EventKind::SetDisplayTimeout : EventKind::SetMotionConfig;
            state_.screen = Screen::Settings;
            return handle({event,state_.setting_draft}) | Effect::Render;
        }
        case 3: state_.screen = Screen::Settings; break;
        }
    }
    return effect(Effect::Render);
}
} // namespace pico
