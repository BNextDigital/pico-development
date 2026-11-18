#pragma once
#include <zephyr/device.h>
#include <cstddef>
#include <cstdint>
struct display_capabilities { std::uint16_t x_resolution, y_resolution; unsigned screen_info; };
struct display_buffer_descriptor { std::size_t buf_size; std::uint16_t width, height, pitch; };
#define SCREEN_INFO_MONO_MSB_FIRST 2U
#define SCREEN_INFO_MONO_VTILED 1U
#define PIXEL_FORMAT_MONO01 1U
void display_get_capabilities(const device *, display_capabilities *);
int display_set_pixel_format(const device *, unsigned);
int display_blanking_on(const device *);
int display_blanking_off(const device *);
int display_set_contrast(const device *, std::uint8_t);
int display_write(const device *, unsigned, unsigned, const display_buffer_descriptor *, const void *);
