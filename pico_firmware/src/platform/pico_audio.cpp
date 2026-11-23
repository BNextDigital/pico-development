#include "pico_audio.hpp"
#include <algorithm>
#include <cstring>
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
constexpr std::size_t kFrameBytes = pico::audio::kChannels * sizeof(std::int16_t);
constexpr std::size_t kBytes = kFrames * kFrameBytes;
constexpr std::uint32_t kLeadBlocks =
    (pico::audio::frames_for_ms(pico::audio::kStartupSilenceMs) + kFrames - 1) / kFrames;
const device *audio_device = DEVICE_DT_GET(DT_ALIAS(pico_i2s));
K_MEM_SLAB_DEFINE(audio_slab, kBytes, 4, 4);
struct SoundJob { pico::audio::Cue cue; std::uint8_t volume; std::uint8_t note; atomic_val_t generation; atomic_val_t owner_generation; };
K_MSGQ_DEFINE(audio_queue, sizeof(SoundJob), 4, 4);
atomic_t audio_ready;
atomic_t audio_owner;
atomic_t owner_generation;
atomic_t audio_muted;
atomic_t audio_playing;
atomic_t metronome_enabled;
atomic_t reference_request;
atomic_t reference_generation;
bool cancelled(const SoundJob &job)
{
    return atomic_get(&audio_muted) || job.owner_generation!=atomic_get(&owner_generation) || (job.cue == pico::audio::Cue::ReferenceTone &&
        (!(atomic_get(&reference_request) & 1) || atomic_get(&reference_generation) != job.generation));
}
bool beat_cue(pico::audio::Cue cue)
{
    return cue == pico::audio::Cue::MetronomeClick || cue == pico::audio::Cue::MetronomeAccent;
}

bool wait_buffers(const SoundJob *job = nullptr)
{
    const auto deadline = k_uptime_get() + 150;
    while (k_mem_slab_num_used_get(&audio_slab) != 0) {
        if ((job && cancelled(*job)) || k_uptime_get() >= deadline) return false;
        k_msleep(2);
    }
    return true;
}

bool trigger(enum i2s_trigger_cmd command, const char *name)
{
    const int error = i2s_trigger(audio_device, I2S_DIR_TX, command);
    if (error) LOG_WRN("I2S %s failed (%d)", name, error);
    (void)name;
    return error == 0;
}

bool drop_stream()
{
    // The driver may still own queued buffers after an underrun. DROP purges
    // them and requests asynchronous release of the active DMA buffers.
    const bool dropped = trigger(I2S_TRIGGER_DROP, "DROP");
    const bool released = wait_buffers();
    if (dropped && released) return true;
    LOG_ERR("I2S recovery failed; slab used=%u, audio unavailable until restart",
            k_mem_slab_num_used_get(&audio_slab));
    atomic_clear(&audio_ready);
    return false;
}

