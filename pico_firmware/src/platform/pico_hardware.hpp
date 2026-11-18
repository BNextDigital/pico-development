#pragma once

#include <pico/features.hpp>
#include <pico/types.hpp>

namespace pico::platform {

using EventSink = bool (*)(const Event &event);

class Hardware {
public:
    bool init(EventSink sink);
    void poll();
    void reflect(const State &state);
    void chirp();
    Capabilities capabilities() const;
};

} // namespace pico::platform
