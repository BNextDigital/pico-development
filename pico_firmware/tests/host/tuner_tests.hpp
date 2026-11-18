// Included inside the host test namespace.
std::array<std::int16_t,pico::pitch::kFrameSamples> tuner_tone(double hz,double cents=0,double harmonic=0,double dc=0)
{
    std::array<std::int16_t,pico::pitch::kFrameSamples> samples{};
    hz *= std::pow(2.0,cents/1200.0);
    for (std::size_t i=0; i<samples.size(); ++i) {
        const auto phase = 6.283185307179586 * hz * static_cast<double>(i) / pico::pitch::kSampleRate;
        samples[i] = static_cast<std::int16_t>(dc + 4500.0*std::sin(phase) + harmonic*4500.0*std::sin(2*phase));
    }
    return samples;
}
pico::pitch::Reading detected(pico::pitch::Detector &detector,const std::int16_t *pcm)
{
    detector.analyze(pcm,pico::pitch::kFrameSamples);
    detector.analyze(pcm,pico::pitch::kFrameSamples);
    return detector.analyze(pcm,pico::pitch::kFrameSamples);
}
void test_pitch_notes_and_detuning()
{
    struct Fixture { double hz; unsigned note; };
    for (const auto &f : {Fixture{41.2034446,28}, {55.0,33}, {61.7354,35}, {65.4064,36},
                         {82.4069,40}, {110.0,45}, {146.8324,50}, {196.0,55}, {246.9417,59},
                         {329.6276,64}, {440.0,69}, {659.2551,76}, {880.0,81}, {1318.5102,88}}) {
        for (double cents : {-30.0,0.0,30.0}) {
            pico::pitch::Detector detector;
            const auto samples = tuner_tone(f.hz,cents);
            const auto reading = detected(detector,samples.data());
            CHECK(reading.signal == pico::pitch::Signal::Detected);
            CHECK(reading.note == f.note);
            if (std::abs(static_cast<int>(reading.cents)-static_cast<int>(cents)) > 5)
                std::cerr << "Pitch error: " << f.hz << " Hz, " << cents << " cents -> " << static_cast<int>(reading.cents) << " cents\n";
            CHECK(std::abs(static_cast<int>(reading.cents)-static_cast<int>(cents)) <= 5);
        }
    }
    for (unsigned note=26; note<=88; ++note) {
        pico::pitch::Detector chromatic;
        const auto pcm = tuner_tone(440.0*std::pow(2.0,(static_cast<int>(note)-69)/12.0));
        const auto result = detected(chromatic,pcm.data());
        CHECK(result.signal == pico::pitch::Signal::Detected && result.note == note);
        CHECK(std::abs(result.cents) <= 5);
    }
    pico::pitch::Detector detector;
    auto samples = tuner_tone(82.4069,15,2.5,3000);
    const auto reading = detected(detector,samples.data());
    CHECK(reading.signal == pico::pitch::Signal::Detected && reading.note == 40);
    CHECK(std::abs(reading.cents-15) <= 5);
    auto decaying = tuner_tone(110,-20);
    std::uint32_t random = 9;
    for (std::size_t i=0; i<decaying.size(); ++i) {
        random = random*1664525U+1013904223U;
        decaying[i] = static_cast<std::int16_t>(decaying[i]*(1.0-0.25*static_cast<double>(i)/decaying.size()) +
                                               static_cast<int>(random>>24U)-128);
    }
    detector.reset();
    const auto decay = detected(detector,decaying.data());
    CHECK(decay.signal == pico::pitch::Signal::Detected && decay.note == 45);
    CHECK(std::abs(decay.cents+20) <= 5);
}
void test_pitch_rejects_silence_noise_clipping_and_stale_notes()
{
    pico::pitch::Detector detector;
    auto samples = tuner_tone(440);
    CHECK(detected(detector,samples.data()).signal == pico::pitch::Signal::Detected);
    samples.fill(4000); // DC is not a note or usable signal.
    CHECK(detector.analyze(samples.data(),samples.size()).signal == pico::pitch::Signal::Quiet);
    samples = tuner_tone(440);
    CHECK(detector.analyze(samples.data(),samples.size()).signal == pico::pitch::Signal::Listening);
    samples.fill(INT16_MAX);
    CHECK(detector.analyze(samples.data(),samples.size()).signal == pico::pitch::Signal::Clipping);
    std::uint32_t noise=42;
    for (auto &s:samples) { noise=noise*1664525U+1013904223U; s=static_cast<std::int16_t>(static_cast<int>(noise>>16U)/4-8192); }
    CHECK(detector.analyze(samples.data(),samples.size()).signal == pico::pitch::Signal::Noisy);
    samples=tuner_tone(2000);
    CHECK(detector.analyze(samples.data(),samples.size()).signal == pico::pitch::Signal::Noisy);
    CHECK(detector.analyze(nullptr,0).signal == pico::pitch::Signal::Noisy);
    CHECK(detector.analyze(samples.data(),samples.size()-1).signal == pico::pitch::Signal::Noisy);
}
void test_tuner_lifecycle_and_session_results()
{
    using E=pico::EventKind;
    pico::Core core;
    core.handle({E::StartTuner,0});
    CHECK(core.state().screen == pico::Screen::Tuner && !core.state().tuner_running);
    CHECK(core.state().tuner.signal == pico::pitch::Signal::Unavailable);
    core.handle({E::MicrophoneReady,1});
    core.handle({E::StartMetronome,0});
    core.handle({E::StartTuner,0});
    CHECK(core.state().tuner_running && !core.state().metronome_running);
    const auto old_session=core.state().tuner_session;
    const pico::pitch::Reading reading{pico::pitch::Signal::Detected,69,12,40};
    const auto result=core.handle({E::TunerReading,pico::pitch::pack(reading,old_session)});
    CHECK(core.state().tuner.note == 69 && core.state().tuner.cents == 12);
    CHECK(pico::has_effect(result,pico::Effect::Render));
    CHECK(!pico::has_effect(result,pico::Effect::Persist) && !pico::has_effect(result,pico::Effect::Notify));
    CHECK(!pico::has_effect(core.handle({E::TestSound,1}),pico::Effect::Chirp));
    core.handle({E::Back,0});
    CHECK(!core.state().tuner_running && core.state().screen == pico::Screen::Menu);
    core.handle({E::StartTuner,0});
    core.handle({E::TunerReading,pico::pitch::pack(reading,old_session)});
    CHECK(core.state().tuner.signal == pico::pitch::Signal::Listening);
    core.handle({E::MicrophoneReady,0});
    CHECK(!core.state().tuner_running && core.state().tuner.signal == pico::pitch::Signal::Unavailable);
    core.handle({E::MicrophoneReady,1}); core.handle({E::StartTuner,0});
    core.handle({E::StartTimer,1}); core.handle({E::Tick,1});
    CHECK(!core.state().tuner_running && core.state().timer_completed);
    core.handle({E::StartTuner,0});
    core.handle({E::SetMode,0});
    CHECK(!core.state().tuner_running && core.state().screen == pico::Screen::Home);
    core.handle({E::StartTuner,0});
    core.handle({E::DockChanged,static_cast<unsigned>(pico::Dock::Home)});
    CHECK(!core.state().tuner_running);
    core.handle({E::DockChanged,static_cast<unsigned>(pico::Dock::Car)});
    core.handle({E::StartTuner,0}); CHECK(!core.state().tuner_running);
}
void test_tuner_packet_validation_and_rendering()
{
    pico::pitch::Reading reading; std::uint8_t session;
    CHECK(!pico::pitch::unpack(UINT32_MAX,reading,session));
    for (int cents=-50; cents<=50; ++cents) {
        const pico::pitch::Reading expected{pico::pitch::Signal::Detected,88,static_cast<std::int8_t>(cents),100};
        CHECK(pico::pitch::unpack(pico::pitch::pack(expected,254),reading,session));
        CHECK(reading.cents == cents && reading.level == 100 && session == 254);
    }
    pico::Core core;
    core.handle({pico::EventKind::MicrophoneReady,1}); core.handle({pico::EventKind::StartTuner,0});
    std::uint8_t status[pico::protocol::kStatusSize];
    pico::protocol::encode_status(core.state(),0,status);
    CHECK((status[4]&64) != 0 && status[0] == 1);
    for (unsigned id : {0x35U,0x36U}) {
        const std::uint8_t command[]{static_cast<std::uint8_t>(id),0};
        CHECK(pico::protocol::decode_command(command,1).status == pico::protocol::DecodeStatus::Ok);
        CHECK(pico::protocol::decode_command(command,2).status == pico::protocol::DecodeStatus::WrongLength);
    }
    std::array<std::uint8_t,1026> frame{}; frame.fill(0xa5);
    pico::display::MonoSurface surface{frame.data()+1,1024,128,64,pico::display::MonoLayout::HorizontalMsb};
    for (unsigned signal=0; signal<=5; ++signal) {
        auto state=core.state(); state.tuner.signal=static_cast<pico::pitch::Signal>(signal);
        state.tuner.cents=-50; state.tuner.note=61;
        CHECK(pico::display::render(state,surface));
        CHECK(frame.front() == 0xa5 && frame.back() == 0xa5);
    }
}
void run_tuner_tests()
{
    test_pitch_notes_and_detuning();
    test_pitch_rejects_silence_noise_clipping_and_stale_notes();
    test_tuner_lifecycle_and_session_results();
    test_tuner_packet_validation_and_rendering();
}
