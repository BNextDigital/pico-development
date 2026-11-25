// Model the production worker with driver ownership retained until DMA release.
#include "platform/pico_audio.cpp"
#include <cstdlib>
#include <functional>
#include <iostream>
namespace {
const device speaker{};
std::int64_t fake_now;
unsigned writes, drops, drains, starts, failures;
unsigned fail_write_at;
int config_error;
bool fail_start, fail_drain, hold_drain, hold_drop, hold_running;
bool running, draining;
i2s_config configured{};
std::deque<void *> pending;
std::vector<std::int16_t> transmitted;
std::function<void()> after_write, during_wait;
#define CHECK(x) do { if (!(x)) { ++failures; std::cerr << __LINE__ << ": " #x "\n"; } } while(false)

void release_one()
{
    k_mem_slab_free(&audio_slab, pending.front());
    pending.pop_front();
}

void reset_trace()
{
    CHECK(pending.empty() && audio_slab.used == 0);
    writes = drops = drains = starts = fail_write_at = 0;
    fail_start = fail_drain = hold_drain = hold_drop = hold_running = false;
    after_write = during_wait = {};
    transmitted.clear();
}
}
const device *fake_display_device() { return &speaker; }
bool device_is_ready(const device *) { return true; }
std::int64_t k_uptime_get() { return fake_now; }
void k_msleep(unsigned ms)
{
    fake_now += ms;
    if (during_wait) during_wait();
    if (draining && !hold_drain && !pending.empty()) release_one();
}
int i2s_configure(const device *, int direction, const i2s_config *config)
{
    CHECK(direction == I2S_DIR_TX && config->mem_slab == &audio_slab);
    configured = *config;
    return config_error;
}
int i2s_trigger(const device *, int direction, i2s_trigger_cmd command)
{
    CHECK(direction == I2S_DIR_TX);
    switch (command) {
    case I2S_TRIGGER_DROP:
        ++drops;
        running = draining = false;
        if (!hold_drop) while (!pending.empty()) release_one();
        break;
    case I2S_TRIGGER_START:
        ++starts;
        CHECK(pending.size() == 2);
        if (fail_start) return -EIO;
        running = true;
        break;
    case I2S_TRIGGER_DRAIN:
        ++drains;
        CHECK(running);
        if (fail_drain) return -EIO;
        draining = true;
        break;
    }
    return 0;
}
int i2s_write(const device *, void *data, std::size_t bytes)
{
    CHECK(bytes > 0 && bytes <= kBytes && bytes % kFrameBytes == 0);
    ++writes;
    if (writes == fail_write_at) return -EIO;
    const auto *samples = static_cast<const std::int16_t *>(data);
    transmitted.insert(transmitted.end(), samples, samples + bytes / sizeof(*samples));
    pending.push_back(data);
    if (running && !hold_running && pending.size() >= 3) release_one();
    if (after_write) after_write();
    return 0;
}
#if defined(CONFIG_PICO_AUDIO_POST_BEEP)
void test_post_beep(pico::platform::Audio &adapter)
{
    using namespace pico::audio;
    auto check_suppressed = [&](bool muted, unsigned volume) {
        pico::platform::Audio boot;
        boot.submit_post_beep(muted, volume);
        boot.submit_post_beep(false, 25); // A skipped attempt is never retried.
        CHECK(k_msgq_num_used_get(&audio_queue) == 0);
    };
    check_suppressed(true, 25);
    adapter.set_muted(true); // The startup effect path also mutes at zero volume.
    check_suppressed(false, 0);
    adapter.set_muted(false);
    adapter.set_owner(Owner::Alarm);
    check_suppressed(false, 25);
    adapter.set_owner(Owner::Feature);

    constexpr unsigned tone_frames = frames_for_ms(100);
    constexpr unsigned fade_frames = frames_for_ms(5);
    CHECK(tone_frames == 4761 && frames(Cue::PostBeep) == tone_frames && fade_frames == 238);
    adapter.submit_post_beep(false, 37);
    adapter.submit_post_beep(false, 37);
    adapter.play(Cue::PostBeep, 37);
    CHECK(k_msgq_num_used_get(&audio_queue) == 1);
    SoundJob post{};
    CHECK(k_msgq_get(&audio_queue, &post, K_NO_WAIT) == 0);
    CHECK(post.cue == Cue::PostBeep && post.volume == 37);

    reset_trace();
    play_cue(post);
    CHECK(starts == 1 && drains == 1 && drops == 1 && pending.empty());
    const std::size_t lead_samples = 3 * 128 * 2;
    CHECK(transmitted.size() == lead_samples + tone_frames * 2);
    if (transmitted.size() == lead_samples + tone_frames * 2) {
        CHECK(std::all_of(transmitted.begin(), transmitted.begin() + lead_samples,
                          [](int x) { return x == 0; }));
        const auto *pcm = transmitted.data() + lead_samples;
        CHECK(pcm[0] == 0 && pcm[(tone_frames-1)*2] == 0);
        const auto ceiling = kPeakLimit * post.volume / 100;
        unsigned crossings = 0;
        unsigned rising = 0;
        for (unsigned frame = 0; frame < tone_frames; ++frame) {
            const auto sample = pcm[2*frame];
            CHECK(sample == pcm[2*frame+1]);
            const auto fade = std::min({frame, tone_frames-1-frame, fade_frames});
            CHECK(std::abs(sample) <= static_cast<int>(kPeakLimit * post.volume * fade / (100 * fade_frames)));
            if (frame > fade_frames && frame < tone_frames - fade_frames &&
                pcm[2*(frame-1)] <= 0 && sample > 0) {
                if (rising) CHECK(frame-rising == 54 || frame-rising == 55);
                rising = frame;
                ++crossings;
            }
        }
        CHECK(crossings >= 79 && crossings <= 80);
        CHECK(std::abs(pcm[2*271]) >= ceiling * 95 / 100);
    }

    reset_trace();
    fail_start = true;
    play_cue(post);
    CHECK(writes == 2 && drains == 0 && drops == 2 && pending.empty() && adapter.ready());

    reset_trace();
    fail_drain = true;
    play_cue(post);
    CHECK(starts == 1 && drains == 1 && drops == 2 && pending.empty() && adapter.ready());

    reset_trace();
    after_write = [&] {
        if (writes == 4) {
            adapter.set_owner(Owner::Alarm);
            adapter.play(Cue::TimerComplete, 25);
        }
    };
    play_cue(post);
    CHECK(writes == 4 && drains == 0 && drops == 2 && pending.empty() && cancelled(post));
    SoundJob alarm{};
    CHECK(k_msgq_get(&audio_queue, &alarm, K_NO_WAIT) == 0);
    CHECK(alarm.cue == Cue::TimerComplete && !cancelled(alarm));
    reset_trace();
    play_cue(alarm);
    CHECK(starts == 1 && drains == 1 && drops == 1 && pending.empty() && adapter.ready());
    adapter.set_owner(Owner::Feature);
}
#endif

