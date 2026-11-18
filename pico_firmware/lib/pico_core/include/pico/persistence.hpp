#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <pico/types.hpp>

namespace pico::persistence {

inline constexpr std::size_t kBlobSize = 32;
inline constexpr std::uint8_t kStorageVersion = 5;
using Blob = std::array<std::uint8_t, kBlobSize>;

Blob encode(const PersistedState &state);
bool decode(const std::uint8_t *data, std::size_t size, PersistedState &state);
inline bool decode(const Blob &data, PersistedState &state)
{ return decode(data.data(), data.size(), state); }

} // namespace pico::persistence
