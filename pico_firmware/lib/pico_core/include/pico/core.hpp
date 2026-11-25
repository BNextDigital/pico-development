#pragma once

#include <pico/types.hpp>
#include <pico/display_power.hpp>
#include <algorithm>
#include <pico/timer.hpp>
#include <pico/personality.hpp>
#include <pico/metronome.hpp>
#include <pico/locator.hpp>

namespace pico {

class Core {
public:
    explicit Core(std::uint32_t seed = 0x5049434fU) : personality_(seed) {}
    const State &state() const { return state_; }
    Effects handle(const Event &event);
    std::uint32_t next_animation_ms(std::uint32_t now) const
    {
        if (state_.display_asleep || state_.pairing.active()) return UINT32_MAX;
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
        if (state_.clock.ringing) return UINT32_MAX; // Suppressed features resume phase without fast cosmetic wakes.
        const auto elapsed = now - reference_started_ms_;
        const std::uint32_t reference_delay = state_.reference_playing ?
            (elapsed >= audio::kReferencePlaybackMs ? 0U : audio::kReferencePlaybackMs-elapsed) : UINT32_MAX;
        return std::min(std::min(metronome_.next_update_ms(now), reference_delay), locator_.next(now));
    }

    const clock::Settings &clock_settings() const { return state_.clock.settings; }
    bool restore_clock(const clock::Settings &settings) { return clock_.restore(state_.clock,settings); }
    PersistedState persistent_state() const;
    bool restore(const PersistedState &persisted);

private:
    Effects begin_parking();
    Effects finish_parking(parking::Phase phase);
    Effects close_pairing(PairingStage stage);
    Effects clock_event(const Event &event);
    Effects clock_navigation(EventKind kind);
    Effects advance_clock(std::uint32_t now);
    Effects stop_finding(bool acknowledged);
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
    std::uint32_t parking_started_{};
    std::uint32_t parking_sequence_{};
    std::uint32_t parking_saved_at_{};
    bool parking_ready_{};
    clock::Engine clock_{};
    std::uint32_t alarm_signal_seconds_{0};
    Mode mode_before_car_dock_{Mode::Companion};
    Timer timer_{};
    Metronome metronome_{};
    Locator locator_{};
    Screen finding_return_screen_{Screen::Home};
    std::uint32_t music_ms_{0};
    std::uint32_t reference_started_ms_{0};
    FocusSession focus_{};
    Personality personality_;
    std::uint32_t animation_ms_{0};
    std::uint32_t display_activity_ms_{0};
};

} // namespace pico
