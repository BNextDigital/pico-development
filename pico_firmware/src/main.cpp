#include <cstdint>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <pico/core.hpp>

#include "platform/pico_ble.hpp"
#include "platform/pico_display.hpp"
#include "platform/pico_hardware.hpp"
#include "platform/pico_storage.hpp"

LOG_MODULE_REGISTER(pico_main, CONFIG_PICO_LOG_LEVEL);

namespace {

K_MSGQ_DEFINE(event_queue, sizeof(pico::Event), CONFIG_PICO_EVENT_QUEUE_SIZE, 4);

bool queue_event(const pico::Event &event)
{
    const int error = k_msgq_put(&event_queue, &event, K_NO_WAIT);
    if (error != 0) {
        LOG_WRN("Event queue full; dropped event %u",
                static_cast<unsigned int>(event.kind));
        return false;
    }
    return true;
}

void apply_effects(pico::Effects effects, pico::Core &core,
                   pico::platform::Hardware &hardware,
                   pico::platform::Display &display,
                   pico::platform::Storage &storage,
                   pico::platform::Ble &ble, pico::Capabilities capabilities)
{
    if (pico::has_effect(effects, pico::Effect::Persist)) {
        storage.save(core.persistent_state());
    }
    if (pico::has_effect(effects, pico::Effect::Render)) {
        display.render(core.state());
    }
    if (pico::has_effect(effects, pico::Effect::Chirp)) {
        hardware.chirp();
    }
    if (pico::has_effect(effects, pico::Effect::Notify)) {
        ble.publish(core.state(), capabilities);
    }
}

} // namespace

int main()
{
    LOG_INF("Pico firmware %s starting", CONFIG_PICO_FIRMWARE_VERSION);

    pico::Core core;
    pico::platform::Hardware hardware;
    pico::platform::Display display;
    pico::platform::Storage storage;
    pico::platform::Ble ble;

    const bool storage_ready = storage.init();
    const bool ble_ready = ble.init(queue_event);

    pico::PersistedState persisted{};
    if (storage_ready && storage.load(persisted)) {
        core.restore(persisted);
    }

    display.init();
    hardware.init(queue_event);
    const auto capabilities = hardware.capabilities() | display.capabilities();

    if (ble_ready) {
        ble.start_advertising();
    }

    apply_effects(core.handle({pico::EventKind::Boot, 0}), core, hardware,
                  display, storage, ble, capabilities);
    hardware.reflect(core.state());

    std::uint32_t last_tick = UINT32_MAX;
    while (true) {
        pico::Event event{};
        if (k_msgq_get(&event_queue, &event, K_MSEC(20)) == 0) {
            const auto effects = core.handle(event);
            apply_effects(effects, core, hardware, display, storage, ble,
                          capabilities);
            hardware.reflect(core.state());
        }

        hardware.poll();

        const auto now_seconds = static_cast<std::uint32_t>(
            k_uptime_get() / MSEC_PER_SEC);
        if (now_seconds != last_tick) {
            last_tick = now_seconds;
            const auto effects = core.handle({pico::EventKind::Tick, now_seconds});
            apply_effects(effects, core, hardware, display, storage, ble,
                          capabilities);
            hardware.reflect(core.state());
#if defined(CONFIG_PICO_BRINGUP_DIAGNOSTICS)
            if (now_seconds % 5U == 0U) {
                display.diagnose(core.state());
            }
#endif
        }
    }

    return 0;
}
