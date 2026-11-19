#pragma once

#include <cstdint>

namespace pico {

struct FocusRecord {
    std::uint32_t rounds{0};
    std::uint32_t work_seconds{0};
    bool finished{false};
};

enum class FocusPhase : std::uint8_t { None, Work, Break };

// Session sequencing only. Elapsed time continues to belong to Timer.
class FocusSession {
public:
    void start(unsigned goal = 0)
    {
        goal_ = goal; finished_ = false; work_seconds_ = 0;
        phase_ = FocusPhase::Work;
        waiting_ = false;
        completed_work_ = 0;
    }
    void complete(std::uint32_t work_seconds = 0)
    {
        if (phase_ == FocusPhase::None || waiting_) return;
        if (phase_ == FocusPhase::Work) {
            if (completed_work_ != UINT32_MAX) ++completed_work_;
            work_seconds_ += work_seconds > UINT32_MAX-work_seconds_ ? UINT32_MAX-work_seconds_ : work_seconds;
            if (goal_ && completed_work_ >= goal_) {
                phase_ = FocusPhase::None; waiting_ = false; finished_ = true; return;
            }
        }
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
        completed_work_ = 0; work_seconds_ = 0; goal_ = 0; finished_ = false;
    }
    bool finished() const { return finished_; }
    unsigned goal() const { return goal_; }
    std::uint32_t work_seconds() const { return work_seconds_; }
    FocusPhase phase() const { return phase_; }
    bool waiting() const { return waiting_; }
    std::uint32_t completed_work() const { return completed_work_; }

private:
    unsigned goal_{0};
    bool finished_{false};
    std::uint32_t work_seconds_{0};
    FocusPhase phase_{FocusPhase::None};
    bool waiting_{false};
    std::uint32_t completed_work_{0};
};

} // namespace pico
