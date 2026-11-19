void test_locator_service_and_protocol()
{
    using namespace pico;
    Locator service;
    CHECK(!service.start(0,0) && !service.start(0,121));
    CHECK(service.next(0)==UINT32_MAX);
    const std::uint32_t start=UINT32_MAX-500U;
    CHECK(service.start(start,5)); CHECK(!service.start(start+1U,120));
    CHECK(service.next(start)==2000U);
    CHECK(!service.advance(start+1999U)); CHECK(service.advance(start+2000U));
    CHECK(service.remaining(start+2000U)==3U);
    CHECK(service.advance(start+4500U)); // one pulse, no stale burst
    CHECK(service.next(start+4500U)==500U);
    CHECK(!service.advance(start+5000U) && !service.active());
    CHECK(service.next(start+5000U)==UINT32_MAX);
    std::uint8_t packet[]{0x50,30,0};
    auto decoded=protocol::decode_command(packet,3);
    CHECK(decoded.status==protocol::DecodeStatus::Ok && decoded.event.kind==EventKind::StartFinding && decoded.event.value==30);
    CHECK(protocol::decode_command(packet,2).status==protocol::DecodeStatus::WrongLength);
    packet[1]=0; CHECK(protocol::decode_command(packet,3).status==protocol::DecodeStatus::InvalidValue);
    packet[1]=121; CHECK(protocol::decode_command(packet,3).status==protocol::DecodeStatus::InvalidValue);
    packet[0]=0x51; CHECK(protocol::decode_command(packet,1).event.kind==EventKind::StopFinding);
    CHECK(protocol::decode_command(packet,3).status==protocol::DecodeStatus::WrongLength);
    State state; state.finding=true; state.finding_remaining_seconds=30;
    std::uint8_t status[protocol::kLocatorStatusSize]{};
    protocol::encode_locator(state,capability(Capability::Speaker),status);
    CHECK(status[0]==1 && status[1]==3 && status[2]==30 && status[3]==0);
    state.sound_muted=true; protocol::encode_locator(state,capability(Capability::Speaker),status);
    CHECK(status[1]==1);
    state.sound_muted=false; protocol::encode_locator(state,0,status); CHECK(status[1]==1);
}
void test_locator_priority_and_acknowledgement()
{
    using E=pico::EventKind;
    pico::Core c; c.handle({E::Boot,0});
    c.handle({E::SleepDisplay,0});
    const auto start=c.handle({E::StartFinding,30});
    CHECK(c.state().finding && !c.state().display_asleep);
    CHECK(!pico::has_effect(start,pico::Effect::Persist));
    CHECK(pico::has_effect(start,pico::Effect::Chirp));
    CHECK(c.handle({E::StartFinding,120})==pico::effect(pico::Effect::None)); // cannot extend an active call
    CHECK(c.next_music_ms(0)==2000U);
    c.handle({E::SleepDisplay,0}); CHECK(!c.state().display_asleep);
    CHECK(pico::has_effect(c.handle({E::MusicTick,2000}),pico::Effect::Chirp));
    c.handle({E::FocusButtonTapped,0});
    CHECK(!c.state().finding && c.state().mood==pico::Mood::Happy && c.state().interaction_count==0);
    CHECK(!c.state().timer_running && c.state().screen==pico::Screen::Home);
    c.handle({E::StartTimer,1}); c.handle({E::StartFinding,30}); c.handle({E::Tick,1});
    CHECK(!c.state().finding && c.state().timer_completed);
    CHECK(c.handle({E::StartFinding,30})==pico::effect(pico::Effect::None));
    c.handle({E::AnimationTick,10000});
    c.handle({E::OpenSettings,0}); c.handle({E::StartFinding,1});
    c.handle({E::MusicTick,3000}); CHECK(!c.state().finding && c.state().screen==pico::Screen::Settings);
    c.handle({E::StartMetronome,0}); c.handle({E::StartFinding,10});
    CHECK(!c.state().metronome_running && c.state().finding);
    c.handle({E::StartTuner,0}); CHECK(!c.state().finding);
    c.handle({E::StartFinding,10}); c.handle({E::Disconnected,0}); CHECK(c.state().finding); // bounded session survives link loss
    c.handle({E::Boot,0}); CHECK(!c.state().finding); // never durable
}
