#pragma once

#include "pico_hardware.hpp"
#include <pico/button.hpp>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>

namespace pico::platform {

// Static-lifetime adapter. Only the system workqueue touches the classifier;
// GPIO callbacks submit work and never read pins or emit application events.
class GpioButton {
public:
    bool init(const gpio_dt_spec &pin, EventSink sink, EventKind tap, EventKind hold);

private:
    static void edge(const device *, gpio_callback *, gpio_port_pins_t);
    static void sample(k_work *);
    gpio_dt_spec pin_{};
    gpio_callback callback_{};
    k_work_delayable work_{};
    Button button_{};
    EventSink sink_{nullptr};
    EventKind tap_{EventKind::PrimaryPressed};
    EventKind hold_{EventKind::PrimaryPressed};
};

} // namespace pico::platform
