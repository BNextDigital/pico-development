#pragma once

#include <pico/types.hpp>
#include <pico/display_power.hpp>
#include <algorithm>
#include <pico/timer.hpp>
#include <pico/personality.hpp>
#include <pico/metronome.hpp>

namespace pico {

class Core {
public:
    explicit Core(std::uint32_t seed = 0x5049434fU) : personality_(seed) {}
    const State &state() const { return state_; }
    Effects handle(const Event &event);
    std::uint32_t next_animation_ms(std::uint32_t now) const
    {
        if (state_.display_asleep) return UINT32_MAX;
        auto delay = personality_.next_update_ms(state_, now);
        if (display::auto_sleep_allowed(state_)) {
            const auto elapsed = now - display_activity_ms_;
            const auto timeout = state_.display_timeout_minutes * 60000U;
            delay = std::min(delay, elapsed >= timeout ? 0U : timeout - elapsed);
        }
        return delay;
    }
    std::uint32_t next_music_ms(std::uint32_t now) const
    {
        const auto elapsed = now - reference_started_ms_;
        const std::uint32_t reference_delay = state_.reference_playing ?
            (elapsed >= 2000U ? 0U : 2000U-elapsed) : UINT32_MAX;
        return std::min(metronome_.next_update_ms(now), reference_delay);
    }

    PersistedState persistent_state() const;
    bool restore(const PersistedState &persisted);

private:
    Effects set_mode(Mode mode);
    Effects set_mood(Mood mood);
    Effects set_theme(FaceTheme theme);
    Effects on_tick(std::uint32_t now_seconds);
    Effects navigate(EventKind kind);
    Effects edit_duration(EventKind kind);
    Effects edit_setting(EventKind kind);
    Effects open_setting(SettingKey key);
    Effects open_duration_editor(bool break_duration);
    void sync_timer();
    Effects timer_complete();
    void activity();
    void sync_metronome();
    Effects music_beat();
    void sync_focus();
    void archive_focus(bool finished);
    void cancel_focus();
    Effects start_focus_phase();

    State state_{};
    Mode mode_before_car_dock_{Mode::Companion};
    Timer timer_{};
    Metronome metronome_{};
    std::uint32_t music_ms_{0};
    std::uint32_t reference_started_ms_{0};
    FocusSession focus_{};
    Personality personality_;
    std::uint32_t animation_ms_{0};
    std::uint32_t display_activity_ms_{0};
};

} // namespace pico
