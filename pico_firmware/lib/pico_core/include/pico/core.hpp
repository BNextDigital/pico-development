#pragma once

#include <pico/types.hpp>

namespace pico {

class Core {
public:
    const State &state() const { return state_; }
    Effects handle(const Event &event);

    PersistedState persistent_state() const;
    bool restore(const PersistedState &persisted);

private:
    Effects set_mode(Mode mode);
    Effects set_mood(Mood mood);
    Effects set_theme(FaceTheme theme);
    Effects on_tick(std::uint32_t now_seconds);

    State state_{};
    Mode mode_before_car_dock_{Mode::Companion};
    std::uint32_t timer_deadline_seconds_{0};
    std::uint32_t mood_reset_deadline_seconds_{0};
};

} // namespace pico
