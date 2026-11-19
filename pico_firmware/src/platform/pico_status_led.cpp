#include "pico_status_led.hpp"
#include <pico/status_led.hpp>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>
LOG_MODULE_REGISTER(pico_status_led, CONFIG_PICO_LOG_LEVEL);
namespace pico::platform {
bool StatusLed::init(const gpio_dt_spec &pin)
{
    pin_ = pin;
    if (!gpio_is_ready_dt(&pin_) || gpio_pin_configure_dt(&pin_,GPIO_OUTPUT_INACTIVE)) return false;
    k_work_init_delayable(&work_,update);
    k_work_schedule(&work_,K_NO_WAIT);
    return true;
}
void StatusLed::update(k_work *work)
{
    auto *self = CONTAINER_OF(k_work_delayable_from_work(work),StatusLed,work_);
    self->on_ = !self->on_;
    const int error = gpio_pin_set_dt(&self->pin_,self->on_ ? 1 : 0);
    if (error) { LOG_WRN("Status LED GPIO failed (%d); heartbeat stopped",error); return; }
    k_work_schedule(&self->work_,K_MSEC(self->on_ ? status_led::kOnMs : status_led::kOffMs));
}
}
