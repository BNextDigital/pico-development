#pragma once

#include <pico/types.hpp>

namespace pico::platform {

class Storage {
public:
    bool init();
    bool load(PersistedState &state);
    bool save(const PersistedState &state);
    // Call after load(): settings_load is shared and runs once at boot.
    bool load_clock(clock::Settings &settings);
    bool save_clock(const clock::Settings &settings);

private:
    bool ready_{false};
};

} // namespace pico::platform