int main()
{
    using namespace pico::audio;
    pico::platform::Audio adapter;
    config_error = -EIO;
    CHECK(!adapter.init() && !adapter.ready());
    pico::platform::Audio failed_boot;
    failed_boot.submit_post_beep(false, 25);
    CHECK(k_msgq_num_used_get(&audio_queue) == 0);
    config_error = 0;
    CHECK(adapter.init());
    CHECK(configured.frame_clk_freq == kSampleRate && kSampleRate == 47619);
    CHECK(kBitClockHz == 1523809 && configured.word_size == 16 && configured.channels == 2);
    CHECK(configured.format == I2S_FMT_DATA_FORMAT_I2S);
    CHECK(configured.options == (I2S_OPT_FRAME_CLK_CONTROLLER | I2S_OPT_BIT_CLK_CONTROLLER));
    CHECK(configured.block_size == 512 && audio_slab.capacity == 4);

    failed_boot.submit_post_beep(false, 25);
    CHECK(k_msgq_num_used_get(&audio_queue) == 0);
#if defined(CONFIG_PICO_AUDIO_POST_BEEP)
    test_post_beep(adapter);
#else
    adapter.submit_post_beep(false, 25);
    CHECK(k_msgq_num_used_get(&audio_queue) == 0);
#endif
    reset_trace();

    adapter.set_reference(true,69);
    adapter.play(Cue::ReferenceTone,25,69);
    SoundJob job{};
    CHECK(k_msgq_get(&audio_queue,&job,K_NO_WAIT)==0);
    CHECK(job.note==69 && !cancelled(job));
    after_write=[&] { if(writes==4) adapter.set_reference(false,69); };
    play_cue(job);
    CHECK(writes==4 && drains==0 && drops==2 && pending.empty());
    adapter.set_reference(true,69);
    CHECK(cancelled(job)); // stale same-note job remains invalid
    adapter.play(Cue::ReferenceTone,25,69);
    CHECK(k_msgq_get(&audio_queue,&job,0)==0 && !cancelled(job));

    reset_trace();
    play_cue(job);
    const std::size_t lead_samples = 3 * 128 * 2;
    CHECK(transmitted.size() == lead_samples + 2 * kReferenceFrames);
    if (transmitted.size() == lead_samples + 2 * kReferenceFrames) {
        CHECK(std::all_of(transmitted.begin(), transmitted.begin() + lead_samples, [](int x){return x==0;}));
        std::vector<std::int16_t> expected(kReferenceFrames * 2);
        render_reference(69,0,expected.data(),kReferenceFrames,25);
        CHECK(std::equal(expected.begin(),expected.end(),transmitted.begin() + lead_samples));
    }
    CHECK(starts==1 && drains==1 && drops==1 && pending.empty());

    reset_trace();
    hold_drain = true;
    during_wait = [&] { if (draining) adapter.set_owner(Owner::Timer); };
    play_cue(job);
    CHECK(drains==1 && drops==2 && pending.empty() && cancelled(job) && adapter.ready());

    adapter.set_reference(false,69);
    adapter.set_owner(Owner::Feature);
    for (unsigned i=0; i<6; ++i) adapter.play(Cue::Interact,25);
    CHECK(k_msgq_num_used_get(&audio_queue)==4);
    adapter.set_metronome_running(true);
    adapter.play(Cue::MetronomeClick,25);
    CHECK(k_msgq_num_used_get(&audio_queue)==4);
    adapter.play(Cue::TimerComplete,25);
    CHECK(k_msgq_num_used_get(&audio_queue)==1);
    CHECK(k_msgq_get(&audio_queue,&job,0)==0 && job.cue==Cue::TimerComplete);
    adapter.set_muted(true);
    CHECK(cancelled(job));
    adapter.set_muted(false);

    reset_trace();
    fail_start = true;
    play_cue(job);
    CHECK(writes==2 && starts==1 && drains==0 && drops==2 && pending.empty());

    reset_trace();
    fail_write_at = 4;
    play_cue(job);
    CHECK(writes==4 && drains==0 && drops==2 && pending.empty());

    reset_trace();
    fail_drain = true;
    play_cue(job);
    CHECK(drains==1 && drops==2 && pending.empty());

    reset_trace();
    hold_running = true;
    play_cue(job);
    CHECK(writes==4 && drains==0 && drops==2 && pending.empty());

    reset_trace();
    hold_drain = true;
    const auto before = fake_now;
    play_cue(job);
    CHECK(drains==1 && drops==2 && fake_now-before==150 && pending.empty() && adapter.ready());

    // A preceding error can leave a queued block for DROP to release.
    reset_trace();
    void *stale = nullptr;
    CHECK(k_mem_slab_alloc(&audio_slab,&stale,K_NO_WAIT)==0);
    pending.push_back(stale);
    play_cue(job);
    CHECK(starts==1 && drains==1 && drops==1 && pending.empty() && adapter.ready());
    play_cue(job);
    CHECK(starts==2 && drains==2 && drops==2 && pending.empty());

    reset_trace();
    CHECK(k_mem_slab_alloc(&audio_slab,&stale,K_NO_WAIT)==0);
    pending.push_back(stale);
    hold_drop = true;
    play_cue(job);
    CHECK(drops==1 && writes==0 && !adapter.ready());
    while (!pending.empty()) release_one();
    if (failures) return EXIT_FAILURE;
    std::cout << "Audio worker clock, DMA ownership, cancellation, saturation and recovery models passed\n";
}
