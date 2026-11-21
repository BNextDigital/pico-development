#include "pico_microphone.hpp"
#include <cerrno>
#include <pico/pitch.hpp>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(pico_microphone, CONFIG_PICO_LOG_LEVEL);

#if defined(CONFIG_PICO_MICROPHONE) && DT_NODE_HAS_STATUS(DT_ALIAS(pico_dmic), okay)
#include <zephyr/audio/dmic.h>
#include <zephyr/drivers/regulator.h>
#include <zephyr/sys/atomic.h>
namespace {
constexpr std::size_t kBytes = pico::pitch::kFrameSamples * sizeof(std::int16_t);
const device *mic = DEVICE_DT_GET(DT_ALIAS(pico_dmic));
const device *power = DEVICE_DT_GET(DT_ALIAS(pico_mic_power));
K_MEM_SLAB_DEFINE(mic_slab, kBytes, 4, 4);
K_SEM_DEFINE(mic_wake, 0, 1);
K_MSGQ_DEFINE(mic_readings, sizeof(pico::Event), 1, 4);
atomic_t mic_ready;
atomic_t request;
pico::platform::EventSink event_sink;
pico::pitch::Detector detector;

// STOP is asynchronous in nrfx. Drain queued blocks and wait for DMA ownership
// to return before disabling power or allowing the next session to start.
bool stop_capture()
{
    dmic_trigger(mic, DMIC_TRIGGER_STOP);
    const auto deadline = k_uptime_get() + 400;
    do {
        void *buffer = nullptr; std::size_t bytes = 0;
        if (dmic_read(mic,0,&buffer,&bytes,10) == 0) k_mem_slab_free(&mic_slab,buffer);
        if (k_mem_slab_num_used_get(&mic_slab) == 0) {
            const int error = regulator_disable(power);
            if (error) { LOG_ERR("Mic power disable failed (%d)",error); return false; }
            return true;
        }
    } while (k_uptime_get() < deadline);
    LOG_ERR("PDM buffers not released; capture disabled until restart");
    return false;
}
void microphone_loop(void *,void *,void *)
{
    while (true) {
        k_sem_take(&mic_wake,K_FOREVER);
        const auto session_request = atomic_get(&request);
        if (!atomic_get(&mic_ready) || !(session_request & 1)) continue;
        int error = regulator_enable(power);
        if (error) { LOG_ERR("Mic power enable failed (%d)",error); atomic_clear(&mic_ready); continue; }
        k_msleep(50);
        detector.reset();
        error = dmic_trigger(mic,DMIC_TRIGGER_START);
        if (!error) {
            unsigned settling = 2;
#if defined(CONFIG_PICO_BRINGUP_DIAGNOSTICS)
            std::int64_t last_diagnostic = -2000;
#endif
            while (atomic_get(&request) == session_request) {
                void *buffer = nullptr; std::size_t bytes = 0;
                error = dmic_read(mic,0,&buffer,&bytes,400);
                if (error) break;
                pico::pitch::Reading reading;
                const auto analysis_start = k_uptime_get();
                if (bytes == kBytes && settling == 0)
                    reading = detector.analyze(static_cast<const std::int16_t *>(buffer),pico::pitch::kFrameSamples);
                else if (bytes != kBytes) error = -EIO;
#if defined(CONFIG_PICO_BRINGUP_DIAGNOSTICS)
                if (settling == 0 && analysis_start - last_diagnostic >= 2000) {
                    last_diagnostic = analysis_start;
                    LOG_INF("Mic signal=%u level=%u note=%u cents=%d analysis_ms=%u",
                        static_cast<unsigned>(reading.signal),static_cast<unsigned>(reading.level),
                        static_cast<unsigned>(reading.note),static_cast<int>(reading.cents),
                        static_cast<unsigned>(k_uptime_get()-analysis_start));
                }
#else
                ARG_UNUSED(analysis_start);
#endif
                k_mem_slab_free(&mic_slab,buffer);
                if (error) break;
                if (settling) { --settling; continue; }
                if (atomic_get(&request) != session_request) break;
                const pico::Event event{pico::EventKind::TunerReading,
                    pico::pitch::pack(reading,static_cast<std::uint8_t>(session_request >> 1))};
                k_msgq_purge(&mic_readings); // Latest estimate only; raw PCM never leaves worker.
                k_msgq_put(&mic_readings,&event,K_NO_WAIT);
            }
        }
        const bool stopped = stop_capture();
        if (error || !stopped) {
            LOG_ERR("PDM capture stopped (%d); restart required",error);
            atomic_clear(&mic_ready);
        }
        k_msgq_purge(&mic_readings);
        // If the user re-entered while STOP was draining, honor the new token.
        if (atomic_get(&request) & 1) k_sem_give(&mic_wake);
    }
}
K_THREAD_DEFINE(pico_mic_thread,4096,microphone_loop,nullptr,nullptr,nullptr,8,K_FP_REGS,0);
}
#endif

