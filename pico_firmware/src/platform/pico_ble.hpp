#pragma once

#include <pico/features.hpp>
#include <pico/types.hpp>

#include "pico_hardware.hpp"

namespace pico::platform {

class Ble {
public:
    bool init(EventSink sink);
    bool start_advertising();
    void publish(const State &state, Capabilities capabilities);
    void publish_clock(const State &state,bool notify=true);

private:
    bool ready_{false};
};

} // namespace pico::platform
