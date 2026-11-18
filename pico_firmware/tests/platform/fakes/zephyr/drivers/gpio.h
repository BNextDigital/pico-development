#pragma once
#include <cstdint>
struct device {};
using gpio_port_pins_t = std::uint32_t;
struct gpio_callback;
using gpio_callback_handler_t = void (*)(const device *, gpio_callback *, gpio_port_pins_t);
struct gpio_callback { gpio_callback_handler_t handler{}; gpio_port_pins_t pins{}; };
struct gpio_dt_spec { const device *port{}; unsigned pin{}; unsigned dt_flags{}; };
#define GPIO_INPUT 1U
#define GPIO_INT_EDGE_BOTH 2U
bool gpio_is_ready_dt(const gpio_dt_spec *);
int gpio_pin_configure_dt(const gpio_dt_spec *, unsigned);
void gpio_init_callback(gpio_callback *, gpio_callback_handler_t, gpio_port_pins_t);
int gpio_add_callback(const device *, gpio_callback *);
int gpio_remove_callback(const device *, gpio_callback *);
int gpio_pin_interrupt_configure_dt(const gpio_dt_spec *, unsigned);
int gpio_pin_get_dt(const gpio_dt_spec *);
