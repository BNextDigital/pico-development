// Included inside the existing host-test namespace; uses its CHECK harness.
void test_timer_service_pause_resume_and_wrap()
{
    pico::Timer timer;
    CHECK(!timer.start(0, 0));
    CHECK(timer.start(10, 100));
    CHECK(!timer.advance(103));
    CHECK(timer.remaining() == 7);
    CHECK(!timer.pause(105));
    CHECK(timer.paused() && timer.remaining() == 5);
    CHECK(!timer.advance(500));
    CHECK(timer.remaining() == 5);
    CHECK(timer.resume(500));
    CHECK(!timer.advance(504));
    CHECK(timer.advance(505));
    CHECK(!timer.advance(506));
    CHECK(!timer.resume(506));
    CHECK(timer.start(10, UINT32_MAX - 4U));
    CHECK(!timer.advance(0));
    CHECK(timer.remaining() == 5);
    CHECK(timer.advance(5));
    timer.cancel();
    CHECK(!timer.running() && !timer.paused() && timer.total() == 0);
}

void test_personality_is_deterministic_and_quiet()
{
    pico::Core a(42), b(42), other(43);
    a.handle({pico::EventKind::Boot, 0});
    b.handle({pico::EventKind::Boot, 0});
    other.handle({pico::EventKind::Boot, 0});
    bool blink = false, curiosity = false, seed_difference = false;
    unsigned frames = 0;
    for (std::uint32_t ms = 100; ms <= 300000; ms += 100) {
        const auto effect = a.handle({pico::EventKind::AnimationTick, ms});
        b.handle({pico::EventKind::AnimationTick, ms});
        other.handle({pico::EventKind::AnimationTick, ms});
        CHECK(a.state().mood == b.state().mood);
        CHECK(a.state().eyes_closed == b.state().eyes_closed);
        CHECK(a.state().gaze_x == b.state().gaze_x);
        CHECK(!pico::has_effect(effect, pico::Effect::Persist));
        CHECK(!pico::has_effect(effect, pico::Effect::Chirp));
        frames += pico::has_effect(effect, pico::Effect::Render) ? 1 : 0;
        blink |= a.state().eyes_closed;
        curiosity |= a.state().mood == pico::Mood::Curious;
        seed_difference |= a.state().eyes_closed != other.state().eyes_closed;
    }
    CHECK(blink && curiosity && seed_difference);
    CHECK(frames < 200); // 3000 animation samples, only changed visuals rendered
    CHECK(a.state().mood == pico::Mood::Sleepy);
    a.handle({pico::EventKind::PrimaryPressed, 0});
    CHECK(a.state().mood == pico::Mood::Happy);
    a.handle({pico::EventKind::AnimationTick, 304000});
    CHECK(a.state().mood == pico::Mood::Neutral);
}

void test_navigation_and_utility()
{
    pico::Core core;
    core.handle({pico::EventKind::Boot, 0});
    core.handle({pico::EventKind::NavigateNext, 0}); // optional D1
    CHECK(core.state().screen == pico::Screen::Menu);
    core.handle({pico::EventKind::FocusButtonTapped, 0}); // D0 selects Focus
    CHECK(core.state().screen == pico::Screen::Utility);
    core.handle({pico::EventKind::SetTimerDuration, 10});
    core.handle({pico::EventKind::FocusButtonTapped, 0}); // default Start item
    CHECK(core.state().timer_remaining_seconds == 10);
    core.handle({pico::EventKind::Tick, 3});
    core.handle({pico::EventKind::Select, 0}); // pause
    CHECK(core.state().timer_paused && !core.state().timer_running);
    core.handle({pico::EventKind::Tick, 100});
    CHECK(core.state().timer_remaining_seconds == 7);
    core.handle({pico::EventKind::Select, 0}); // resume
    core.handle({pico::EventKind::Tick, 107});
    CHECK(core.state().mood == pico::Mood::Alert && core.state().timer_completed);
    core.handle({pico::EventKind::Tick, 111});
    CHECK(core.state().screen == pico::Screen::Home);
    CHECK(!core.state().timer_completed);
    core.handle({pico::EventKind::OpenMenu, 0});
    for (unsigned i = 0; i < 3; ++i) core.handle({pico::EventKind::NavigateNext, 0});
    core.handle({pico::EventKind::Select, 0});
    CHECK(core.state().screen == pico::Screen::Music);
    core.handle({pico::EventKind::Back, 0});
    CHECK(core.state().screen == pico::Screen::Menu);
    for (unsigned i = 0; i < 3; ++i) core.handle({pico::EventKind::NavigateNext, 0});
    core.handle({pico::EventKind::Select, 0});
    CHECK(core.state().screen == pico::Screen::Themes);
    core.handle({pico::EventKind::NavigatePrevious, 0});
    CHECK(core.state().theme == pico::FaceTheme::KawaiiMascot);
    core.handle({pico::EventKind::Select, 0});
    CHECK(core.state().screen == pico::Screen::Home);
    core.handle({pico::EventKind::OpenMenu, 0});
    CHECK(core.state().menu_index == 0); // explicit BLE OpenMenu has a predictable start
}

