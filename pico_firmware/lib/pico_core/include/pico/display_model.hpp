#pragma once

#include <cstddef>
#include <cstdint>

#include <pico/types.hpp>

namespace pico::display {

inline constexpr std::uint16_t kDefaultWidth = 128;
inline constexpr std::uint16_t kDefaultHeight = 64;
inline constexpr std::size_t kDefaultBufferSize =
    kDefaultWidth * kDefaultHeight / 8U;

enum class MonoLayout : std::uint8_t {
    HorizontalMsb = 0,
    VerticalLsb,
    VerticalMsb,
};

struct MonoSurface {
    std::uint8_t *data;
    std::size_t size;
    std::uint16_t width;
    std::uint16_t height;
    MonoLayout layout;
};

std::size_t required_buffer_size(const MonoSurface &surface);
bool render(const State &state, MonoSurface surface);
bool pixel_at(const MonoSurface &surface, std::uint16_t x, std::uint16_t y);

} // namespace pico::display
