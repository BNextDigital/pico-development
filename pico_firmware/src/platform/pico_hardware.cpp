#include "pico_hardware.hpp"
#include <pico/button.hpp>

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(pico_hardware, CONFIG_PICO_LOG_LEVEL);

#define PRIMARY_BUTTON_NODE DT_ALIAS(sw0)
#define SECONDARY_BUTTON_NODE DT_ALIAS(sw1)
#define STATUS_LED_NODE DT_ALIAS(led0)
#define HOME_DOCK_NODE DT_ALIAS(pico_dock_home)
#define CAR_DOCK_NODE DT_ALIAS(pico_dock_car)
#define BUZZER_NODE DT_ALIAS(pico_buzzer)
#define FOCUS_BUTTON_NODE DT_ALIAS(pico_focus_button)
#define NAV_BUTTON_NODE DT_ALIAS(pico_nav_button)

#define PICO_HAS_PRIMARY_BUTTON DT_NODE_HAS_STATUS(PRIMARY_BUTTON_NODE, okay)
#define PICO_HAS_SECONDARY_BUTTON DT_NODE_HAS_STATUS(SECONDARY_BUTTON_NODE, okay)
#define PICO_HAS_STATUS_LED DT_NODE_HAS_STATUS(STATUS_LED_NODE, okay)
#define PICO_HAS_HOME_DOCK DT_NODE_HAS_STATUS(HOME_DOCK_NODE, okay)
#define PICO_HAS_CAR_DOCK DT_NODE_HAS_STATUS(CAR_DOCK_NODE, okay)
#define PICO_HAS_BUZZER DT_NODE_HAS_STATUS(BUZZER_NODE, okay)
#define PICO_HAS_FOCUS_BUTTON DT_NODE_HAS_STATUS(FOCUS_BUTTON_NODE, okay)
#define PICO_HAS_NAV_BUTTON DT_NODE_HAS_STATUS(NAV_BUTTON_NODE, okay)

