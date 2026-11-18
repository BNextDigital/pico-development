#pragma once
#include "pico_hardware.hpp"
namespace pico::platform {
class Motion {
public:
    bool init(EventSink sink);
    bool ready() const;
    void poll(std::uint8_t config);
};
}
