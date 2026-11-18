#include <cstdint>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <pico/core.hpp>

#include "platform/pico_motion.hpp"
#include "platform/pico_audio.hpp"
#include "platform/pico_microphone.hpp"
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
                   pico::platform::Hardware &hardware, pico::platform::Audio &audio, pico::platform::Microphone &microphone,
                   pico::platform::Display &display,
                   pico::platform::Storage &storage,
                   pico::platform::Ble &ble, pico::Capabilities capabilities)
{
    microphone.sync(core.state().tuner_running,core.state().tuner_session);
    audio.set_muted(core.state().sound_muted || core.state().tuner_running);
    audio.set_metronome_running(core.state().metronome_running);
    if (pico::has_effect(effects, pico::Effect::Persist)) {
        storage.save(core.persistent_state());
    }
    if (pico::has_effect(effects, pico::Effect::Chirp) && !core.state().sound_muted && !core.state().tuner_running) {
        if (audio.ready()) audio.play(core.state().sound_cue);
        else if (core.state().sound_cue < pico::audio::Cue::MetronomeClick) hardware.chirp();
    }
    if (pico::has_effect(effects, pico::Effect::Render)) {
        display.render(core.state());
    }
    if (pico::has_effect(effects, pico::Effect::Notify)) {
        const auto microphone_bit = pico::capability(pico::Capability::Microphone);
        const auto current = static_cast<pico::Capabilities>((capabilities & ~microphone_bit) |
            (core.state().microphone_ready ? microphone_bit : 0U));
        const auto motion_bit = pico::capability(pico::Capability::Motion);
        const auto available = static_cast<pico::Capabilities>((current & ~motion_bit) |
            (core.state().motion_ready ? motion_bit : 0U));
        ble.publish(core.state(), available);
    }
}

} // namespace

int main()
{
    LOG_INF("Pico firmware %s starting", CONFIG_PICO_FIRMWARE_VERSION);

    pico::Core core;
    pico::platform::Hardware hardware;
    pico::platform::Motion motion;
    pico::platform::Audio audio;
    pico::platform::Microphone microphone;
    pico::platform::Display display;
    pico::platform::Storage storage;
    pico::platform::Ble ble;

    const bool storage_ready = storage.init();
    const bool ble_ready = ble.init(queue_event);

    pico::PersistedState persisted{};
    if (storage_ready && storage.load(persisted)) {
        core.restore(persisted);
    }

    core.handle({pico::EventKind::MotionReady,motion.init(queue_event) ? 1U : 0U});
    audio.init();
    core.handle({pico::EventKind::MicrophoneReady, microphone.init(queue_event) ? 1U : 0U});
    display.init();
    hardware.init(queue_event);
    const auto capabilities = hardware.capabilities() | display.capabilities() |
        (audio.ready() ? pico::capability(pico::Capability::Speaker) : 0U) |
        (microphone.ready() ? pico::capability(pico::Capability::Microphone) : 0U);

    if (ble_ready) {
        ble.start_advertising();
    }

    apply_effects(core.handle({pico::EventKind::Boot, 0}), core, hardware, audio, microphone,
                  display, storage, ble, capabilities);
    hardware.reflect(core.state());

    std::uint32_t last_tick = UINT32_MAX;
    std::uint32_t last_animation_ms = 0;
    while (true) {
        pico::Event event{};
        if (k_msgq_get(&event_queue, &event, K_MSEC(20)) == 0) {
            apply_effects(core.handle({pico::EventKind::MusicTick, k_uptime_get_32()}),
                          core, hardware, audio, microphone, display, storage, ble, capabilities);
            const auto effects = core.handle(event);
            apply_effects(effects, core, hardware, audio, microphone, display, storage, ble,
                          capabilities);
            hardware.reflect(core.state());
        }

        hardware.poll();
        motion.poll(core.state().motion_config);
        if (core.state().motion_ready != motion.ready()) {
            apply_effects(core.handle({pico::EventKind::MotionReady,motion.ready() ? 1U : 0U}),
                          core,hardware,audio,microphone,display,storage,ble,capabilities);
        }
        microphone.poll();
        if (core.state().microphone_ready != microphone.ready()) {
            apply_effects(core.handle({pico::EventKind::MicrophoneReady,microphone.ready() ? 1U : 0U}),
                          core,hardware,audio,microphone,display,storage,ble,capabilities);
        }

        apply_effects(core.handle({pico::EventKind::MusicTick, k_uptime_get_32()}),
                      core, hardware, audio, microphone, display, storage, ble, capabilities);
        const auto now_ms = k_uptime_get_32();
        if (now_ms - last_animation_ms >= 100U) {
            last_animation_ms = now_ms;
            apply_effects(core.handle({pico::EventKind::AnimationTick, now_ms}),
                          core, hardware, audio, microphone, display, storage, ble, capabilities);
        }

        const auto now_seconds = static_cast<std::uint32_t>(
            k_uptime_get() / MSEC_PER_SEC);
        if (now_seconds != last_tick) {
            last_tick = now_seconds;
            const auto effects = core.handle({pico::EventKind::Tick, now_seconds});
            apply_effects(effects, core, hardware, audio, microphone, display, storage, ble,
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
