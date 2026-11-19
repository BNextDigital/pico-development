#include <pico/personality.hpp>
#include <algorithm>
namespace pico {
namespace {
bool due(std::uint32_t now, std::uint32_t deadline)
{ return static_cast<std::int32_t>(now - deadline) >= 0; }
}
Personality::Personality(std::uint32_t seed) : rng_(seed ? seed : 1U),
    next_blink_(4000U + random() % 3000U), next_idle_(20000U + random() % 20000U) {}
std::uint32_t Personality::random()
{
    rng_ ^= rng_ << 13U; rng_ ^= rng_ >> 17U; rng_ ^= rng_ << 5U;
    return rng_;
}
void Personality::activity(State &s, std::uint32_t now)
{
    last_activity_ = now;
    temporary_ = false;
    s.mood = Mood::Neutral;
    s.eyes_closed = false;
    s.gaze_x = 0;
    next_blink_ = now + 4000U + random() % 3000U;
    next_idle_ = now + 20000U + random() % 20000U;
}
void Personality::react(State &s, Mood mood, std::uint32_t now, std::uint32_t duration)
{
    s.mood = mood;
    s.eyes_closed = false;
    s.gaze_x = 0;
    temporary_ = true;
    temporary_end_ = now + duration;
}
void Personality::rest(State &s)
{
    temporary_ = false;
    s.mood = Mood::Sleepy; s.eyes_closed = false; s.gaze_x = 0;
}
bool Personality::advance(State &s, std::uint32_t now)
{
    const auto mood = s.mood;
    const auto closed = s.eyes_closed;
    const auto gaze = s.gaze_x;
    if (temporary_ && due(now, temporary_end_)) {
        temporary_ = false;
        s.mood = Mood::Neutral;
        s.gaze_x = 0;
    }
    const bool busy = s.timer_running || s.timer_paused || s.screen != Screen::Home;
    if (!temporary_ && !busy && now - last_activity_ >= 300000U) {
        s.mood = Mood::Sleepy;
    } else if (!temporary_ && !busy && s.mood != Mood::Sleepy && due(now, next_idle_)) {
        react(s, Mood::Curious, now, 2000);
        s.gaze_x = (random() & 1U) ? 1 : -1;
        next_idle_ = now + 20000U + random() % 20000U;
    }
    if (s.eyes_closed && due(now, blink_end_)) {
        s.eyes_closed = false;
        next_blink_ = now + 4000U + random() % 3000U;
    }
    if (!s.eyes_closed && !temporary_ && s.mood != Mood::Sleepy &&
        s.screen == Screen::Home && due(now, next_blink_)) {
        s.eyes_closed = true;
        blink_end_ = now + 150U;
    }
    return mood != s.mood || closed != s.eyes_closed || gaze != s.gaze_x;
}
std::uint32_t Personality::next_update_ms(const State &s, std::uint32_t now) const
{
    std::uint32_t delay = UINT32_MAX;
    const auto include = [&](std::uint32_t deadline) {
        delay = std::min(delay, due(now, deadline) ? 0U : deadline - now);
    };
    if (temporary_) include(temporary_end_);
    if (s.eyes_closed) include(blink_end_);
    if (!s.eyes_closed && !temporary_ && s.mood != Mood::Sleepy && s.screen == Screen::Home)
        include(next_blink_);
    const bool busy = s.timer_running || s.timer_paused || s.screen != Screen::Home;
    if (!temporary_ && !busy && s.mood != Mood::Sleepy) {
        include(next_idle_);
        const auto inactive = now - last_activity_;
        delay = std::min(delay, inactive >= 300000U ? 0U : 300000U - inactive);
    }
    return delay;
}

}
