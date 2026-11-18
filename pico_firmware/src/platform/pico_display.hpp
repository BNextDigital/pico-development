#pragma once

#include <pico/features.hpp>
#include <pico/types.hpp>

namespace pico::platform {

class Display {
public:
    bool init();
    bool render(const State &state);
    Capabilities capabilities() const;
#if defined(CONFIG_PICO_BRINGUP_DIAGNOSTICS)
    void diagnose(const State &state);
#endif

private:
    bool ready_{false};
};

} // namespace pico::platform