void test_all_theme_frames_and_clipping()
{
    using Frame = std::array<std::uint8_t, pico::display::kDefaultBufferSize>;
    std::array<Frame, 8> neutral{};
    for (unsigned theme = 0; theme < 8; ++theme) {
        for (unsigned mood = 0; mood < 5; ++mood) {
            pico::State state{};
            state.theme = static_cast<pico::FaceTheme>(theme);
            state.mood = static_cast<pico::Mood>(mood);
            Frame frame{}, tiled{};
            pico::display::MonoSurface h{frame.data(),frame.size(),128,64,pico::display::MonoLayout::HorizontalMsb};
            pico::display::MonoSurface v{tiled.data(),tiled.size(),128,64,pico::display::MonoLayout::VerticalMsb};
            CHECK(pico::display::render(state, h));
            CHECK(pico::display::render(state, v));
            unsigned lit = 0;
            for (unsigned y = 0; y < 64; ++y) for (unsigned x = 0; x < 128; ++x) {
                CHECK(pico::display::pixel_at(h,x,y) == pico::display::pixel_at(v,x,y));
                lit += pico::display::pixel_at(h,x,y) ? 1 : 0;
            }
            CHECK(lit > 10 && lit < 1500);
            if (mood == 0) neutral[theme] = frame;
            else CHECK(frame != neutral[theme]);
        }
    }
    for (unsigned a = 0; a < 8; ++a) for (unsigned b = a+1; b < 8; ++b)
        CHECK(neutral[a] != neutral[b]);
    // Guard bytes on an odd-sized small surface catch signed crop / stride errors.
    std::array<std::uint8_t, 302> guarded{};
    guarded.fill(0xa5);
    pico::display::MonoSurface small{guarded.data()+1,300,65,33,pico::display::MonoLayout::HorizontalMsb};
    CHECK(pico::display::render({},small));
    CHECK(guarded.front() == 0xa5 && guarded.back() == 0xa5);
}

void test_sprint_protocol_and_persistence()
{
    pico::Core core;
    for (const auto byte : {0x08,0x09,0x10,0x11,0x12,0x13,0x14}) {
        const std::uint8_t packet[]{static_cast<std::uint8_t>(byte)};
        CHECK(pico::protocol::decode_command(packet,1).status == pico::protocol::DecodeStatus::Ok);
        const std::uint8_t extra[]{static_cast<std::uint8_t>(byte),0};
        CHECK(pico::protocol::decode_command(extra,2).status == pico::protocol::DecodeStatus::WrongLength);
    }
    const std::uint8_t theme[]{0x07,7}, invalid_theme[]{0x07,8};
    CHECK(pico::protocol::decode_command(theme,2).event.kind == pico::EventKind::SetTheme);
    CHECK(pico::protocol::decode_command(invalid_theme,2).status == pico::protocol::DecodeStatus::InvalidValue);
    core.handle({pico::EventKind::SetTheme,7});
    core.handle({pico::EventKind::SetTimerDuration,300});
    core.handle({pico::EventKind::StartTimer,10});
    core.handle({pico::EventKind::PauseTimer,0});
    std::uint8_t status[pico::protocol::kStatusSize]{};
    pico::protocol::encode_status(core.state(),0,status);
    CHECK(status[0] == 1 && (status[4] & 4) && !(status[4] & 2));
    const auto blob = pico::persistence::encode(core.persistent_state());
    CHECK(blob.size() == 28 && blob[4] == 4);
    pico::PersistedState saved{};
    CHECK(pico::persistence::decode(blob,saved));
    CHECK(saved.theme == pico::FaceTheme::KawaiiMascot && saved.configured_duration_seconds == 300);
    pico::Core restored;
    CHECK(restored.restore(saved));
    restored.handle({pico::EventKind::Boot,0});
    CHECK(restored.state().mode == pico::Mode::Companion);
    CHECK(restored.state().theme == pico::FaceTheme::KawaiiMascot);
    CHECK(!restored.state().timer_running && !restored.state().timer_paused);
    CHECK(!pico::persistence::decode(blob.data(),19,saved));
    const std::array<std::uint8_t,16> v2{
        0x50,0x49,0x43,0x4f,0x02,0x01,0x02,0x07,0x2a,0,0,0,0x08,0xbf,0x3b,0x8f};
    CHECK(pico::persistence::decode(v2.data(),v2.size(),saved));
    CHECK(saved.theme == pico::FaceTheme::KawaiiMascot);
    CHECK(saved.configured_duration_seconds == 1500 && saved.interaction_count == 42);
    CHECK(!pico::persistence::decode(v2.data(),v2.size()-1,saved));
    saved.configured_duration_seconds = 0;
    CHECK(!pico::persistence::decode(pico::persistence::encode(saved),saved));
}

void run_sprint01_tests()
{
    test_timer_service_pause_resume_and_wrap();
    test_personality_is_deterministic_and_quiet();
    test_navigation_and_utility();
    test_all_theme_frames_and_clipping();
    test_sprint_protocol_and_persistence();
}
