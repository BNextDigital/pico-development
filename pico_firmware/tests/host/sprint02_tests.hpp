// Included inside the host test namespace.
void test_sound_pcm_is_bounded_and_chunk_independent()
{
    using namespace pico::audio;
    constexpr unsigned expected_frames[]{2857, 10236, 14283, 10950, 571, 761, 95238};
    for (unsigned cue = 0; cue < static_cast<unsigned>(pico::audio::Cue::Count); ++cue) {
        const auto kind = static_cast<pico::audio::Cue>(cue);
        const auto frames = pico::audio::frames(kind);
        CHECK(frames == expected_frames[cue]);
        std::vector<std::int16_t> full((frames + 10) * 2), chunks(full.size());
        pico::audio::render(kind, 0, full.data(), frames + 10, 100);
        for (unsigned offset = 0; offset < frames + 10; offset += 128) {
            const auto count = std::min(128U, frames + 10 - offset);
            pico::audio::render(kind, offset, chunks.data() + offset * 2, count, 100);
        }
        CHECK(full == chunks);
        bool audible = false;
        for (unsigned i = 0; i < frames + 10; ++i) {
            CHECK(full[2*i] == full[2*i+1]);
            CHECK(full[2*i] >= -pico::audio::kPeakLimit && full[2*i] <= pico::audio::kPeakLimit);
            if (full[2*i]) audible = true;
            if (i >= frames) CHECK(full[2*i] == 0);
        }
        CHECK(audible);
        CHECK(full[0] == 0 && full[(frames-1)*2] == 0);
        std::int16_t muted[256], clamped[256];
        pico::audio::render(kind, 128, muted, 128, 0);
        pico::audio::render(kind, 128, clamped, 128, 200);
        for (unsigned i=0; i<256; ++i) { CHECK(muted[i] == 0); CHECK(clamped[i] == full[256+i]); }
    }
    std::vector<std::int16_t> interaction(2 * frames(Cue::Interact));
    render(Cue::Interact, 0, interaction.data(), interaction.size()/2, 100);
    unsigned first_crossing=0, last_crossing=0, crossings=0;
    for (unsigned i=frames_for_ms(6); i<frames_for_ms(54); ++i) {
        if (interaction[2*i-2] <= 0 && interaction[2*i] > 0) {
            if (!crossings++) first_crossing=i;
            last_crossing=i;
        }
    }
    CHECK(crossings>35 && last_crossing>first_crossing);
    if (last_crossing>first_crossing)
        CHECK(std::abs((crossings-1) * double(kSampleRate) / (last_crossing-first_crossing) - 880.0) < 1.0);
    std::int16_t half[256], full[256];
    render(Cue::Interact, 256, half, 128, 50);
    render(Cue::Interact, 256, full, 128, 100);
    for (unsigned i = 0; i < 256; ++i) CHECK(std::abs(2 * half[i] - full[i]) <= 1);
    std::int16_t invalid[4]{1,1,1,1};
    pico::audio::render(static_cast<pico::audio::Cue>(255), UINT32_MAX, invalid, 2);
    CHECK(invalid[0] == 0 && invalid[3] == 0);
    CHECK(pico::audio::frames(pico::audio::Cue::Count) == 0);
}
void test_focus_cycle_waits_for_confirmation_and_reuses_timer()
{
    using E = pico::EventKind;
    pico::Core core;
    core.handle({E::Boot, 0});
    core.handle({E::SetTimerDuration, 10});
    core.handle({E::SetBreakDuration, 3});
    core.handle({E::StartFocusCycle, 0});
    CHECK(core.state().focus_phase == pico::FocusPhase::Work);
    CHECK(core.state().timer_remaining_seconds == 10);
    core.handle({E::Tick, 4});
    core.handle({E::PauseTimer, 0});
    core.handle({E::Tick, 100});
    CHECK(core.state().timer_remaining_seconds == 6 && core.state().timer_paused);
    core.handle({E::ResumeTimer, 0});
    const auto done = core.handle({E::Tick, 106});
    CHECK(pico::has_effect(done, pico::Effect::Chirp));
    CHECK(core.state().sound_cue == pico::audio::Cue::WorkComplete);
    CHECK(core.state().focus_waiting && core.state().completed_work_sessions == 1);
    CHECK(!pico::has_effect(core.handle({E::Tick, 107}), pico::Effect::Chirp));
    core.handle({E::Tick, 200});
    CHECK(!core.state().timer_running && core.state().focus_waiting);
    CHECK(core.state().screen == pico::Screen::Home);
    core.handle({E::NextFocusPhase, 0});
    CHECK(core.state().focus_phase == pico::FocusPhase::Break && !core.state().focus_waiting);
    CHECK(core.state().timer_remaining_seconds == 3);
    core.handle({E::NextFocusPhase, 0}); // cannot skip an active phase
    CHECK(core.state().focus_phase == pico::FocusPhase::Break);
    core.handle({E::Tick, 203});
    CHECK(core.state().sound_cue == pico::audio::Cue::BreakComplete);
    CHECK(core.state().completed_work_sessions == 1);
    core.handle({E::Tick, 207});
    core.handle({E::NextFocusPhase, 0});
    CHECK(core.state().timer_remaining_seconds == 10);
    CHECK(core.state().focus_phase == pico::FocusPhase::Work);
    core.handle({E::CancelTimer, 0});
    CHECK(core.state().focus_phase == pico::FocusPhase::None && !core.state().timer_running);
}
void test_focus_cycle_exits_and_legacy_timer()
{
    using E = pico::EventKind;
    pico::Core core;
    core.handle({E::SetTimerDuration, 1});
    core.handle({E::StartFocusCycle, 0});
    core.handle({E::Tick, 1});
    core.handle({E::CancelTimer, 0}); // cancellation is explicit in the app
    CHECK(!core.state().focus_waiting && core.state().focus_phase == pico::FocusPhase::None);
    CHECK(core.state().mode == pico::Mode::Focus);
    core.handle({E::StartFocusCycle, 0});
    core.handle({E::SetMode, static_cast<unsigned>(pico::Mode::Cooking)});
    CHECK(!core.state().timer_running && core.state().focus_phase == pico::FocusPhase::None);
    core.handle({E::StartFocusCycle, 0});
    core.handle({E::DockChanged, static_cast<unsigned>(pico::Dock::Car)});
    CHECK(!core.state().timer_running && core.state().focus_phase == pico::FocusPhase::None);
    core.handle({E::StartFocusCycle, 0});
    CHECK(!core.state().timer_running);
    core.handle({E::DockChanged, 0});
    core.handle({E::StartFocusCycle, 0});
    core.handle({E::StartTimer, 2}); // original command replaces cycle explicitly
    CHECK(core.state().focus_phase == pico::FocusPhase::None);
    core.handle({E::Tick, 3});
    CHECK(core.state().sound_cue == pico::audio::Cue::TimerComplete);
    CHECK(!core.state().focus_waiting);
    pico::Core reboot;
    CHECK(reboot.restore(core.persistent_state()));
    reboot.handle({E::Boot, 0});
    CHECK(reboot.state().mode == pico::Mode::Companion);
    CHECK(reboot.state().focus_phase == pico::FocusPhase::None);
    CHECK(reboot.state().configured_duration_seconds == 1);
}
void test_session_menu_and_extended_protocol()
{
    using E = pico::EventKind;
    pico::Core core;
    core.handle({E::OpenMenu,0}); core.handle({E::Select,0});
    for (unsigned i=0; i<3; ++i) core.handle({E::NavigateNext,0});
    CHECK(core.state().utility_index == 4);
    core.handle({E::Select,0});
    CHECK(core.state().focus_phase == pico::FocusPhase::Work);
    core.handle({E::SetMode,static_cast<unsigned>(pico::Mode::Cooking)});
    CHECK(core.state().utility_index == 1);
    core.handle({E::Select,0});
    CHECK(core.state().timer_running && core.state().focus_phase == pico::FocusPhase::None);
    core.handle({E::CancelTimer,0});
    core.handle({E::SetMuted,1});
    CHECK(!pico::has_effect(core.handle({E::TestSound,0}), pico::Effect::Chirp));
    CHECK(core.state().sound_muted);
    core.handle({E::SetMuted,0});
    CHECK(pico::has_effect(core.handle({E::TestSound,3}), pico::Effect::Chirp));
    CHECK(core.state().sound_cue == pico::audio::Cue::BreakComplete);
    for (unsigned id : {0x20U,0x21U}) {
        const std::uint8_t cmd[]{static_cast<std::uint8_t>(id)};
        CHECK(pico::protocol::decode_command(cmd,1).status == pico::protocol::DecodeStatus::Ok);
    }
    for (unsigned id : {0x22U,0x23U,0x24U}) {
        const std::uint8_t bad[]{static_cast<std::uint8_t>(id),255,255,255,255};
        CHECK(pico::protocol::decode_command(bad,id == 0x22 ? 5 : 2).status == pico::protocol::DecodeStatus::InvalidValue);
    }
    core.handle({E::SetMuted,1});
    std::uint8_t status[pico::protocol::kStatusSize];
    pico::protocol::encode_status(core.state(),0,status);
    CHECK((status[4] & 16) != 0 && sizeof(status) == 16 && status[0] == 1);
}
void run_sprint02_tests()
{
    test_sound_pcm_is_bounded_and_chunk_independent();
    test_focus_cycle_waits_for_confirmation_and_reuses_timer();
    test_focus_cycle_exits_and_legacy_timer();
    test_session_menu_and_extended_protocol();
}
