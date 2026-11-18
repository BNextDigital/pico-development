#pragma once

#include <cstdint>

namespace pico {

enum class FocusPhase : std::uint8_t { None, Work, Break };

// Session sequencing only. Elapsed time continues to belong to Timer.
class FocusSession {
public:
    void start()
    {
        phase_ = FocusPhase::Work;
        waiting_ = false;
        completed_work_ = 0;
    }
    void complete()
    {
        if (phase_ == FocusPhase::None || waiting_) return;
        if (phase_ == FocusPhase::Work && completed_work_ != UINT32_MAX) ++completed_work_;
        waiting_ = true;
    }
    bool next()
    {
        if (!waiting_) return false;
        phase_ = phase_ == FocusPhase::Work ? FocusPhase::Break : FocusPhase::Work;
        waiting_ = false;
        return true;
    }
    void cancel()
    {
        phase_ = FocusPhase::None;
        waiting_ = false;
        completed_work_ = 0;
    }
    FocusPhase phase() const { return phase_; }
    bool waiting() const { return waiting_; }
    std::uint32_t completed_work() const { return completed_work_; }

private:
    FocusPhase phase_{FocusPhase::None};
    bool waiting_{false};
    std::uint32_t completed_work_{0};
};

} // namespace pico
