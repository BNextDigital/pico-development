#pragma once
#include <cstdint>

namespace pico {
// One allocation-free timer instance; features may own additional instances later.
// Time is supplied by the caller. No platform clock or persistence dependency.
class Timer {
public:
    bool start(std::uint32_t seconds, std::uint32_t now);
    bool advance(std::uint32_t now); // true exactly once at completion
    bool pause(std::uint32_t now);
    bool resume(std::uint32_t now);
    void cancel();
    bool running() const { return running_; }
    bool paused() const { return paused_; }
    std::uint32_t remaining() const { return remaining_; }
    std::uint32_t total() const { return total_; }
private:
    std::uint32_t last_{0}, remaining_{0}, total_{0};
    bool running_{false}, paused_{false};
};
}
