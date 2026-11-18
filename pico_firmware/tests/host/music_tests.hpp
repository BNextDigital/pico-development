// Included inside the host test namespace.
void test_metronome_fractional_phase_and_wrap()
{
    pico::Metronome metro;
    CHECK(!metro.set_bpm(29) && !metro.set_bpm(241));
    CHECK(!metro.set_beats(0) && !metro.set_beats(9));
    CHECK(metro.set_bpm(137));
    CHECK(metro.start(0));
    CHECK(!metro.start(100)); // duplicate start must not restart phase
    CHECK(!metro.advance(437));
    CHECK(metro.advance(438) && metro.beat() == 2);
    CHECK(metro.pulse());
    CHECK(!metro.advance(600) && !metro.pulse());
    CHECK(metro.advance(3600000)); // exactly 8220 beats, no rounded-ms drift
    CHECK(metro.beat() == 1 && metro.pulse());
    CHECK(!metro.advance(3600000));
    metro.stop();
    CHECK(!metro.pulse() && !metro.advance(3601000));
    CHECK(metro.set_bpm(120));
    CHECK(metro.start(UINT32_MAX - 249U));
    CHECK(!metro.advance(249)); // 499 ms across wrap
    CHECK(metro.advance(250) && metro.beat() == 2);
    CHECK(metro.set_beats(1));
    CHECK(metro.advance(750) && metro.beat() == 1);
}
void test_tap_tempo_average_bounce_and_timeout()
{
    pico::Metronome metro;
    CHECK(!metro.tap(100));
    CHECK(!metro.tap(200)); // ignored, keeps 100 as previous accepted tap
    CHECK(metro.tap(600) && metro.bpm() == 120);
    CHECK(metro.tap(1200) && metro.bpm() == 109); // 60k / 550, rounded
    CHECK(metro.tap(1700));
    CHECK(metro.tap(2200));
    CHECK(metro.tap(2700) && metro.bpm() == 114); // rolling last four intervals
    CHECK(!metro.tap(5000)); // long gap starts a new series
    CHECK(metro.tap(7000) && metro.bpm() == 30);
    CHECK(metro.tap(7250) && metro.bpm() == 53);
    pico::Metronome wrap;
    CHECK(!wrap.tap(UINT32_MAX - 249));
    CHECK(wrap.tap(250) && wrap.bpm() == 120);
}
void test_music_menu_controls_and_cues()
{
    using E = pico::EventKind;
    pico::Core core;
    core.handle({E::Boot,0}); core.handle({E::OpenMenu,0});
    for (unsigned i=0; i<3; ++i) core.handle({E::NavigateNext,0});
    core.handle({E::FocusButtonTapped,0});
    CHECK(core.state().screen == pico::Screen::Music);
    CHECK(core.state().music_index == 2);
    core.handle({E::MusicTick,100});
    const auto start = core.handle({E::Select,0});
    CHECK(core.state().metronome_running && core.state().beat_pulse);
    CHECK(pico::has_effect(start,pico::Effect::Chirp));
    CHECK(core.state().sound_cue == pico::audio::Cue::MetronomeAccent);
    const auto off = core.handle({E::MusicTick,180});
    CHECK(!core.state().beat_pulse && pico::has_effect(off,pico::Effect::Render));
    CHECK(!pico::has_effect(off,pico::Effect::Chirp));
    const auto beat = core.handle({E::MusicTick,600});
    CHECK(core.state().current_beat == 2 && core.state().beat_pulse);
    CHECK(core.state().sound_cue == pico::audio::Cue::MetronomeClick);
    CHECK(pico::has_effect(beat,pico::Effect::Chirp));
    CHECK(!pico::has_effect(beat,pico::Effect::Persist) && !pico::has_effect(beat,pico::Effect::Notify));
    core.handle({E::NavigatePrevious,0}); core.handle({E::Select,0});
    CHECK(core.state().metronome_bpm == 125);
    core.handle({E::NavigatePrevious,0}); core.handle({E::Select,0});
    CHECK(core.state().metronome_bpm == 120);
    core.handle({E::NavigatePrevious,0}); // wraps to HOME
    CHECK(core.state().music_index == 6);
    core.handle({E::Select,0});
    CHECK(!core.state().metronome_running && core.state().mode == pico::Mode::Companion);
}
void test_metronome_exit_and_completion_priority()
{
    using E = pico::EventKind;
    pico::Core core;
    core.handle({E::StartMetronome,0});
    core.handle({E::Back,0});
    CHECK(!core.state().metronome_running && core.state().screen == pico::Screen::Menu);
    core.handle({E::StartMetronome,0});
    core.handle({E::SetMode,static_cast<unsigned>(pico::Mode::Cooking)});
    CHECK(!core.state().metronome_running && core.state().screen == pico::Screen::Home);
    core.handle({E::StartMetronome,0});
    core.handle({E::DockChanged,static_cast<unsigned>(pico::Dock::Home)});
    CHECK(!core.state().metronome_running && core.state().screen == pico::Screen::Home);
    core.handle({E::StartMetronome,0});
    core.handle({E::DockChanged,static_cast<unsigned>(pico::Dock::Car)});
    CHECK(!core.state().metronome_running);
    CHECK(!pico::has_effect(core.handle({E::StartMetronome,0}),pico::Effect::Chirp));
    core.handle({E::DockChanged,0}); core.handle({E::StartMetronome,0});
    core.handle({E::StartTimer,1});
    core.handle({E::Tick,1});
    CHECK(!core.state().metronome_running && core.state().timer_completed);
    CHECK(core.state().sound_cue == pico::audio::Cue::TimerComplete);
    core.handle({E::SetMetronomeBpm,99});
    core.handle({E::Boot,0});
    CHECK(!core.state().metronome_running && core.state().metronome_bpm == 120);
    CHECK(core.state().mode == pico::Mode::Companion);
    core.handle({E::OpenMenu,0});
    for (unsigned i=0; i<4; ++i) core.handle({E::NavigateNext,0});
    core.handle({E::Select,0});
    CHECK(core.state().screen == pico::Screen::Unavailable); // Wayfinding still unavailable.
}
void test_music_wire_commands_and_rendering()
{
    using E = pico::EventKind;
    const std::uint8_t tempo[]{0x32,137,0}, bad_tempo[]{0x32,0,1}, bad_bar[]{0x33,0};
    const auto decoded = pico::protocol::decode_command(tempo,3);
    CHECK(decoded.status == pico::protocol::DecodeStatus::Ok && decoded.event.value == 137);
    CHECK(pico::protocol::decode_command(bad_tempo,3).status == pico::protocol::DecodeStatus::InvalidValue);
    CHECK(pico::protocol::decode_command(bad_bar,2).status == pico::protocol::DecodeStatus::InvalidValue);
    CHECK(pico::protocol::decode_command(tempo,2).status == pico::protocol::DecodeStatus::WrongLength);
    pico::Core core;
    core.handle({E::SetMuted,1}); core.handle({E::StartMetronome,0});
    std::uint8_t status[pico::protocol::kStatusSize];
    pico::protocol::encode_status(core.state(),0,status);
    CHECK(status[0] == 1 && (status[4] & 32) && (status[4] & 16));
    std::array<std::uint8_t,1024> pulse{}, off{};
    pico::display::MonoSurface a{pulse.data(),pulse.size(),128,64,pico::display::MonoLayout::HorizontalMsb};
    pico::display::MonoSurface b{off.data(),off.size(),128,64,pico::display::MonoLayout::HorizontalMsb};
    CHECK(pico::display::render(core.state(),a));
    core.handle({E::MusicTick,100});
    CHECK(pico::display::render(core.state(),b));
    CHECK(pulse != off); // visual beat still works when muted / no amplifier
    core.handle({E::StopMetronome,0});
    pico::protocol::encode_status(core.state(),0,status);
    CHECK((status[4] & 32) == 0);
}
void run_music_tests()
{
    test_metronome_fractional_phase_and_wrap();
    test_tap_tempo_average_bounce_and_timeout();
    test_music_menu_controls_and_cues();
    test_metronome_exit_and_completion_priority();
    test_music_wire_commands_and_rendering();
}
