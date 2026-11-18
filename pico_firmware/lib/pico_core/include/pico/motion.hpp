#pragma once
#include <cstdint>

namespace pico::motion {
struct Sample { std::int32_t x, y, z; }; // milli-g, including gravity
using Signals = std::uint8_t;
enum Signal : Signals { None=0, Settled=1, Active=2, Pickup=4, Rock=8, Rest=16 };
inline constexpr std::uint8_t kPickup = 1, kRock = 2, kRest = 4, kAll = 7;

// Conservative accelerometer-only heuristic; no position/velocity inference.
// Samples should arrive every ~40 ms. Gaps >250 ms invalidate the sequence.
class Detector {
public:
    Signals sample(Sample value, std::uint32_t now);
    void reset() { *this = Detector{}; }
private:
    void restart(Sample value,std::uint32_t now,bool valid);
    Sample previous_{}, quiet_anchor_{}, baseline_{};
    std::uint32_t last_ms_{0}, quiet_since_{0}, moving_since_{0}, stage_since_{0}, rest_at_{0};
    std::uint32_t pickup_at_{0}, rock_at_{0}, pickup_since_{0};
    bool initialized_{false}, valid_{false}, settled_{false}, pickup_armed_{false}, rock_armed_{false};
    bool picked_before_{false}, rocked_before_{false}, centered_{false}, rested_{false}, pickup_candidate_{false};
    enum class Stage : std::uint8_t { Find, First, Opposite, Second, Center };
    Stage stage_{Stage::Find};
    unsigned axis_{0};
    int sign_{1};
};
} // namespace pico::motion