namespace {

pico::platform::EventSink event_sink;
std::uint32_t last_primary_press_ms;
std::uint32_t last_secondary_press_ms;

#if PICO_HAS_FOCUS_BUTTON
const gpio_dt_spec focus_button = GPIO_DT_SPEC_GET(FOCUS_BUTTON_NODE, gpios);
pico::Button focus_button_state;
bool focus_button_ready;
#endif

#if PICO_HAS_NAV_BUTTON
const gpio_dt_spec nav_button = GPIO_DT_SPEC_GET(NAV_BUTTON_NODE, gpios);
pico::Button nav_button_state;
bool nav_button_ready;
#endif

#if PICO_HAS_PRIMARY_BUTTON
const gpio_dt_spec primary_button = GPIO_DT_SPEC_GET(PRIMARY_BUTTON_NODE, gpios);
gpio_callback primary_button_callback;
#endif

#if PICO_HAS_SECONDARY_BUTTON
const gpio_dt_spec secondary_button = GPIO_DT_SPEC_GET(SECONDARY_BUTTON_NODE, gpios);
gpio_callback secondary_button_callback;
#endif

#if PICO_HAS_STATUS_LED
const gpio_dt_spec status_led = GPIO_DT_SPEC_GET(STATUS_LED_NODE, gpios);
#endif

#if PICO_HAS_HOME_DOCK
const gpio_dt_spec home_dock = GPIO_DT_SPEC_GET(HOME_DOCK_NODE, gpios);
gpio_callback home_dock_callback;
#endif

#if PICO_HAS_CAR_DOCK
const gpio_dt_spec car_dock = GPIO_DT_SPEC_GET(CAR_DOCK_NODE, gpios);
gpio_callback car_dock_callback;
#endif

#if PICO_HAS_BUZZER
const pwm_dt_spec buzzer = PWM_DT_SPEC_GET(BUZZER_NODE);

void stop_buzzer(k_work *)
{
    const int error = pwm_set_dt(&buzzer, buzzer.period, 0);
    if (error != 0) {
        LOG_WRN("Could not stop speaker output (%d)", error);
    }
}

K_WORK_DELAYABLE_DEFINE(stop_buzzer_work, stop_buzzer);
#endif

bool emit(pico::EventKind kind, std::uint32_t value = 0)
{
    return event_sink != nullptr && event_sink({kind, value});
}

#if PICO_HAS_PRIMARY_BUTTON
void primary_button_pressed(const device *, gpio_callback *, gpio_port_pins_t)
{
    const auto now = k_uptime_get_32();
    if (last_primary_press_ms != 0U && now - last_primary_press_ms < 75U) {
        return;
    }
    last_primary_press_ms = now;
    emit(pico::EventKind::PrimaryPressed);
}
#endif

#if PICO_HAS_SECONDARY_BUTTON
void secondary_button_pressed(const device *, gpio_callback *, gpio_port_pins_t)
{
    const auto now = k_uptime_get_32();
    if (last_secondary_press_ms != 0U && now - last_secondary_press_ms < 75U) {
        return;
    }
    last_secondary_press_ms = now;
    emit(pico::EventKind::SecondaryPressed);
}
#endif

pico::Dock current_dock()
{
#if PICO_HAS_CAR_DOCK
    if (gpio_pin_get_dt(&car_dock) > 0) {
        return pico::Dock::Car;
    }
#endif
#if PICO_HAS_HOME_DOCK
    if (gpio_pin_get_dt(&home_dock) > 0) {
        return pico::Dock::Home;
    }
#endif
    return pico::Dock::None;
}

#if PICO_HAS_HOME_DOCK || PICO_HAS_CAR_DOCK
void dock_changed(const device *, gpio_callback *, gpio_port_pins_t)
{
    emit(pico::EventKind::DockChanged,
         static_cast<std::uint32_t>(current_dock()));
}
#endif

bool configure_input(const gpio_dt_spec &input, gpio_callback &callback,
                     gpio_callback_handler_t handler, gpio_flags_t interrupt_mode)
{
    if (!gpio_is_ready_dt(&input)) {
        LOG_ERR("GPIO controller for pin %u is not ready", input.pin);
        return false;
    }

    int error = gpio_pin_configure_dt(&input, GPIO_INPUT);
    if (error != 0) {
        LOG_ERR("Could not configure input pin %u (%d)", input.pin, error);
        return false;
    }

    gpio_init_callback(&callback, handler, BIT(input.pin));
    error = gpio_add_callback(input.port, &callback);
    if (error != 0) {
        LOG_ERR("Could not add callback for pin %u (%d)", input.pin, error);
        return false;
    }

    error = gpio_pin_interrupt_configure_dt(&input, interrupt_mode);
    if (error != 0) {
        LOG_ERR("Could not enable interrupt for pin %u (%d)", input.pin, error);
        return false;
    }

    return true;
}

} // namespace