namespace pico::platform {
bool Microphone::init(EventSink sink)
{
#if defined(CONFIG_PICO_MICROPHONE) && DT_NODE_HAS_STATUS(DT_ALIAS(pico_dmic), okay)
    event_sink = sink;
    if (!device_is_ready(mic) || !device_is_ready(power)) { LOG_WRN("PDM or mic power not ready"); return false; }
    pcm_stream_cfg stream{};
    stream.pcm_rate = pitch::kSampleRate;
    stream.pcm_width = 16;
    stream.block_size = kBytes;
    stream.mem_slab = &mic_slab;
    dmic_cfg config{};
    config.io.min_pdm_clk_freq = 1000000;
    config.io.max_pdm_clk_freq = 1000000;
    config.io.min_pdm_clk_dc = 40;
    config.io.max_pdm_clk_dc = 60;
    config.streams = &stream;
    config.channel.req_num_chan = 1;
    config.channel.req_num_streams = 1;
    config.channel.req_chan_map_lo = dmic_build_channel_map(0,0,PDM_CHAN_LEFT);
    const int error = dmic_configure(mic,&config);
    if (error) { LOG_WRN("PDM configuration failed (%d)",error); return false; }
    atomic_set(&mic_ready,1);
    LOG_INF("Sense microphone ready: 15625 Hz mono; capture only in tuner");
    return true;
#else
    ARG_UNUSED(sink);
    LOG_INF("Microphone disabled; tuner requires the microphone profile");
    return false;
#endif
}
bool Microphone::ready() const
{
#if defined(CONFIG_PICO_MICROPHONE) && DT_NODE_HAS_STATUS(DT_ALIAS(pico_dmic), okay)
    return atomic_get(&mic_ready) != 0;
#else
    return false;
#endif
}
void Microphone::sync(bool active,std::uint8_t session)
{
#if defined(CONFIG_PICO_MICROPHONE) && DT_NODE_HAS_STATUS(DT_ALIAS(pico_dmic), okay)
    const auto next = active && ready() ? (static_cast<atomic_val_t>(session) << 1) | 1 : 0;
    if (atomic_set(&request,next) != next) k_sem_give(&mic_wake);
#else
    ARG_UNUSED(active); ARG_UNUSED(session);
#endif
}
void Microphone::poll()
{
#if defined(CONFIG_PICO_MICROPHONE) && DT_NODE_HAS_STATUS(DT_ALIAS(pico_dmic), okay)
    Event event;
    if (event_sink && k_msgq_get(&mic_readings,&event,K_NO_WAIT) == 0) event_sink(event);
#endif
}
}

#if defined(CONFIG_PICO_RUNTIME_PROFILE)
#include "pico_runtime_profile.hpp"
namespace pico::platform {
void profile_microphone()
{
#if defined(CONFIG_PICO_MICROPHONE)
    profile_slab("mic_slab", &mic_slab);
#endif
}
}
#endif
