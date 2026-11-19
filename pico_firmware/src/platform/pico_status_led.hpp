#pragma once
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
namespace pico::platform {
class StatusLed {
public:
    bool init(const gpio_dt_spec &pin);
private:
    static void update(k_work *work);
    gpio_dt_spec pin_{};
    k_work_delayable work_{};
    bool on_{false};
};
}
