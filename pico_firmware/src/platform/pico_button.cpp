#include "pico_button.hpp"
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(pico_button, CONFIG_PICO_LOG_LEVEL);

namespace pico::platform {

bool GpioButton::init(const gpio_dt_spec &pin, EventSink sink, EventKind tap, EventKind hold)
{
    pin_ = pin;
    sink_ = sink;
    tap_ = tap;
    hold_ = hold;
    if (!gpio_is_ready_dt(&pin_)) return false;
    int error = gpio_pin_configure_dt(&pin_, GPIO_INPUT);
    if (error) return false;
    k_work_init_delayable(&work_, sample);
    gpio_init_callback(&callback_, edge, BIT(pin_.pin));
    error = gpio_add_callback(pin_.port, &callback_);
    if (error) return false;
    error = gpio_pin_interrupt_configure_dt(&pin_, GPIO_INT_EDGE_BOTH);
    if (error) {
        gpio_remove_callback(pin_.port, &callback_);
        return false;
    }
    // Detect an input held at boot, even if no new edge arrives.
    k_work_schedule(&work_, K_NO_WAIT);
    return true;
}

void GpioButton::edge(const device *, gpio_callback *callback, gpio_port_pins_t)
{
    auto *self = CONTAINER_OF(callback, GpioButton, callback_);
    // Replace a future hold/debounce deadline so every edge is sampled promptly.
    k_work_reschedule(&self->work_, K_NO_WAIT);
}

void GpioButton::sample(k_work *work)
{
    auto *self = CONTAINER_OF(k_work_delayable_from_work(work), GpioButton, work_);
    const int level = gpio_pin_get_dt(&self->pin_);
    if (level < 0) {
        // Never turn a read error into a release/tap. Retry on the next edge.
        self->button_ = Button{};
        LOG_WRN("Button pin %u read failed (%d); waiting for next edge", self->pin_.pin, level);
        return;
    }
    const auto now = k_uptime_get_32();
    const auto action = self->button_.update(level != 0, now);
    if (action != ButtonAction::None && self->sink_) {
        self->sink_({action == ButtonAction::Tap ? self->tap_ : self->hold_, 0});
    }
    const auto delay = self->button_.next_update_ms(now);
    if (delay != UINT32_MAX) {
        // schedule preserves an earlier edge submitted while this worker ran.
        // reschedule here could overwrite that edge with a distant hold deadline.
        k_work_schedule(&self->work_, K_MSEC(delay));
    }
}

} // namespace pico::platform
