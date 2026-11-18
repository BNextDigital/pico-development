#pragma once

#include <cstdint>

namespace pico {

enum class ButtonAction { None, Tap, Hold };

// Sample at least every 20 ms. Unsigned subtraction also handles uptime wrap.
class Button {
public:
    ButtonAction update(bool pressed, std::uint32_t now_ms)
    {
        if (pressed != raw_pressed_) {
            raw_pressed_ = pressed;
            raw_changed_ms_ = now_ms;
        }

        if (raw_pressed_ != stable_pressed_ &&
            now_ms - raw_changed_ms_ >= kDebounceMs) {
            stable_pressed_ = raw_pressed_;
            if (stable_pressed_) {
                pressed_ms_ = now_ms;
                hold_sent_ = false;
            } else if (!hold_sent_) {
                // A release sampled at the hold boundary must not become a tap.
                if (raw_changed_ms_ - pressed_ms_ >= kHoldMs) {
                    hold_sent_ = true;
                    return ButtonAction::Hold;
                }
                return ButtonAction::Tap;
            }
        }

        if (stable_pressed_ && raw_pressed_ && !hold_sent_ &&
            now_ms - pressed_ms_ >= kHoldMs) {
            hold_sent_ = true;
            return ButtonAction::Hold;
        }
        return ButtonAction::None;
    }

private:
    static constexpr std::uint32_t kDebounceMs = 30;
    static constexpr std::uint32_t kHoldMs = 1000;
    bool raw_pressed_{false};
    bool stable_pressed_{false};
    bool hold_sent_{false};
    std::uint32_t raw_changed_ms_{0};
    std::uint32_t pressed_ms_{0};
};

} // namespace pico
