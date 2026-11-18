#pragma once

#include <pico/types.hpp>
#include <pico/timer.hpp>
#include <pico/personality.hpp>

namespace pico {

class Core {
public:
    explicit Core(std::uint32_t seed = 0x5049434fU) : personality_(seed) {}
    const State &state() const { return state_; }
    Effects handle(const Event &event);

    PersistedState persistent_state() const;
    bool restore(const PersistedState &persisted);

private:
    Effects set_mode(Mode mode);
    Effects set_mood(Mood mood);
    Effects set_theme(FaceTheme theme);
    Effects on_tick(std::uint32_t now_seconds);
    Effects navigate(EventKind kind);
    void sync_timer();
    Effects timer_complete();
    void activity();

    State state_{};
    Mode mode_before_car_dock_{Mode::Companion};
    Timer timer_{};
    Personality personality_;
    std::uint32_t animation_ms_{0};
};

} // namespace pico
