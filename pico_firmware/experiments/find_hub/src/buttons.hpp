#pragma once
#include <cstdint>
namespace pico::fhn {
enum class Action { None, Pairing, StopRinging, Identify, Recovery, Motion, Reset };
// Debounced two-button prototype controls. Chords never emit individual actions.
class Buttons {
public:
    Action update(unsigned raw, std::uint32_t now, bool ringing) {
        raw &= 3U;
        if (raw!=raw_) { raw_=raw; changed_=now; }
        if (now-changed_>=30U && stable_!=raw_) {
            const auto before=stable_; stable_=raw_;
            if (stable_ && ringing) { consumed_=true; return Action::StopRinging; }
            if (stable_==3U && !chord_) { chord_=true; chord_start_=now; }
            for (unsigned i=0;i<2;++i) {
                const auto bit=1U<<i;
                if ((stable_&bit) && !(before&bit)) pressed_[i]=now;
                if (!(stable_&bit) && (before&bit) && !chord_ && !consumed_) {
                    const auto held=now-pressed_[i];
                    return i==0 ? held>=3000U ? Action::Motion : Action::Pairing :
                                  held>=3000U ? Action::Recovery : Action::Identify;
                }
            }
        }
        if (stable_==3U && chord_ && !consumed_ && now-chord_start_>=5000U) {
            consumed_=true; return Action::Reset;
        }
        if (!stable_ && !raw_) { consumed_=false; chord_=false; }
        return Action::None;
    }
    bool pending() const { return raw_!=stable_ || stable_!=0 || chord_ || consumed_; }
private:
    unsigned raw_{0},stable_{0};
    bool chord_{false},consumed_{false};
    std::uint32_t changed_{0},pressed_[2]{},chord_start_{0};
};
}
