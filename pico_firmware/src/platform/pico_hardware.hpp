#pragma once

#include <pico/features.hpp>
#include <pico/types.hpp>

namespace pico::platform {

using EventSink = bool (*)(const Event &event);

class Hardware {
public:
    bool init(EventSink sink);
    void reflect(const State &state);
    void chirp();
    Capabilities capabilities() const;
    bool usb_powered() const;
};

} // namespace pico::platform
