#pragma once

#include <pico/types.hpp>

namespace pico::platform {

class Storage {
public:
    bool init();
    bool load(PersistedState &state);
    bool save(const PersistedState &state);

private:
    bool ready_{false};
};

} // namespace pico::platform
