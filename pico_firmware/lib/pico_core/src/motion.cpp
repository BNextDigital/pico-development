#include <pico/motion.hpp>
#include <algorithm>

namespace pico::motion {
namespace {
std::int32_t absolute(std::int32_t value) { return value < 0 ? -value : value; }
std::int32_t distance(Sample a,Sample b)
{ return absolute(a.x-b.x)+absolute(a.y-b.y)+absolute(a.z-b.z); }
std::int32_t component(Sample value,unsigned axis)
{ return axis == 0 ? value.x : axis == 1 ? value.y : value.z; }
bool gravity_like(Sample value)
{
    if (value.x < -4000 || value.x > 4000 || value.y < -4000 || value.y > 4000 ||
        value.z < -4000 || value.z > 4000) return false;
    const auto square = static_cast<std::int64_t>(value.x)*value.x +
        static_cast<std::int64_t>(value.y)*value.y + static_cast<std::int64_t>(value.z)*value.z;
    return square >= 850LL*850 && square <= 1150LL*1150;
}
}
void Detector::restart(Sample value,std::uint32_t now,bool valid)
{
    previous_ = quiet_anchor_ = baseline_ = value;
    last_ms_ = quiet_since_ = now;
    initialized_ = true; valid_ = valid; settled_ = pickup_armed_ = rock_armed_ = false;
    stage_ = Stage::Find; centered_ = rested_ = pickup_candidate_ = false;
}
Signals Detector::sample(Sample value,std::uint32_t now)
{
    const bool good = gravity_like(value);
    if (!initialized_ || !good || !valid_ || now-last_ms_ > 250U) {
        const Signals result = settled_ ? Active : None;
        restart(value,now,good);
        return result;
    }
    if (now == last_ms_) return None;
    const bool quiet = distance(value,previous_) <= 35 && distance(value,quiet_anchor_) <= 80;
    last_ms_ = now; previous_ = value;
    Signals result = None;
    if (!quiet) {
        quiet_since_ = now; quiet_anchor_ = value; rested_ = false;
        if (settled_) {
            settled_ = false; result |= Active;
            moving_since_ = now; stage_ = Stage::Find; centered_ = pickup_candidate_ = false;
            if (picked_before_ && now-pickup_at_ < 5000U) pickup_armed_ = false;
            if (rocked_before_ && now-rock_at_ < 10000U) rock_armed_ = false;
        }
    } else if (!settled_ && now-quiet_since_ >= 2000U) {
        settled_ = true; baseline_ = quiet_anchor_;
        pickup_armed_ = rock_armed_ = true;
        result |= Settled;
    }
    if (settled_) {
        if (now-quiet_since_ >= 60000U && (!rested_ || now-rest_at_ >= 10000U)) {
            rested_ = true; rest_at_ = now; result |= Rest;
        }
        return result;
    }
    if (now-moving_since_ > 2500U) { pickup_armed_ = rock_armed_ = false; return result; }
    if (pickup_armed_) {
        if (distance(value,baseline_) < 180) pickup_candidate_ = false;
        else if (!pickup_candidate_) { pickup_candidate_ = true; pickup_since_ = now; }
        else if (now-pickup_since_ >= 120U) {
            pickup_armed_ = false; picked_before_ = true; pickup_at_ = now;
            result |= Pickup;
        }
    }
    if (!rock_armed_) return result;
    const auto delta = component(value,axis_) - component(baseline_,axis_);
    switch (stage_) {
    case Stage::Find: {
        unsigned selected = 0; std::int32_t largest = 0;
        for (unsigned axis=0; axis<3; ++axis) {
            const auto change = absolute(component(value,axis)-component(baseline_,axis));
            if (absolute(component(baseline_,axis)) <= 600 && change >= 350 && change > largest) {
                selected = axis; largest = change;
            }
        }
        if (largest) {
            axis_ = selected;
            sign_ = component(value,axis_) >= component(baseline_,axis_) ? 1 : -1;
            stage_ = Stage::First; stage_since_ = now;
        }
        break;
    }
    case Stage::First:
        if (delta*sign_ < 350) stage_ = Stage::Find;
        else if (now-stage_since_ >= 160U) stage_ = Stage::Opposite;
        break;
    case Stage::Opposite:
        if (delta*sign_ <= -350) { stage_ = Stage::Second; stage_since_ = now; }
        break;
    case Stage::Second:
        if (delta*sign_ > -350) stage_ = Stage::Opposite;
        else if (now-stage_since_ >= 160U) { stage_ = Stage::Center; centered_ = false; }
        break;
    case Stage::Center:
        if (distance(value,baseline_) > 160) centered_ = false;
        else if (!centered_) { centered_ = true; stage_since_ = now; }
        else if (now-stage_since_ >= 160U) {
            rock_armed_ = pickup_armed_ = false; rocked_before_ = true; rock_at_ = now;
            result |= Rock;
        }
        break;
    }
    return result;
}
} // namespace pico::motion