void play_cue(const SoundJob &job)
{
    if (!drop_stream() || cancelled(job)) return;
    const auto frames = pico::audio::frames(job.cue);
    bool started = false;
    bool submitted = true;
    for (std::uint32_t block = 0, first = 0; first < frames && !cancelled(job); ++block) {
        void *data = nullptr;
        const int alloc_error = k_mem_slab_alloc(&audio_slab, &data, K_MSEC(100));
        if (alloc_error) {
            LOG_WRN("I2S TX slab allocation failed (%d), used=%u", alloc_error,
                    k_mem_slab_num_used_get(&audio_slab));
            submitted = false;
            break;
        }
        const std::size_t count = block < kLeadBlocks ? kFrames :
            std::min<std::size_t>(kFrames, frames - first);
        if (block < kLeadBlocks)
            std::memset(data, 0, kBytes);
        else {
            if (job.cue == pico::audio::Cue::ReferenceTone)
                pico::audio::render_reference(job.note, first, static_cast<std::int16_t *>(data), count, job.volume);
            else
                pico::audio::render(job.cue, first, static_cast<std::int16_t *>(data), count, job.volume);
            first += count;
        }
        if (cancelled(job)) {
            k_mem_slab_free(&audio_slab, data);
            break;
        }
        const int write_error = i2s_write(audio_device, data, count * kFrameBytes);
        if (write_error) {
            k_mem_slab_free(&audio_slab, data); // Driver owns the block only on success.
            LOG_WRN("I2S TX submit failed (%d), block=%u", write_error, block);
            submitted = false;
            break;
        }
        if (!started && block == 1 && !cancelled(job)) {
            started = trigger(I2S_TRIGGER_START, "START");
            if (!started) break;
        }
    }

    if (submitted && started && !cancelled(job) && trigger(I2S_TRIGGER_DRAIN, "DRAIN")) {
        if (wait_buffers(&job)) {
#if defined(CONFIG_PICO_BRINGUP_DIAGNOSTICS)
            if (!beat_cue(job.cue))
                LOG_INF("I2S DMA released cue=%u, PCM frames=%u", static_cast<unsigned>(job.cue), frames);
#endif
            return;
        }
        if (!cancelled(job)) LOG_WRN("I2S DRAIN timed out; dropping stream");
    }
    drop_stream();
}
void audio_loop(void *, void *, void *)
{
    while (true) {
        SoundJob job{};
        if (k_msgq_get(&audio_queue, &job, K_FOREVER) == 0 && atomic_get(&audio_ready) &&
            !cancelled(job) && (!beat_cue(job.cue) || atomic_get(&metronome_enabled))) {
            atomic_set(&audio_playing, 1);
            play_cue(job);
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
    config.word_size = audio::kWordBits;
    config.channels = audio::kChannels;
    config.format = I2S_FMT_DATA_FORMAT_I2S;
    config.options = I2S_OPT_FRAME_CLK_CONTROLLER | I2S_OPT_BIT_CLK_CONTROLLER;
    config.frame_clk_freq = audio::kSampleRate;
    config.mem_slab = &audio_slab;
    config.block_size = kBytes;
    config.timeout = 100;
    const int error = i2s_configure(audio_device, I2S_DIR_TX, &config);
#if defined(CONFIG_PICO_BRINGUP_DIAGNOSTICS)
    LOG_INF("I2S configure rc=%d, requested=%u Hz, calculated LRCLK=%u Hz BCLK=%u Hz (rounded down)",
            error, audio::kSampleRate, audio::kSampleRate, audio::kBitClockHz);
#endif
    if (error) { LOG_WRN("I2S configuration failed (%d); visual alerts remain available", error); return false; }
    atomic_set(&audio_ready, 1);
    LOG_INF("MCU I2S ready; default volume=%u%%", static_cast<unsigned>(CONFIG_PICO_AUDIO_VOLUME_PERCENT));
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
void Audio::set_owner(audio::Owner owner)
{
#if defined(CONFIG_PICO_AUDIO_I2S) && DT_NODE_HAS_STATUS(DT_ALIAS(pico_i2s), okay)
    if (atomic_set(&audio_owner,static_cast<atomic_val_t>(owner))!=static_cast<atomic_val_t>(owner)) {
        atomic_inc(&owner_generation);
        k_msgq_purge(&audio_queue);
    }
#else
    ARG_UNUSED(owner);
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
void Audio::set_reference(bool running, std::uint8_t note)
{
#if defined(CONFIG_PICO_AUDIO_I2S) && DT_NODE_HAS_STATUS(DT_ALIAS(pico_i2s), okay)
    const auto request = static_cast<atomic_val_t>((static_cast<unsigned>(note) << 1) | (running ? 1U : 0U));
    if (atomic_set(&reference_request,request) != request) atomic_inc(&reference_generation);
#else
    ARG_UNUSED(running); ARG_UNUSED(note);
#endif
}
void Audio::play(audio::Cue cue,unsigned volume_percent,std::uint8_t note)
{
#if defined(CONFIG_PICO_AUDIO_I2S) && DT_NODE_HAS_STATUS(DT_ALIAS(pico_i2s), okay)
    if (!ready() || atomic_get(&audio_muted) || cue >= audio::Cue::Count || !volume_percent) return;
    if (cue == audio::Cue::ReferenceTone &&
        (!(atomic_get(&reference_request) & 1) || !audio::frequency_millihz(note))) return;
    const SoundJob job{cue,static_cast<std::uint8_t>(volume_percent > 100 ? 100 : volume_percent),
                       note,atomic_get(&reference_generation),atomic_get(&owner_generation)};
    const bool beat = beat_cue(cue);
    if (beat && (!atomic_get(&metronome_enabled) || atomic_get(&audio_playing) ||
                 k_msgq_num_used_get(&audio_queue))) return; // Never queue stale beats.
    int error = k_msgq_put(&audio_queue, &job, K_NO_WAIT);
    if (error && !beat && cue != audio::Cue::Interact) {
        k_msgq_purge(&audio_queue);
        error = k_msgq_put(&audio_queue, &job, K_NO_WAIT);
        LOG_WRN("I2S cue queue full; replaced pending cues, submit rc=%d", error);
    }
#if defined(CONFIG_PICO_BRINGUP_DIAGNOSTICS)
    if (!beat || error)
        LOG_INF("I2S job submit cue=%u rc=%d, queued=%u", static_cast<unsigned>(cue),
                error, k_msgq_num_used_get(&audio_queue));
#endif
#else
    ARG_UNUSED(cue); ARG_UNUSED(volume_percent); ARG_UNUSED(note);
#endif
}
}

#if defined(CONFIG_PICO_RUNTIME_PROFILE)
#include "pico_runtime_profile.hpp"
namespace pico::platform {
void profile_audio()
{
#if defined(CONFIG_PICO_AUDIO_I2S) && DT_NODE_HAS_STATUS(DT_ALIAS(pico_i2s), okay)
    profile_slab("audio_slab", &audio_slab);
#endif
}
}
#endif
