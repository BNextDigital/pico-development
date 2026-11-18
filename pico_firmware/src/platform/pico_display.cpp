#include "pico_display.hpp"

#include <array>
#include <cerrno>
#include <cstdint>

#include <zephyr/devicetree.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>
#if defined(CONFIG_PICO_BRINGUP_DIAGNOSTICS)
#include <zephyr/drivers/i2c.h>
#include <zephyr/sys/printk.h>
#endif

#include <pico/display_model.hpp>

LOG_MODULE_REGISTER(pico_display, CONFIG_PICO_LOG_LEVEL);

#if defined(CONFIG_PICO_DISPLAY) && DT_HAS_CHOSEN(zephyr_display)
#define PICO_HAS_DISPLAY 1
#else
#define PICO_HAS_DISPLAY 0
#endif

namespace {

#if PICO_HAS_DISPLAY
constexpr std::uint16_t kMaxWidth = pico::display::kDefaultWidth;
constexpr std::uint16_t kMaxHeight = pico::display::kDefaultHeight;
constexpr std::size_t kFrameBufferSize = pico::display::kDefaultBufferSize;

const device *display_device = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
display_capabilities display_caps{};
std::array<std::uint8_t, kFrameBufferSize> frame_buffer{};

pico::display::MonoLayout display_layout()
{
    return (display_caps.screen_info & SCREEN_INFO_MONO_MSB_FIRST) != 0U
               ? pico::display::MonoLayout::VerticalMsb
               : pico::display::MonoLayout::VerticalLsb;
}
#endif

} // namespace

namespace pico::platform {

bool Display::init()
{
#if PICO_HAS_DISPLAY
    if (!device_is_ready(display_device)) {
        LOG_ERR("Chosen display device is not ready");
        return false;
    }

    display_get_capabilities(display_device, &display_caps);
    if (display_caps.x_resolution > kMaxWidth ||
        display_caps.y_resolution > kMaxHeight ||
        display_caps.x_resolution < 64U || display_caps.y_resolution < 32U ||
        (display_caps.screen_info & SCREEN_INFO_MONO_VTILED) == 0U) {
        LOG_ERR("Unsupported display geometry %ux%u or memory layout",
                display_caps.x_resolution, display_caps.y_resolution);
        return false;
    }

    const int format_error = display_set_pixel_format(display_device,
                                                       PIXEL_FORMAT_MONO01);
    if (format_error != 0) {
        LOG_ERR("Could not select monochrome pixel format (%d)", format_error);
        return false;
    }

    const int blanking_error = display_blanking_off(display_device);
    if (blanking_error != 0 && blanking_error != -ENOSYS) {
        LOG_ERR("Could not enable display (%d)", blanking_error);
        return false;
    }

    ready_ = true;
    LOG_INF("Display ready: %ux%u", display_caps.x_resolution,
            display_caps.y_resolution);
    return true;
#else
    LOG_WRN("No zephyr,display chosen node; OLED rendering is disabled");
    return false;
#endif
}

bool Display::render(const State &state)
{
#if PICO_HAS_DISPLAY
    if (!ready_) {
        return false;
    }

    display::MonoSurface surface{
        frame_buffer.data(), frame_buffer.size(), display_caps.x_resolution,
        display_caps.y_resolution, display_layout(),
    };
    if (!display::render(state, surface)) {
        LOG_WRN("Could not compose Pico display frame");
        return false;
    }

    display_buffer_descriptor descriptor{};
    descriptor.buf_size = display::required_buffer_size(surface);
    descriptor.width = display_caps.x_resolution;
    descriptor.height = display_caps.y_resolution;
    descriptor.pitch = display_caps.x_resolution;

    const int error = display_write(display_device, 0, 0, &descriptor,
                                    frame_buffer.data());
    if (error != 0) {
        LOG_WRN("Display write failed (%d)", error);
        return false;
    }
    return true;
#else
    ARG_UNUSED(state);
    return false;
#endif
}

Capabilities Display::capabilities() const
{
    return ready_ ? capability(Capability::Display)
                  : capability(Capability::None);
}

#if defined(CONFIG_PICO_BRINGUP_DIAGNOSTICS)
void Display::diagnose(const State &state)
{
    const bool frame_written = render(state);
    printk("[Pico diag] uptime=%u display_ready=%u frame_written=%u\n",
           static_cast<unsigned int>(state.uptime_seconds),
           static_cast<unsigned int>(ready_),
           static_cast<unsigned int>(frame_written));
#if PICO_HAS_DISPLAY
#if DT_ON_BUS(DT_CHOSEN(zephyr_display), i2c)
    const device *bus = DEVICE_DT_GET(DT_BUS(DT_CHOSEN(zephyr_display)));
    if (!device_is_ready(bus)) {
        printk("[Pico diag] OLED I2C controller is not ready\n");
        return;
    }

    // Address-only writes: no OLED commands or framebuffer data are sent.
    std::uint8_t unused = 0;
    const int at_3c = i2c_write(bus, &unused, 0, 0x3c);
    const int at_3d = i2c_write(bus, &unused, 0, 0x3d);
    printk("[Pico diag] %s: 0x3c=%d 0x3d=%d (0=ACK)\n",
           bus->name, at_3c, at_3d);
#else
    printk("[Pico diag] Chosen display is not on I2C\n");
#endif
#else
    printk("[Pico diag] No enabled chosen display\n");
#endif
}
#endif

} // namespace pico::platform
