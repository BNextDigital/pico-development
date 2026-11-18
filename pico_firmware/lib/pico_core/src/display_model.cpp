#include <pico/display_model.hpp>

#include <algorithm>

namespace pico::display {
namespace {

struct Sprite {
    std::uint8_t width;
    std::uint8_t height;
    const std::uint8_t *rows;
};

constexpr std::uint8_t kEyeOpenRows[]{
    0b01110, 0b11111, 0b11011, 0b11011, 0b11011, 0b11111, 0b01110,
};
constexpr std::uint8_t kEyeHappyRows[]{
    0b00000, 0b00000, 0b10001, 0b01010, 0b00100, 0b00000, 0b00000,
};
constexpr std::uint8_t kEyeSleepyRows[]{
    0b00000, 0b00000, 0b00000, 0b11111, 0b00000, 0b00000, 0b00000,
};
constexpr std::uint8_t kMouthNeutralRows[]{0b1111111};
constexpr std::uint16_t kMouthSmileRows[]{
    0b100000001, 0b010000010, 0b001000100, 0b000111000,
};

constexpr Sprite kEyeOpen{5, 7, kEyeOpenRows};
constexpr Sprite kEyeHappy{5, 7, kEyeHappyRows};
constexpr Sprite kEyeSleepy{5, 7, kEyeSleepyRows};
constexpr Sprite kMouthNeutral{7, 1, kMouthNeutralRows};

bool locate(const MonoSurface &surface, std::uint16_t x, std::uint16_t y,
            std::size_t &byte_index, std::uint8_t &bit)
{
    if (surface.data == nullptr || x >= surface.width || y >= surface.height) {
        return false;
    }

    switch (surface.layout) {
    case MonoLayout::HorizontalMsb: {
        const auto stride = (surface.width + 7U) / 8U;
        byte_index = static_cast<std::size_t>(y) * stride + (x / 8U);
        bit = static_cast<std::uint8_t>(7U - (x % 8U));
        break;
    }
    case MonoLayout::VerticalLsb:
        byte_index = static_cast<std::size_t>(y / 8U) * surface.width + x;
        bit = static_cast<std::uint8_t>(y % 8U);
        break;
    case MonoLayout::VerticalMsb:
        byte_index = static_cast<std::size_t>(y / 8U) * surface.width + x;
        bit = static_cast<std::uint8_t>(7U - (y % 8U));
        break;
    }

    return byte_index < surface.size;
}

void set_pixel(MonoSurface &surface, std::uint16_t x, std::uint16_t y,
               bool on = true)
{
    std::size_t byte_index = 0;
    std::uint8_t bit = 0;
    if (!locate(surface, x, y, byte_index, bit)) {
        return;
    }

    if (on) {
        surface.data[byte_index] |= static_cast<std::uint8_t>(1U << bit);
    } else {
        surface.data[byte_index] &= static_cast<std::uint8_t>(~(1U << bit));
    }
}

void fill_rect(MonoSurface &surface, std::uint16_t x, std::uint16_t y,
               std::uint16_t width, std::uint16_t height, bool on = true)
{
    const auto end_x = static_cast<std::uint32_t>(x) + width;
    const auto end_y = static_cast<std::uint32_t>(y) + height;
    for (std::uint32_t py = y; py < end_y; ++py) {
        for (std::uint32_t px = x; px < end_x; ++px) {
            set_pixel(surface, static_cast<std::uint16_t>(px),
                      static_cast<std::uint16_t>(py), on);
        }
    }
}

void outline_rect(MonoSurface &surface, std::uint16_t x, std::uint16_t y,
                  std::uint16_t width, std::uint16_t height)
{
    if (width == 0U || height == 0U) {
        return;
    }
    fill_rect(surface, x, y, width, 1);
    fill_rect(surface, x, y + height - 1U, width, 1);
    fill_rect(surface, x, y, 1, height);
    fill_rect(surface, x + width - 1U, y, 1, height);
}

void blit(MonoSurface &surface, const Sprite &sprite, std::uint16_t x,
          std::uint16_t y)
{
    for (std::uint8_t row = 0; row < sprite.height; ++row) {
        for (std::uint8_t column = 0; column < sprite.width; ++column) {
            const auto mask = static_cast<std::uint8_t>(
                1U << (sprite.width - column - 1U));
            if ((sprite.rows[row] & mask) != 0U) {
                set_pixel(surface, x + column, y + row);
            }
        }
    }
}

void blit_smile(MonoSurface &surface, std::uint16_t x, std::uint16_t y)
{
    for (std::uint8_t row = 0; row < 4U; ++row) {
        for (std::uint8_t column = 0; column < 9U; ++column) {
            if ((kMouthSmileRows[row] & (1U << (8U - column))) != 0U) {
                set_pixel(surface, x + column, y + row);
            }
        }
    }
}

void render_face(const State &state, MonoSurface &surface)
{
    const auto center_x = surface.width / 2U;
    const auto center_y = surface.height / 2U;

    const Sprite *eye = &kEyeOpen;
    if (state.mood == Mood::Happy) {
        eye = &kEyeHappy;
    } else if (state.mood == Mood::Sleepy) {
        eye = &kEyeSleepy;
    }

    blit(surface, *eye, center_x - 23U, center_y - 10U);
    blit(surface, *eye, center_x + 18U, center_y - 10U);

    if (state.mood == Mood::Happy) {
        blit_smile(surface, center_x - 4U, center_y + 7U);
    } else if (state.mood == Mood::Alert) {
        outline_rect(surface, center_x - 3U, center_y + 5U, 7U, 7U);
    } else if (state.mood == Mood::Curious) {
        fill_rect(surface, center_x - 3U, center_y + 7U, 7U, 1U);
        set_pixel(surface, center_x + 5U, center_y + 5U);
        set_pixel(surface, center_x + 6U, center_y + 4U);
    } else {
        blit(surface, kMouthNeutral, center_x - 3U, center_y + 8U);
    }
}

void render_status(const State &state, MonoSurface &surface)
{
    if (state.connected) {
        fill_rect(surface, 3, 3, 3, 3);
    } else {
        outline_rect(surface, 3, 3, 3, 3);
    }

    if (state.dock != Dock::None) {
        const auto x = surface.width - 10U;
        outline_rect(surface, x, 3, 7, 6);
        if (state.dock == Dock::Car) {
            fill_rect(surface, x + 1U, 7, 2, 2);
            fill_rect(surface, x + 4U, 7, 2, 2);
        }
    }

    constexpr std::uint8_t user_mode_count = 6;
    const auto first_x = surface.width / 2U - 17U;
    const auto y = surface.height - 4U;
    for (std::uint8_t index = 0; index < user_mode_count; ++index) {
        const bool selected = index == static_cast<std::uint8_t>(state.mode);
        if (selected) {
            fill_rect(surface, first_x + index * 6U, y, 3, 3);
        } else {
            set_pixel(surface, first_x + index * 6U + 1U, y + 1U);
        }
    }

    if (state.timer_running && state.timer_total_seconds > 0U) {
        const auto bar_width = surface.width - 16U;
        const auto filled = static_cast<std::uint16_t>(
            (static_cast<std::uint64_t>(bar_width) *
             state.timer_remaining_seconds) /
            state.timer_total_seconds);
        outline_rect(surface, 8, 12, bar_width, 5);
        if (filled > 2U) {
            fill_rect(surface, 9, 13, filled - 2U, 3);
        }
    }
}

} // namespace

std::size_t required_buffer_size(const MonoSurface &surface)
{
    if (surface.layout == MonoLayout::HorizontalMsb) {
        return static_cast<std::size_t>((surface.width + 7U) / 8U) *
               surface.height;
    }
    return static_cast<std::size_t>(surface.width) *
           ((surface.height + 7U) / 8U);
}

bool render(const State &state, MonoSurface surface)
{
    if (surface.data == nullptr || surface.width < 64U || surface.height < 32U ||
        surface.size < required_buffer_size(surface)) {
        return false;
    }

    std::fill(surface.data, surface.data + required_buffer_size(surface), 0U);
    render_status(state, surface);
    render_face(state, surface);
    return true;
}

bool pixel_at(const MonoSurface &surface, std::uint16_t x, std::uint16_t y)
{
    std::size_t byte_index = 0;
    std::uint8_t bit = 0;
    return locate(surface, x, y, byte_index, bit) &&
           (surface.data[byte_index] & static_cast<std::uint8_t>(1U << bit)) != 0U;
}

} // namespace pico::display
