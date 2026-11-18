#include "pico_audio.hpp"
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(pico_audio, CONFIG_PICO_LOG_LEVEL);

#if defined(CONFIG_PICO_AUDIO_I2S) && DT_NODE_HAS_STATUS(DT_ALIAS(pico_i2s), okay)
#include <zephyr/drivers/i2s.h>
#include <zephyr/sys/atomic.h>
namespace {
constexpr std::size_t kFrames = 128;
constexpr std::size_t kBytes = kFrames * 2 * sizeof(std::int16_t);
const device *audio_device = DEVICE_DT_GET(DT_ALIAS(pico_i2s));
K_MEM_SLAB_DEFINE(audio_slab, kBytes, 4, 4);
struct SoundJob { pico::audio::Cue cue; std::uint8_t volume; };
K_MSGQ_DEFINE(audio_queue, sizeof(SoundJob), 4, 4);
atomic_t audio_ready;
atomic_t audio_muted;
atomic_t audio_playing;
atomic_t metronome_enabled;
bool beat_cue(pico::audio::Cue cue)
{
    return cue == pico::audio::Cue::MetronomeClick || cue == pico::audio::Cue::MetronomeAccent;
}

bool wait_buffers()
{
    const auto deadline = k_uptime_get() + 150;
    while (k_mem_slab_num_used_get(&audio_slab) != 0) {
        if (k_uptime_get() >= deadline) return false;
        k_msleep(2);
    }
    return true;
}
void play_cue(pico::audio::Cue cue,unsigned volume)
{
    if (!wait_buffers()) {
        LOG_ERR("I2S buffers not released; disabling audio until restart");
        atomic_clear(&audio_ready);
        return;
    }
    // DROP also recovers a preceding underrun without reconfiguring the device.
    int error = i2s_trigger(audio_device, I2S_DIR_TX, I2S_TRIGGER_DROP);
    if (error || !wait_buffers()) return;
    const auto blocks = (pico::audio::frames(cue) + kFrames - 1) / kFrames;
    bool started = false;
    for (std::uint32_t block=0; block<blocks; ++block) {
        void *data = nullptr;
        error = k_mem_slab_alloc(&audio_slab, &data, K_MSEC(100));
        if (error) break;
        pico::audio::render(cue, block * kFrames, static_cast<std::int16_t *>(data),
                            kFrames, volume);
        error = i2s_write(audio_device, data, kBytes);
        if (error) { k_mem_slab_free(&audio_slab, data); break; }
        // Queue two buffers before starting so the first IRQ has its successor.
        if (!started && (block == 1 || block + 1 == blocks)) {
            error = i2s_trigger(audio_device, I2S_DIR_TX, I2S_TRIGGER_START);
            if (error) break;
            started = true;
        }
    }
    if (!error && started) error = i2s_trigger(audio_device, I2S_DIR_TX, I2S_TRIGGER_DRAIN);
    if (error || !wait_buffers()) {
        LOG_WRN("I2S cue failed (%d); dropping stream", error);
        i2s_trigger(audio_device, I2S_DIR_TX, I2S_TRIGGER_DROP);
        if (!wait_buffers()) atomic_clear(&audio_ready);
    }
}
void audio_loop(void *, void *, void *)
{
    while (true) {
        SoundJob job{};
        if (k_msgq_get(&audio_queue, &job, K_FOREVER) == 0 && atomic_get(&audio_ready) &&
            !atomic_get(&audio_muted) && (!beat_cue(job.cue) || atomic_get(&metronome_enabled))) {
            atomic_set(&audio_playing, 1);
            play_cue(job.cue,job.volume);
            atomic_clear(&audio_playing);
        }
    }
}
K_THREAD_DEFINE(pico_audio_thread, 2048, audio_loop, nullptr, nullptr, nullptr, 7, 0, 0);
}
#endif

namespace pico::platform {
bool Audio::init()
{
#if defined(CONFIG_PICO_AUDIO_I2S) && DT_NODE_HAS_STATUS(DT_ALIAS(pico_i2s), okay)
    if (!device_is_ready(audio_device)) { LOG_WRN("I2S not ready; visual alerts remain available"); return false; }
    i2s_config config{};
    config.word_size = 16;
    config.channels = 2;
    config.format = I2S_FMT_DATA_FORMAT_I2S;
    config.options = I2S_OPT_FRAME_CLK_CONTROLLER | I2S_OPT_BIT_CLK_CONTROLLER;
    config.frame_clk_freq = audio::kSampleRate;
    config.mem_slab = &audio_slab;
    config.block_size = kBytes;
    config.timeout = 100;
    const int error = i2s_configure(audio_device, I2S_DIR_TX, &config);
    if (error) { LOG_WRN("I2S configuration failed (%d)", error); return false; }
    atomic_set(&audio_ready, 1);
    LOG_INF("MAX98357 I2S ready; default volume=%u%%", static_cast<unsigned>(CONFIG_PICO_AUDIO_VOLUME_PERCENT));
    return true;
#else
    LOG_INF("I2S audio disabled; visual alerts remain available");
    return false;
#endif
}
bool Audio::ready() const
{
#if defined(CONFIG_PICO_AUDIO_I2S) && DT_NODE_HAS_STATUS(DT_ALIAS(pico_i2s), okay)
    return atomic_get(&audio_ready) != 0;
#else
    return false;
#endif
}
void Audio::set_metronome_running(bool running)
{
#if defined(CONFIG_PICO_AUDIO_I2S) && DT_NODE_HAS_STATUS(DT_ALIAS(pico_i2s), okay)
    atomic_set(&metronome_enabled, running ? 1 : 0);
#else
    ARG_UNUSED(running);
#endif
}
void Audio::set_muted(bool muted)
{
#if defined(CONFIG_PICO_AUDIO_I2S) && DT_NODE_HAS_STATUS(DT_ALIAS(pico_i2s), okay)
    atomic_set(&audio_muted, muted ? 1 : 0);
    if (muted) k_msgq_purge(&audio_queue);
#else
    ARG_UNUSED(muted);
#endif
}
void Audio::play(audio::Cue cue,unsigned volume_percent)
{
#if defined(CONFIG_PICO_AUDIO_I2S) && DT_NODE_HAS_STATUS(DT_ALIAS(pico_i2s), okay)
    if (!ready() || atomic_get(&audio_muted) || cue >= audio::Cue::Count || !volume_percent) return;
    const SoundJob job{cue,static_cast<std::uint8_t>(volume_percent > 100 ? 100 : volume_percent)};
    if (beat_cue(cue)) {
        if (!atomic_get(&metronome_enabled) || atomic_get(&audio_playing) ||
            k_msgq_num_used_get(&audio_queue)) return; // Never queue stale beats behind other audio.
        k_msgq_put(&audio_queue, &job, K_NO_WAIT);
        return;
    }
    if (k_msgq_put(&audio_queue, &job, K_NO_WAIT) != 0 && cue != audio::Cue::Interact) {
        k_msgq_purge(&audio_queue);
        k_msgq_put(&audio_queue, &job, K_NO_WAIT);
    }
#else
    ARG_UNUSED(cue); ARG_UNUSED(volume_percent);
#endif
}
}
