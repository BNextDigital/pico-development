// Included inside the host test namespace.
void settings_item(pico::Core &core,unsigned index)
{
    using E=pico::EventKind;
    core.handle({E::OpenSettings,0});
    while (core.state().settings_index != index) core.handle({E::NavigateNext,0});
    core.handle({E::Select,0});
}
void save_setting(pico::Core &core)
{
    using E=pico::EventKind;
    while (core.state().setting_action != 2) core.handle({E::NavigateNext,0});
    core.handle({E::Select,0});
}
void test_settings_theme_preview_and_back()
{
    using E=pico::EventKind;
    pico::Core core;
    core.handle({E::OpenMenu,0});
    for (unsigned i=0; i<7; ++i) core.handle({E::NavigateNext,0});
    core.handle({E::FocusButtonTapped,0});
    CHECK(core.state().screen == pico::Screen::Settings);
    settings_item(core,0);
    CHECK(core.state().screen == pico::Screen::SettingEdit);
    const auto preview=core.handle({E::Select,0}); // theme +1
    CHECK(core.state().setting_draft == 1 && core.state().theme == pico::FaceTheme::Modern);
    CHECK(!pico::has_effect(preview,pico::Effect::Persist));
    core.handle({E::Back,0});
    CHECK(core.state().theme == pico::FaceTheme::Modern && core.state().screen == pico::Screen::Settings);
    core.handle({E::Select,0}); core.handle({E::Select,0}); save_setting(core);
    CHECK(core.state().theme == pico::FaceTheme::Chibi && core.state().screen == pico::Screen::Settings);
    core.handle({E::Back,0}); CHECK(core.state().screen == pico::Screen::Menu);
}
void test_settings_mute_volume_motion_and_durations()
{
    using E=pico::EventKind;
    pico::Core core;
    settings_item(core,1); core.handle({E::Select,0}); save_setting(core);
    CHECK(core.state().sound_muted);
    CHECK(!pico::has_effect(core.handle({E::TestSound,0}),pico::Effect::Chirp));
    settings_item(core,2); core.handle({E::Select,0}); save_setting(core);
    CHECK(core.state().sound_volume_percent == 30);
    settings_item(core,5); core.handle({E::NavigatePrevious,0}); core.handle({E::Select,0});
    CHECK(core.state().setting_draft == 6 && core.state().motion_config == 7);
    save_setting(core); CHECK(core.state().motion_config == 6);
    settings_item(core,3); core.handle({E::Select,0}); core.handle({E::NavigateNext,0}); core.handle({E::Select,0});
    CHECK(core.state().screen == pico::Screen::Settings && core.state().configured_duration_seconds == 1560);
    settings_item(core,4); core.handle({E::Select,0}); core.handle({E::NavigateNext,0}); core.handle({E::Select,0});
    CHECK(core.state().screen == pico::Screen::Settings && core.state().break_duration_seconds == 360);
    core.handle({E::SetMuted,0}); core.handle({E::SetVolume,0});
    CHECK(!pico::has_effect(core.handle({E::TestSound,0}),pico::Effect::Chirp));
    CHECK(core.handle({E::SetVolume,101}) == pico::effect(pico::Effect::None));
    CHECK(pico::has_effect(core.handle({E::SetBreakDuration,420}),pico::Effect::Persist));
    CHECK(pico::has_effect(core.handle({E::SetMotionConfig,0}),pico::Effect::Persist));
    CHECK(core.handle({E::SetMotionConfig,0}) == pico::effect(pico::Effect::None));
}
void checksum_settings_bytes(std::uint8_t *data,std::size_t size)
{
    std::uint32_t hash=2166136261U;
    for (std::size_t i=0; i<size-4; ++i) { hash^=data[i]; hash*=16777619U; }
    for (unsigned i=0; i<4; ++i) data[size-4+i]=static_cast<std::uint8_t>(hash>>(8*i));
}
void test_settings_v4_roundtrip_migration_and_corruption()
{
    using E=pico::EventKind;
    pico::Core core;
    core.handle({E::SetTheme,4}); core.handle({E::SetTimerDuration,1620});
    core.handle({E::SetBreakDuration,420}); core.handle({E::SetMuted,1});
    core.handle({E::SetVolume,70}); core.handle({E::SetMotionConfig,1});
    auto blob=pico::persistence::encode(core.persistent_state());
    CHECK(blob.size() == 28 && blob[4] == 4);
    pico::PersistedState state;
    CHECK(pico::persistence::decode(blob,state));
    pico::Core restored; CHECK(restored.restore(state)); restored.handle({E::Boot,0});
    CHECK(restored.state().mode == pico::Mode::Companion && restored.state().screen == pico::Screen::Home);
    CHECK(restored.state().sound_muted && restored.state().sound_volume_percent == 70);
    CHECK(restored.state().break_duration_seconds == 420 && restored.state().motion_config == 1);
    CHECK(restored.state().theme == pico::FaceTheme::Mecha && restored.state().configured_duration_seconds == 1620);
    CHECK(!restored.state().timer_running && !restored.state().tuner_running);
    for (const auto index : {16,20,21,22,23}) {
        auto corrupt=blob; corrupt[static_cast<std::size_t>(index)] ^= 0x80;
        CHECK(!pico::persistence::decode(corrupt,state));
    }
    for (const auto &pair : {std::pair<unsigned,unsigned>{20,2},{21,101},{22,8},{23,1}}) {
        auto invalid=blob; invalid[pair.first]=static_cast<std::uint8_t>(pair.second);
        checksum_settings_bytes(invalid.data(),invalid.size());
        CHECK(!pico::persistence::decode(invalid,state));
    }
    std::array<std::uint8_t,20> legacy{};
    std::copy(blob.begin(),blob.begin()+16,legacy.begin()); legacy[4]=3;
    checksum_settings_bytes(legacy.data(),legacy.size());
    CHECK(pico::persistence::decode(legacy.data(),legacy.size(),state));
    CHECK(state.theme == pico::FaceTheme::Mecha && state.configured_duration_seconds == 1620);
    CHECK(state.break_duration_seconds == 300 && !state.sound_muted && state.sound_volume_percent == 25 && state.motion_config == 7);
    CHECK(!pico::persistence::decode(blob.data(),27,state));
}
void test_settings_interrupts_limits_and_protocol()
{
    using E=pico::EventKind;
    pico::Core core;
    core.handle({E::StartMetronome,0}); core.handle({E::OpenSettings,0});
    CHECK(!core.state().metronome_running);
    core.handle({E::MicrophoneReady,1}); core.handle({E::StartTuner,0}); core.handle({E::OpenSettings,0});
    CHECK(!core.state().tuner_running);
    core.handle({E::StartTimer,1}); settings_item(core,0); core.handle({E::Select,0});
    core.handle({E::Tick,1});
    CHECK(core.state().timer_completed && core.state().screen == pico::Screen::Home);
    CHECK(core.state().theme == pico::FaceTheme::Modern);
    core.handle({E::CancelTimer,0});
    settings_item(core,2);
    for (unsigned i=0; i<50; ++i) core.handle({E::Select,0});
    CHECK(core.state().setting_draft == 100 && core.state().sound_volume_percent == 25);
    core.handle({E::NavigatePrevious,0});
    for (unsigned i=0; i<50; ++i) core.handle({E::Select,0});
    CHECK(core.state().setting_draft == 0);
    core.handle({E::FocusButtonHeld,0}); CHECK(core.state().screen == pico::Screen::Settings);
    const std::uint8_t open[]{0x15},volume[]{0x25,70},bad[]{0x25,101};
    CHECK(pico::protocol::decode_command(open,1).event.kind == E::OpenSettings);
    CHECK(pico::protocol::decode_command(volume,2).event.kind == E::SetVolume);
    CHECK(pico::protocol::decode_command(bad,2).status == pico::protocol::DecodeStatus::InvalidValue);
    core.handle({E::DockChanged,static_cast<unsigned>(pico::Dock::Car)});
    CHECK(core.handle({E::OpenSettings,0}) == pico::effect(pico::Effect::None));
}
void run_settings_tests()
{
    test_settings_theme_preview_and_back();
    test_settings_mute_volume_motion_and_durations();
    test_settings_v4_roundtrip_migration_and_corruption();
    test_settings_interrupts_limits_and_protocol();
}
