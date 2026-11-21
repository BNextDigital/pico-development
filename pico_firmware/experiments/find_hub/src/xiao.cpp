#include <cerrno>
#include <algorithm>
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <bluetooth/fast_pair/fhn/fhn.h>
#include "app_ui.h"
#include "app_ui_priv.h"
#include "app_battery.h"
#include "app_motion_detector.h"
#include "platform/pico_display.hpp"
#include "platform/pico_audio.hpp"
#include "platform/pico_motion.hpp"
#include "platform/pico_status_led.hpp"
#include "buttons.hpp"
LOG_MODULE_REGISTER(pico_fhn_ui, CONFIG_PICO_LOG_LEVEL);
namespace {
const gpio_dt_spec action=GPIO_DT_SPEC_GET(DT_ALIAS(pico_focus_button),gpios);
const gpio_dt_spec navigate=GPIO_DT_SPEC_GET(DT_ALIAS(pico_nav_button),gpios);
const gpio_dt_spec green=GPIO_DT_SPEC_GET(DT_ALIAS(pico_status_led),gpios);
const gpio_dt_spec red=GPIO_DT_SPEC_GET(DT_ALIAS(pico_status_red),gpios);
const gpio_dt_spec blue=GPIO_DT_SPEC_GET(DT_ALIAS(pico_status_blue),gpios);
gpio_callback action_callback,navigate_callback;
pico::platform::Display display;
pico::platform::Audio audio;
pico::platform::Motion motion;
pico::platform::StatusLed heartbeat;
pico::fhn::Buttons buttons;
atomic_t states, detector_enabled, detected;
std::uint32_t last_ring;
bool previous_ring;
void service(k_work *);
K_WORK_DELAYABLE_DEFINE(service_work,service);
void wake() { k_work_reschedule(&service_work,K_NO_WAIT); }
void edge(const device *,gpio_callback *,gpio_port_pins_t) { wake(); }
void send(pico::fhn::Action action)
{
    using A=pico::fhn::Action;
    app_ui_request request;
    switch (action) {
    case A::Pairing: request=APP_UI_REQUEST_FP_ADV_PAIRING_MODE_CHANGE; break;
    case A::StopRinging: request=APP_UI_REQUEST_RINGING_STOP; break;
    case A::Identify: request=APP_UI_REQUEST_ID_MODE_ENTER; break;
    case A::Recovery: request=APP_UI_REQUEST_RECOVERY_MODE_ENTER; break;
    case A::Motion: request=APP_UI_REQUEST_MOTION_INDICATE; break;
    case A::Reset: request=APP_UI_REQUEST_FACTORY_RESET; break;
    default: return;
    }
    // Fast Pair callbacks require the cooperative system workqueue, never ISR context.
    app_ui_request_broadcast(request);
}
void service(k_work *)
{
    const auto now=k_uptime_get_32();
    const auto snapshot=atomic_get(&states);
    const bool ringing=(snapshot & BIT(APP_UI_STATE_RINGING))!=0;
    const int a=gpio_pin_get_dt(&action),n=gpio_pin_get_dt(&navigate);
    if (a>=0 && n>=0) send(buttons.update((a ? 1U : 0U)|(n ? 2U : 0U),now,ringing));
    pico::State face;
    face.finding=ringing;
    face.mood=(snapshot & BIT(APP_UI_STATE_PROVISIONED)) ? pico::Mood::Happy : pico::Mood::Neutral;
    display.render(face); // existing cache skips identical OLED transfers
    if (ringing && (!previous_ring || now-last_ring>=1000U)) {
        audio.play(pico::audio::Cue::TimerComplete,25); last_ring=now;
    }
    previous_ring=ringing;
    const auto config=atomic_get(&detector_enabled) ? pico::motion::kAll : 0U;
    motion.poll(static_cast<std::uint8_t>(config));
    std::uint32_t delay=motion.next_poll_ms(static_cast<std::uint8_t>(config),now);
    if (buttons.pending() || a<0 || n<0) delay=std::min(delay,std::uint32_t{20});
    if (ringing) delay=std::min(delay,std::uint32_t{1000}-(now-last_ring));
    if (delay!=UINT32_MAX) k_work_schedule(&service_work,K_MSEC(delay));
}
int configure(const gpio_dt_spec &pin,gpio_callback &cb)
{
    if (!gpio_is_ready_dt(&pin)) return -ENODEV;
    int err=gpio_pin_configure_dt(&pin,GPIO_INPUT);
    if (err) return err;
    gpio_init_callback(&cb,edge,BIT(pin.pin));
    err=gpio_add_callback(pin.port,&cb);
    if (err) return err;
    return gpio_pin_interrupt_configure_dt(&pin,GPIO_INT_EDGE_BOTH);
}
bool motion_event(const pico::Event &event)
{
    if (event.kind==pico::EventKind::MotionActive || event.kind==pico::EventKind::MotionPickup ||
        event.kind==pico::EventKind::MotionRock) atomic_set(&detected,1);
    return true;
}
void detector_start() { atomic_set(&detector_enabled,1); atomic_clear(&detected); wake(); }
bool detector_period() {
    // Never silently report "no motion" after a runtime sensor failure.
    const bool movement=atomic_set(&detected,0)!=0;
    return !motion.ready() || movement;
}
void detector_stop() { atomic_clear(&detector_enabled); atomic_clear(&detected); wake(); }
void test_request(app_ui_request request) {
    if (request==APP_UI_REQUEST_MOTION_INDICATE && atomic_get(&detector_enabled)) {
        atomic_set(&detected,1); LOG_INF("Physical bench button injected a motion indication");
    }
}
APP_UI_REQUEST_LISTENER_REGISTER(pico_bench_request,test_request);
const bt_fast_pair_fhn_motion_detector_cb motion_callbacks{detector_start,detector_period,detector_stop};
}
extern "C" int app_ui_init(void)
{
    // All state, credentials and settings belong to this standalone experiment.
    display.init();
    if (!audio.init()) LOG_WRN("No sound output: visual-only bench prototype, not a compliant locator tag");
    audio.set_muted(false); // Pico's ordinary mute must not silence DULT indications.
    for (const auto &led : {red,blue}) if (gpio_is_ready_dt(&led)) gpio_pin_configure_dt(&led,GPIO_OUTPUT_INACTIVE);
    heartbeat.init(green);
    int err=configure(action,action_callback);
    if (!err) err=configure(navigate,navigate_callback);
    if (!err) wake();
    return err;
}
extern "C" int app_ui_state_change_indicate(app_ui_state state,bool active)
{
    if (state>=APP_UI_STATE_COUNT) return -EINVAL;
    atomic_set_bit_to(&states,state,active);
    LOG_INF("FHN UI state %u = %u",static_cast<unsigned>(state),active ? 1U : 0U);
    wake(); return 0;
}
extern "C" int app_battery_init(void)
{
    LOG_WRN("Battery percentage unavailable in this prototype; no simulated charge reading");
    return bt_fast_pair_fhn_battery_level_set(BT_FAST_PAIR_FHN_BATTERY_LEVEL_NONE);
}
extern "C" int app_motion_detector_init(void)
{
    if (!motion.init(motion_event)) return -ENODEV;
    return bt_fast_pair_fhn_motion_detector_cb_register(&motion_callbacks);
}