namespace pico::platform {

bool Hardware::init(EventSink sink)
{
    event_sink = sink;
    bool ready = true;

#if PICO_HAS_FOCUS_BUTTON
    focus_button_ready = gpio_is_ready_dt(&focus_button) &&
                         gpio_pin_configure_dt(&focus_button, GPIO_INPUT) == 0;
    ready &= focus_button_ready;
    if (focus_button_ready) {
        LOG_INF("Focus button ready: D0 to GND; tap=start/pat, hold=cancel");
    } else {
        LOG_ERR("Focus button GPIO could not be configured");
    }
#endif

#if PICO_HAS_NAV_BUTTON
    nav_button_ready = gpio_is_ready_dt(&nav_button) &&
                      gpio_pin_configure_dt(&nav_button, GPIO_INPUT) == 0;
    ready &= nav_button_ready;
    if (nav_button_ready) LOG_INF("Navigation button ready: D1 to GND");
    else LOG_ERR("Navigation button GPIO could not be configured");
#endif

#if PICO_HAS_STATUS_LED
    if (!gpio_is_ready_dt(&status_led) ||
        gpio_pin_configure_dt(&status_led, GPIO_OUTPUT_INACTIVE) != 0) {
        LOG_ERR("Status LED is not ready");
        ready = false;
    }
#else
    LOG_WRN("No led0 alias; status indication is disabled");
#endif

#if PICO_HAS_PRIMARY_BUTTON
    ready &= configure_input(primary_button, primary_button_callback,
                             primary_button_pressed, GPIO_INT_EDGE_TO_ACTIVE);
#else
    LOG_INF("No sw0 alias; primary interaction remains available over BLE");
#endif

#if PICO_HAS_SECONDARY_BUTTON
    ready &= configure_input(secondary_button, secondary_button_callback,
                             secondary_button_pressed, GPIO_INT_EDGE_TO_ACTIVE);
#else
    LOG_INF("No sw1 alias; menu navigation remains available over BLE");
#endif

#if PICO_HAS_HOME_DOCK
    ready &= configure_input(home_dock, home_dock_callback, dock_changed,
                             GPIO_INT_EDGE_BOTH);
#endif
#if PICO_HAS_CAR_DOCK
    ready &= configure_input(car_dock, car_dock_callback, dock_changed,
                             GPIO_INT_EDGE_BOTH);
#endif
#if PICO_HAS_HOME_DOCK || PICO_HAS_CAR_DOCK
    emit(EventKind::DockChanged, static_cast<std::uint32_t>(current_dock()));
#else
    LOG_INF("No dock aliases; dock detection is disabled");
#endif

#if PICO_HAS_BUZZER
    if (!pwm_is_ready_dt(&buzzer)) {
        LOG_ERR("Speaker PWM is not ready");
        ready = false;
    }
#else
    LOG_INF("No pico-buzzer alias; sound effects are disabled");
#endif

    return ready;
}

void Hardware::poll()
{
#if PICO_HAS_FOCUS_BUTTON || PICO_HAS_NAV_BUTTON
    const auto sample = [](const gpio_dt_spec &pin, pico::Button &button,
                           bool ready, EventKind tap, EventKind hold) {
        if (!ready) return;
        const int pressed = gpio_pin_get_dt(&pin);
        if (pressed < 0) return;
        switch (button.update(pressed != 0, k_uptime_get_32())) {
        case ButtonAction::Tap: emit(tap); break;
        case ButtonAction::Hold: emit(hold); break;
        case ButtonAction::None: break;
        }
    };
#if PICO_HAS_FOCUS_BUTTON
    sample(focus_button, focus_button_state, focus_button_ready,
           EventKind::FocusButtonTapped, EventKind::FocusButtonHeld);
#endif
#if PICO_HAS_NAV_BUTTON
    sample(nav_button, nav_button_state, nav_button_ready,
           EventKind::NavigateNext, EventKind::Back);
#endif
#endif
}

void Hardware::reflect(const State &state)
{
#if PICO_HAS_STATUS_LED
    // Solid while connected; a short heartbeat while advertising.
    const bool led_on = state.connected || (state.uptime_seconds % 4U == 0U);
    const int error = gpio_pin_set_dt(&status_led, led_on ? 1 : 0);
    if (error != 0) {
        LOG_WRN("Could not update status LED (%d)", error);
    }
#else
    ARG_UNUSED(state);
#endif
}

void Hardware::chirp()
{
#if PICO_HAS_BUZZER
    constexpr std::uint32_t period = PWM_HZ(2400);
    const int error = pwm_set_dt(&buzzer, period, period / 2U);
    if (error != 0) {
        LOG_WRN("Could not start speaker chirp (%d)", error);
        return;
    }
    k_work_reschedule(&stop_buzzer_work, K_MSEC(45));
#else
    LOG_DBG("Chirp requested without configured speaker hardware");
#endif
}

Capabilities Hardware::capabilities() const
{
    Capabilities result = capability(Capability::None);
#if PICO_HAS_BUZZER
    result |= capability(Capability::Speaker);
#endif
#if PICO_HAS_HOME_DOCK
    result |= capability(Capability::HomeDock);
#endif
#if PICO_HAS_CAR_DOCK
    result |= capability(Capability::CarDock);
#endif
    return result;
}

} // namespace pico::platform
