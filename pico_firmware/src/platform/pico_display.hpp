#pragma once

#include <pico/features.hpp>
#include <pico/types.hpp>

namespace pico::platform {

class Display {
public:
    bool init();
    bool render(const State &state);
    Capabilities capabilities() const;

private:
    bool ready_{false};
};

} // namespace pico::platform
