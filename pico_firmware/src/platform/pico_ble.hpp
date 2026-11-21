#pragma once

#include <pico/features.hpp>
#include <pico/types.hpp>

#include "pico_hardware.hpp"

namespace pico::platform {

class Ble {
public:
    bool init(EventSink sink);
    bool start_advertising();
    bool forget_companions();
    bool open_pairing(std::uint32_t token);
    void close_pairing();
    void publish(const State &state, Capabilities capabilities);
    void publish_clock(const State &state,bool notify=true);

private:
    bool ready_{false};
};

} // namespace pico::platform
