#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <pico/types.hpp>

namespace pico::persistence {

inline constexpr std::size_t kBlobSize = 16;
inline constexpr std::uint8_t kStorageVersion = 2;
using Blob = std::array<std::uint8_t, kBlobSize>;

Blob encode(const PersistedState &state);
bool decode(const Blob &data, PersistedState &state);

} // namespace pico::persistence
