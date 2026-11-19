#pragma once
#include <cstdint>
namespace pico {
// Bounded, rollover-safe locating session; no timer allocation or flash writes.
class Locator {
public:
    bool start(std::uint32_t now, unsigned seconds) {
        if (!seconds || seconds > 120 || active_) return false;
        active_=true; started_=last_pulse_=now; duration_=seconds*1000U; return true;
    }
    void stop() { active_=false; }
    bool active() const { return active_; }
    bool advance(std::uint32_t now) {
        if (!active_) return false;
        if (now-started_ >= duration_) { stop(); return false; }
        if (now-last_pulse_ < 2000U) return false;
        last_pulse_=now; return true; // skip stale pulses after a delayed wake
    }
    unsigned remaining(std::uint32_t now) const {
        const auto elapsed=now-started_;
        return !active_ || elapsed >= duration_ ? 0U : (duration_-elapsed+999U)/1000U;
    }
    std::uint32_t next(std::uint32_t now) const {
        if (!active_) return UINT32_MAX;
        const auto elapsed=now-started_, pulse=now-last_pulse_;
        const auto end=elapsed>=duration_ ? 0U : duration_-elapsed;
        const auto beat=pulse>=2000U ? 0U : 2000U-pulse;
        return end<beat ? end : beat;
    }
private:
    bool active_{false};
    std::uint32_t started_{0},last_pulse_{0},duration_{0};
};
}
