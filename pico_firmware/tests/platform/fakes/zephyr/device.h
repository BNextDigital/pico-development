#pragma once
#include <zephyr/drivers/gpio.h>
bool device_is_ready(const device *);
const device *fake_display_device();
#define DEVICE_DT_GET(node) fake_display_device()
