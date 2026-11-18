#include <pico/timer.hpp>
#include <pico/types.hpp>
namespace pico {
bool Timer::start(std::uint32_t seconds, std::uint32_t now)
{
    if (seconds == 0 || seconds > kMaxTimerSeconds) return false;
    total_ = remaining_ = seconds;
    last_ = now;
    running_ = true;
    paused_ = false;
    return true;
}
bool Timer::advance(std::uint32_t now)
{
    if (!running_) return false;
    const auto elapsed = now - last_;
    last_ = now;
    if (elapsed >= remaining_) {
        remaining_ = 0;
        running_ = false;
        return true;
    }
    remaining_ -= elapsed;
    return false;
}
bool Timer::pause(std::uint32_t now)
{
    if (!running_) return false;
    if (advance(now)) return true;
    running_ = false;
    paused_ = true;
    return false;
}
bool Timer::resume(std::uint32_t now)
{
    if (!paused_) return false;
    last_ = now;
    running_ = true;
    paused_ = false;
    return true;
}
void Timer::cancel()
{
    running_ = paused_ = false;
    total_ = remaining_ = 0;
}
}
