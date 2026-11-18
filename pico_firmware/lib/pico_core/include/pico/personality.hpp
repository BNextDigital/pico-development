#pragma once
#include <pico/types.hpp>
namespace pico {
class Personality {
public:
    explicit Personality(std::uint32_t seed = 0x5049434fU);
    void activity(State &state, std::uint32_t now_ms);
    void react(State &state, Mood mood, std::uint32_t now_ms,
               std::uint32_t duration_ms = 4000);
    std::uint32_t inactive_for(std::uint32_t now_ms) const { return now_ms-last_activity_; }
    void rest(State &state);
    bool advance(State &state, std::uint32_t now_ms);
private:
    std::uint32_t random();
    std::uint32_t rng_, last_activity_{0}, next_blink_, blink_end_{0};
    std::uint32_t next_idle_, temporary_end_{0};
    bool temporary_{false};
};
}
