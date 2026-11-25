void test_parking_recall()
{
    using E = pico::EventKind;
    using Phase = pico::parking::Phase;
    using pico::Effect;
    constexpr std::uint32_t boot = 0x12345678;
    pico::Core core;
    const auto go_to_parking = [&] {
        core.handle({E::OpenMenu});
        while (core.state().menu_index != 4) core.handle({E::NavigateNext});
        core.handle({E::FocusButtonTapped});
        CHECK(core.state().screen == pico::Screen::Parking);
    };
    const auto result = [&](std::uint32_t id, std::uint32_t seq, Phase phase) {
        return core.handle({E::ParkingResult, id, seq | (static_cast<unsigned>(phase) << 24U)});
    };
    core.handle({E::Boot, boot});
    core.handle({E::StartFocusCycle});
    go_to_parking();
    CHECK(core.state().mode == pico::Mode::Focus && core.state().timer_running);
    core.handle({E::Select}); // An existing remote menu command cannot request a fix.
    CHECK(core.state().parking.phase == Phase::Idle);
    core.handle({E::FocusButtonTapped});
    CHECK(core.state().parking.phase == Phase::OpenApp);
    core.handle({E::Connected});
    core.handle({E::FocusButtonTapped});
    CHECK(core.state().parking.phase == Phase::OpenApp); // Older companion.
    core.handle({E::ParkingReady, 1});
    auto effects = core.handle({E::FocusButtonTapped});
    const auto first = core.state().parking.sequence;
    CHECK(core.state().parking.phase == Phase::Saving && first != 0);
    CHECK(pico::has_effect(effects, Effect::NotifyParking));
    CHECK(!pico::has_effect(effects, Effect::Chirp) && !pico::has_effect(effects, Effect::Persist));
    CHECK(!core.handle({E::FocusButtonTapped})); // Bounded single transaction.
    CHECK(!result(boot+1, first, Phase::Saved));
    CHECK(!result(boot, first+1, Phase::Saved));
    CHECK(!result(boot, first, Phase::Saving));
    core.handle({E::Tick, 44});
    CHECK(core.state().parking.phase == Phase::Saving && core.state().timer_running);
    core.handle({E::Tick, 45});
    CHECK(core.state().parking.phase == Phase::TimedOut);
    CHECK(pico::has_effect(result(boot, first, Phase::Saved), Effect::NotifyParking));
    CHECK(core.state().parking.reaction);
    CHECK(!result(boot, first, Phase::Saved));
    core.handle({E::Tick, 48});
    CHECK(!core.state().parking.reaction);
    core.handle({E::FocusButtonTapped});
    const auto second = core.state().parking.sequence;
    CHECK(second != first && !result(boot, first, Phase::Saved));
    core.handle({E::Disconnected});
    CHECK(core.state().parking.phase == Phase::Interrupted);
    CHECK(!result(boot, second, Phase::Saved));
    core.handle({E::Connected});
    result(boot, second, Phase::Saved);
    CHECK(core.state().parking.phase == Phase::Saved);
    core.handle({E::ParkingReady, 1});
    core.handle({E::FocusButtonTapped});
    auto id = core.state().parking.sequence;
    core.handle({E::FocusButtonHeld});
    CHECK(core.state().parking.phase == Phase::Cancelled);
    CHECK(!result(boot, id, Phase::Saved));
    core.handle({E::Boot, boot+1});
    CHECK(core.state().parking.phase == Phase::Idle && !core.state().connected);
    CHECK(core.state().parking.boot_id == boot+1 && core.state().parking.sequence == 0);
    core.handle({E::Tick, 0});

    core.handle({E::SetDisplayTimeout, 1});
    go_to_parking();
    core.handle({E::AnimationTick, 60000});
    CHECK(core.state().display_asleep);
    core.handle({E::FocusButtonTapped});
    CHECK(!core.state().display_asleep && core.state().parking.phase == Phase::Idle);
    core.handle({E::Connected});
    core.handle({E::ParkingReady, 1});
    core.handle({E::FocusButtonTapped});
    id = core.state().parking.sequence;
    core.handle({E::StartFinding, 10});
    CHECK(core.state().finding);
    core.handle({E::FocusButtonTapped}); // Dismiss the alert, keep the same request.
    CHECK(!core.state().finding && core.state().parking.sequence == id);
    CHECK(core.state().parking.phase == Phase::Saving);
    result(boot+1, id, Phase::Permission);
    CHECK(core.state().parking.phase == Phase::Permission);
    core.handle({E::OpenClock});
    CHECK(core.state().screen == pico::Screen::Clock);
}

void test_parking_protocol_validation()
{
    using pico::protocol::decode_command;
    using D = pico::protocol::DecodeStatus;
    const std::uint8_t ready[]{0x70, 1}, bad_ready[]{0x70, 2};
    const std::uint8_t bad_result[]{0x71, 1, 1, 0, 0, 0, 1, 0, 0};
    const std::uint8_t zero_id[]{0x71, 2, 1, 0, 0, 0, 0, 0, 0};
    CHECK(decode_command(ready, sizeof(ready)).status == D::Ok);
    CHECK(decode_command(ready, 1).status == D::WrongLength);
    CHECK(decode_command(bad_ready, sizeof(bad_ready)).status == D::InvalidValue);
    CHECK(decode_command(bad_result, sizeof(bad_result)).status == D::InvalidValue);
    CHECK(decode_command(zero_id, sizeof(zero_id)).status == D::InvalidValue);
}
