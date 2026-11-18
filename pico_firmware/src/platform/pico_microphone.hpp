#pragma once
#include "pico_hardware.hpp"
namespace pico::platform {
class Microphone {
public:
    bool init(EventSink sink);
    bool ready() const;
    void sync(bool active, std::uint8_t session);
    void poll();
};
}
