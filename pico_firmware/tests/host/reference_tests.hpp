void test_reference_pcm_frequency_bounds_and_chunks()
{
    using namespace pico::audio;
    CHECK(frequency_millihz(69) == 440000);
    CHECK(frequency_millihz(40) == 82407 && frequency_millihz(84) == 1046502);
    CHECK(frequency_millihz(39) == 0 && frequency_millihz(85) == 0);
    for (const auto note : {40,69,84}) {
        std::vector<std::int16_t> full((kReferenceFrames+17)*2), chunks(full.size());
        render_reference(note,0,full.data(),kReferenceFrames+17,100);
        for (unsigned first=0; first<kReferenceFrames+17; first+=257) {
            const auto count=std::min(257U,kReferenceFrames+17-first);
            render_reference(note,first,chunks.data()+first*2,count,100);
        }
        CHECK(full == chunks);
        unsigned crossings=0;
        for (unsigned frame=1; frame<kReferenceFrames; ++frame) {
            CHECK(full[frame*2] == full[frame*2+1]);
            CHECK(std::abs(static_cast<int>(full[frame*2])) <= kPeakLimit);
            if (frame > 3125 && frame <= 3125+kSampleRate && full[(frame-1)*2] <= 0 && full[frame*2] > 0) ++crossings;
        }
        CHECK(std::abs(static_cast<double>(crossings)-frequency_millihz(note)/1000.0) < 1.1);
        CHECK(full[0] == 0 && full[(kReferenceFrames-1)*2] == 0 && full[kReferenceFrames*2] == 0);
    }
    std::array<std::int16_t,512> silent{};
    render_reference(39,100,silent.data(),256,100);
    CHECK(std::all_of(silent.begin(),silent.end(),[](auto x){return x==0;}));
    render_reference(69,100,silent.data(),256,0);
    CHECK(std::all_of(silent.begin(),silent.end(),[](auto x){return x==0;}));
    render_reference(69,UINT32_MAX,silent.data(),256,100);
    CHECK(std::all_of(silent.begin(),silent.end(),[](auto x){return x==0;}));
}
void test_reference_lifecycle_and_priority()
{
    using E=pico::EventKind;
    pico::Core c; c.handle({E::Boot,0}); c.handle({E::OpenReference,0});
    CHECK(c.state().screen == pico::Screen::ReferenceTone);
    CHECK(!pico::has_effect(c.handle({E::PlayReference,0}),pico::Effect::Chirp));
    c.handle({E::AudioReady,1});
    c.handle({E::MusicTick,UINT32_MAX-100U});
    CHECK(pico::has_effect(c.handle({E::PlayReference,0}),pico::Effect::Chirp));
    CHECK(c.next_music_ms(UINT32_MAX-100U) == 2000);
    c.handle({E::MusicTick,1898}); CHECK(c.state().reference_playing);
    c.handle({E::MusicTick,1899}); CHECK(!c.state().reference_playing);
    c.handle({E::PlayReference,0}); c.handle({E::SetReferenceNote,40}); CHECK(!c.state().reference_playing);
    CHECK(c.handle({E::SetReferenceNote,39}) == pico::effect(pico::Effect::None));
    c.handle({E::PlayReference,0}); c.handle({E::SetMuted,1}); CHECK(!c.state().reference_playing);
    CHECK(!pico::has_effect(c.handle({E::PlayReference,0}),pico::Effect::Chirp));
    c.handle({E::SetMuted,0}); c.handle({E::PlayReference,0});
    c.handle({E::SetVolume,0}); CHECK(!c.state().reference_playing);
    c.handle({E::SetVolume,25}); c.handle({E::PlayReference,0});
    c.handle({E::SleepDisplay,0}); CHECK(!c.state().display_asleep);
    c.handle({E::StartTimer,1}); c.handle({E::Tick,1});
    CHECK(!c.state().reference_playing && c.state().timer_completed);
    CHECK(!pico::has_effect(c.handle({E::PlayReference,0}),pico::Effect::Chirp));
    c.handle({E::Tick,5}); c.handle({E::PlayReference,0});
    c.handle({E::MicrophoneReady,1}); c.handle({E::StartTuner,0});
    CHECK(!c.state().reference_playing && c.state().tuner_running);
    c.handle({E::OpenReference,0}); CHECK(!c.state().tuner_running);
    c.handle({E::PlayReference,0}); c.handle({E::Back,0});
    CHECK(!c.state().reference_playing && c.state().screen == pico::Screen::Music);
    c.handle({E::PlayReference,0}); c.handle({E::AudioReady,0}); CHECK(!c.state().reference_playing);
    c.handle({E::AudioReady,1}); c.handle({E::PlayReference,0}); c.handle({E::OpenSettings,0});
    CHECK(!c.state().reference_playing);
    pico::Core restored; CHECK(restored.restore(c.persistent_state())); restored.handle({E::Boot,0});
    CHECK(!restored.state().reference_playing && restored.state().reference_note == 69);
}
