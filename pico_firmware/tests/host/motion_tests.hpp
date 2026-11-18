// Included inside the host test namespace.
struct MotionFixture {
    pico::motion::Detector detector;
    std::uint32_t now{0};
    unsigned pickups{0}, rocks{0}, rests{0}, settles{0};
    pico::motion::Signals hold(pico::motion::Sample value,unsigned ms)
    {
        pico::motion::Signals combined=0;
        for (unsigned i=0; i<ms; i+=40) {
            now += 40;
            const auto signals=detector.sample(value,now);
            if (signals & pico::motion::Pickup) ++pickups;
            if (signals & pico::motion::Rock) ++rocks;
            if (signals & pico::motion::Rest) ++rests;
            if (signals & pico::motion::Settled) ++settles;
            combined |= signals;
        }
        return combined;
    }
    void rock()
    {
        hold({500,0,866},240); hold({-500,0,866},240); hold({0,0,1000},240);
    }
};
void test_motion_still_pickup_and_gesture()
{
    MotionFixture fixture;
    fixture.hold({0,0,1000},2200);
    CHECK(fixture.settles == 1 && fixture.pickups == 0 && fixture.rocks == 0);
    fixture.rock();
    CHECK(fixture.pickups == 1 && fixture.rocks == 1);
    fixture.rock();
    CHECK(fixture.rocks == 1); // no re-arm during movement
    fixture.hold({0,0,1000},2200); fixture.rock();
    CHECK(fixture.rocks == 1); // ten-second cooldown
    fixture.hold({0,0,1000},12000); fixture.rock();
    CHECK(fixture.rocks == 2);
    MotionFixture vertical;
    vertical.hold({1000,0,0},2200);
    vertical.hold({866,500,0},240); vertical.hold({866,-500,0},240); vertical.hold({1000,0,0},240);
    CHECK(vertical.rocks == 1); // no fixed flat mounting assumed
}
void test_motion_noise_impacts_gaps_and_wrap()
{
    MotionFixture fixture;
    fixture.hold({0,0,1000},2200);
    fixture.hold({450,0,893},40); fixture.hold({0,0,1000},240);
    CHECK(fixture.pickups == 0 && fixture.rocks == 0); // isolated sample is insufficient
    fixture.hold({0,0,1000},2200);
    fixture.hold({2500,0,1000},40); fixture.hold({0,0,1000},240);
    CHECK(fixture.pickups == 0 && fixture.rocks == 0); // impact cancels arming
    for (unsigned i=0; i<100; ++i) fixture.hold({i%2 ? 600 : -600,0,800},80);
    CHECK(fixture.rocks == 0); // fast walking/shaking trace never qualifies
    MotionFixture gap;
    gap.hold({0,0,1000},2200);
    gap.now += 1000; gap.rock();
    CHECK(gap.rocks == 0 && gap.pickups == 0);
    MotionFixture wrap; wrap.now=UINT32_MAX-1000U;
    wrap.hold({0,0,1000},2200); wrap.rock();
    CHECK(wrap.rocks == 1 && wrap.pickups == 1);
    fixture.detector.sample({INT32_MIN,INT32_MAX,0},fixture.now+40);
    CHECK(fixture.hold({0,0,1000},40) == pico::motion::None);
}
void test_motion_rest_is_timed_not_invented_from_gaps()
{
    MotionFixture fixture;
    fixture.hold({0,0,1000},60000);
    CHECK(fixture.rests == 0);
    fixture.hold({0,0,1000},80);
    CHECK(fixture.rests == 1);
    fixture.hold({0,0,1000},1000);
    CHECK(fixture.rests == 1);
    fixture.hold({0,0,1000},10000);
    CHECK(fixture.rests == 2); // bounded retry after recent user activity
    MotionFixture no_samples;
    no_samples.hold({0,0,1000},40);
    no_samples.now += 60000;
    CHECK(no_samples.hold({0,0,1000},40) == pico::motion::None);
    CHECK(no_samples.rests == 0);
}
void test_motion_core_priority_and_no_timer_side_effects()
{
    using E=pico::EventKind;
    pico::Core core;
    core.handle({E::Boot,0}); core.handle({E::MotionReady,1});
    core.handle({E::AnimationTick,60000});
    core.handle({E::MotionRest,60000});
    CHECK(core.state().mood == pico::Mood::Sleepy);
    core.handle({E::AnimationTick,61000});
    CHECK(core.state().mood == pico::Mood::Sleepy); // idle expression must not wake a resting Pico
    const auto wake=core.handle({E::MotionPickup,61000});
    CHECK(core.state().mood == pico::Mood::Curious);
    CHECK(!pico::has_effect(wake,pico::Effect::Persist) && !pico::has_effect(wake,pico::Effect::Chirp));
    core.handle({E::MotionRock,61500});
    CHECK(core.state().mood == pico::Mood::Happy);
    CHECK(core.state().interaction_count == 0 && !core.state().timer_running);
    core.handle({E::AnimationTick,64500});
    CHECK(core.state().mood == pico::Mood::Neutral);
    core.handle({E::Tick,64});
    core.handle({E::StartTimer,5});
    core.handle({E::MotionRock,64500});
    CHECK(core.state().timer_remaining_seconds == 5 && core.state().mood != pico::Mood::Happy);
    core.handle({E::Tick,69});
    CHECK(core.state().mood == pico::Mood::Alert);
    core.handle({E::MotionPickup,69000});
    CHECK(core.state().mood == pico::Mood::Alert);
    core = pico::Core{}; core.handle({E::Boot,0}); core.handle({E::MotionReady,1});
    core.handle({E::SetMotionConfig,0});
    core.handle({E::MotionRock,0});
    CHECK(core.state().mood != pico::Mood::Happy);
    core.handle({E::SetMotionConfig,7}); core.handle({E::OpenMenu,0});
    core.handle({E::MotionRock,0});
    CHECK(core.state().screen == pico::Screen::Menu && core.state().mood != pico::Mood::Happy);
}
void test_motion_configuration_staleness_and_packet_compatibility()
{
    using E=pico::EventKind;
    pico::Core core;
    core.handle({E::Boot,0}); core.handle({E::MotionReady,1});
    core.handle({E::AnimationTick,10000});
    CHECK(core.handle({E::MotionRock,9000}) == pico::effect(pico::Effect::None));
    core.handle({E::PrimaryPressed,0});
    CHECK(core.handle({E::MotionPickup,10000}) == pico::effect(pico::Effect::None));
    core.handle({E::AnimationTick,14000});
    core.handle({E::MotionRest,14000});
    CHECK(core.state().mood != pico::Mood::Sleepy); // user was active recently
    core.handle({E::SetMotionConfig,pico::motion::kPickup});
    core.handle({E::MotionRock,14000});
    CHECK(core.state().mood != pico::Mood::Happy);
    core.handle({E::MotionPickup,14000});
    CHECK(core.state().mood == pico::Mood::Curious);
    core.handle({E::DockChanged,static_cast<unsigned>(pico::Dock::Home)});
    CHECK(core.handle({E::MotionRock,14000}) == pico::effect(pico::Effect::None));
    core.handle({E::DockChanged,0});
    core.handle({E::StartMetronome,0});
    core.handle({E::MotionRock,14000});
    CHECK(core.state().metronome_running && core.state().screen == pico::Screen::Music);
    core.handle({E::MicrophoneReady,1}); core.handle({E::StartTuner,0});
    core.handle({E::MotionRock,14000});
    CHECK(core.state().tuner_running && core.state().screen == pico::Screen::Tuner);
    std::uint8_t status[pico::protocol::kStatusSize];
    pico::protocol::encode_status(core.state(),pico::capability(pico::Capability::Motion),status);
    CHECK(status[0] == 1 && (status[4]&128) && (status[14]&128));
    const std::uint8_t valid[]{0x40,7}, invalid[]{0x40,8};
    CHECK(pico::protocol::decode_command(valid,2).status == pico::protocol::DecodeStatus::Ok);
    CHECK(pico::protocol::decode_command(invalid,2).status == pico::protocol::DecodeStatus::InvalidValue);
    CHECK(pico::protocol::decode_command(valid,1).status == pico::protocol::DecodeStatus::WrongLength);
    core.handle({E::MotionReady,0});
    pico::protocol::encode_status(core.state(),0,status);
    CHECK((status[4]&128) == 0);
}
void run_motion_tests()
{
    test_motion_still_pickup_and_gesture();
    test_motion_noise_impacts_gaps_and_wrap();
    test_motion_rest_is_timed_not_invented_from_gaps();
    test_motion_core_priority_and_no_timer_side_effects();
    test_motion_configuration_staleness_and_packet_compatibility();
}
