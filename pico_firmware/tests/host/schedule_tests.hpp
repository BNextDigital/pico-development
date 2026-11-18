// Included in the host CHECK namespace.
void test_service_wait_bounds()
{
    using pico::schedule::wait_ms;
    constexpr auto none = UINT32_MAX;
    CHECK(wait_ms(1500, 1, none, none, none, false) == 500);
    CHECK(wait_ms(2000, 1, none, none, none, false) == 0); // tick already due
    CHECK(wait_ms(1500, 1, 150, 80, 40, true) == 20);
    CHECK(wait_ms(1500, 1, 5, 80, 40, true) == 5);
    CHECK(wait_ms(1500, 1, none, none, 40, false) == 40);
    CHECK(wait_ms(1500, 1, 0, none, none, false) == 0);
    const std::uint64_t wrap = UINT32_MAX + 1ULL;
    CHECK(wait_ms(wrap, static_cast<std::uint32_t>(wrap / 1000), none, none, none, false)
          == 1000 - wrap % 1000); // not a new fake second at millisecond wrap
}

void test_music_deadlines_and_wrap()
{
    pico::Metronome music;
    CHECK(music.next_update_ms(100) == UINT32_MAX);
    music.set_bpm(137); music.start(UINT32_MAX - 20U);
    CHECK(music.next_update_ms(UINT32_MAX - 20U) == 80);
    music.advance(59); // 80 ms elapsed across wrap
    CHECK(!music.pulse());
    CHECK(music.next_update_ms(59) == 358);
    CHECK(music.next_update_ms(417) == 0);
    CHECK(music.advance(417));
    CHECK(music.next_update_ms(417) == 80);
    music.stop(); CHECK(music.next_update_ms(418) == UINT32_MAX);
}

void test_personality_deadlines_and_wrap()
{
    pico::Personality personality(42);
    pico::State state;
    const auto start = UINT32_MAX - 100U;
    personality.activity(state, start);
    personality.react(state, pico::Mood::Happy, start, 150);
    CHECK(personality.next_update_ms(state, start) == 150);
    CHECK(personality.next_update_ms(state, 49) == 0);
    personality.advance(state, 49);
    CHECK(state.mood == pico::Mood::Neutral);
    CHECK(personality.next_update_ms(state, 49) > 0);
    state.screen = pico::Screen::Menu;
    CHECK(personality.next_update_ms(state, 10000) == UINT32_MAX);
    state.screen = pico::Screen::Home;
    personality.rest(state);
    CHECK(personality.next_update_ms(state, 10000) == UINT32_MAX);
}

void test_scheduled_core_matches_millisecond_reference()
{
    // Compare every visible state transition and its exact timestamp to a 1 ms
    // reference. The scheduled run has no periodic animation/music sampler.
    using Snapshot = std::array<std::uint32_t, 9>;
    for (unsigned scenario = 0; scenario < 3; ++scenario) {
        std::vector<Snapshot> reference;
        unsigned scheduled_wakes = 0;
        for (unsigned scheduled = 0; scheduled < 2; ++scheduled) {
            pico::Core core(42);
            core.handle({pico::EventKind::Boot, 0});
            if (scenario == 1) {
                core.handle({pico::EventKind::SetMetronomeBpm, 137});
                core.handle({pico::EventKind::StartMetronome, 0});
            }
            if (scenario == 2) {
                core.handle({pico::EventKind::StartTimer, 3});
                core.handle({pico::EventKind::OpenMenu, 0});
            }
            const unsigned end = scenario == 0 ? 301000 : 10000;
            std::uint64_t now = 0;
            std::uint32_t last_tick = UINT32_MAX;
            std::vector<Snapshot> actual;
            Snapshot previous{};
            while (now <= end) {
                const auto ms = static_cast<std::uint32_t>(now);
                if (now / 1000 != last_tick) {
                    last_tick = static_cast<std::uint32_t>(now / 1000);
                    core.handle({pico::EventKind::Tick, last_tick});
                }
                core.handle({pico::EventKind::MusicTick, ms});
                core.handle({pico::EventKind::AnimationTick, ms});
                const auto &s = core.state();
                Snapshot snapshot{ms, static_cast<unsigned>(s.mood), s.eyes_closed,
                    static_cast<std::uint32_t>(s.gaze_x + 1), s.beat_pulse,
                    s.current_beat, s.timer_remaining_seconds, s.timer_completed,
                    static_cast<unsigned>(s.screen)};
                auto comparable = snapshot; comparable[0] = 0;
                if (now == 0 || comparable != previous) actual.push_back(snapshot);
                previous = comparable;
                if (scheduled) ++scheduled_wakes;
                const auto delay = scheduled ? pico::schedule::wait_ms(now, last_tick,
                    core.next_animation_ms(ms), core.next_music_ms(ms), UINT32_MAX, false) : 1U;
                CHECK(delay > 0 && delay <= 1000);
                if (delay == 0) break; // prevent a failing test from spinning
                now += delay;
            }
            if (!scheduled) reference = actual;
            else CHECK(reference == actual);
            if (scenario == 0) CHECK(core.state().mood == pico::Mood::Sleepy);
            if (scenario == 2) CHECK(!core.state().timer_completed);
        }
        if (scenario == 0) {
            CHECK(scheduled_wakes < 600); // formerly ~15050 20 ms wakes
            std::cout << "Deadline model: " << scheduled_wakes
                      << " service passes over 301 seconds (motion/tuner off)\n";
        }
    }
}

void run_schedule_tests()
{
    test_service_wait_bounds();
    test_music_deadlines_and_wrap();
    test_personality_deadlines_and_wrap();
    test_scheduled_core_matches_millisecond_reference();
}
