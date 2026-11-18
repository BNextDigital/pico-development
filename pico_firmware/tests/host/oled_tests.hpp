void test_oled_settings_and_power_policy()
{
    using E = pico::EventKind;
    pico::Core core;
    core.handle({E::Boot,0});
    CHECK(core.state().display_brightness_percent == 50 && !core.state().display_timeout_minutes);
    settings_item(core,7);
    core.handle({E::Select,0});
    CHECK(pico::display::brightness_percent(core.state()) == 55);
    CHECK(core.state().display_brightness_percent == 50);
    core.handle({E::Back,0});
    CHECK(pico::display::brightness_percent(core.state()) == 50);
    settings_item(core,7); core.handle({E::Select,0}); save_setting(core);
    CHECK(core.state().display_brightness_percent == 55);
    core.handle({E::SetBrightness,7}); settings_item(core,7);
    core.handle({E::NavigatePrevious,0}); core.handle({E::Select,0});
    CHECK(core.state().setting_draft == 5);
    core.handle({E::Back,0});
    settings_item(core,8); core.handle({E::Select,0}); save_setting(core);
    CHECK(core.state().display_timeout_minutes == 1);
    core.handle({E::AnimationTick,59999}); CHECK(!core.state().display_asleep);
    core.handle({E::AnimationTick,60000}); CHECK(core.state().display_asleep);
    CHECK(core.next_animation_ms(60001) == UINT32_MAX);
    const auto count = core.state().interaction_count;
    const auto screen = core.state().screen;
    const auto wake = core.handle({E::FocusButtonTapped,0});
    CHECK(pico::has_effect(wake,pico::Effect::Render) && !pico::has_effect(wake,pico::Effect::Persist));
    CHECK(!core.state().display_asleep && core.state().screen == screen);
    CHECK(!core.state().timer_running && core.state().interaction_count == count);
    core.handle({E::Tick,60}); core.handle({E::StartTimer,1});
    core.handle({E::SleepDisplay,0}); CHECK(core.state().display_asleep);
    core.handle({E::Tick,61}); CHECK(core.state().timer_completed && !core.state().display_asleep);
    core.handle({E::SleepDisplay,0}); CHECK(!core.state().display_asleep); // alert priority
    core.handle({E::Tick,65});
    core.handle({E::StartMetronome,0});
    core.handle({E::SleepDisplay,0}); CHECK(!core.state().display_asleep);
    core.handle({E::StopMetronome,0});
    core.handle({E::MicrophoneReady,1}); core.handle({E::StartTuner,0});
    core.handle({E::SleepDisplay,0}); CHECK(!core.state().display_asleep);
    core.handle({E::StopTuner,0});
    core.handle({E::SetBrightness,55});
    const auto blob = pico::persistence::encode(core.persistent_state());
    pico::PersistedState saved;
    CHECK(pico::persistence::decode(blob,saved));
    CHECK(saved.display_brightness_percent == 55 && saved.display_timeout_minutes == 1);
    pico::Core restored; CHECK(restored.restore(saved)); restored.handle({E::Boot,0});
    CHECK(!restored.state().display_asleep && restored.state().display_brightness_percent == 55);
    std::array<std::uint8_t,28> old{};
    std::copy(blob.begin(),blob.begin()+23,old.begin()); old[4]=4;
    checksum_settings_bytes(old.data(),old.size());
    CHECK(pico::persistence::decode(old.data(),old.size(),saved));
    CHECK(saved.display_brightness_percent == 50 && saved.display_timeout_minutes == 0);
    for (unsigned index : {23,24,25,26,27}) {
        auto invalid=blob; invalid[index]=255; checksum_settings_bytes(invalid.data(),invalid.size());
        CHECK(!pico::persistence::decode(invalid,saved));
    }
    pico::Core background;
    background.handle({E::SetDisplayTimeout,1}); background.handle({E::StartTimer,61});
    background.handle({E::Tick,60}); CHECK(background.state().display_asleep);
    background.handle({E::Connected,0}); CHECK(background.state().display_asleep);
    background.handle({E::Tick,61}); CHECK(background.state().timer_completed && !background.state().display_asleep);
    background.handle({E::Tick,65}); background.handle({E::SleepDisplay,0});
    background.handle({E::MotionReady,1}); background.handle({E::MotionPickup,65000});
    CHECK(!background.state().display_asleep);
    CHECK(core.handle({E::SetBrightness,0}) == pico::effect(pico::Effect::None));
    CHECK(core.handle({E::SetDisplayTimeout,31}) == pico::effect(pico::Effect::None));
}
void test_oled_timeout_wrap_preview_and_rendering()
{
    using E=pico::EventKind;
    pico::Core core;
    core.handle({E::AnimationTick,UINT32_MAX-100U});
    core.handle({E::SetDisplayTimeout,1});
    core.handle({E::OpenSettings,0});
    CHECK(core.next_animation_ms(UINT32_MAX-100U) == 60000);
    core.handle({E::AnimationTick,59898}); CHECK(!core.state().display_asleep);
    core.handle({E::AnimationTick,59899}); CHECK(core.state().display_asleep);
    core.handle({E::Back,0}); CHECK(!core.state().display_asleep);
    settings_item(core,7);
    core.handle({E::AnimationTick,200000}); CHECK(!core.state().display_asleep); // editing protected
    core.handle({E::Tick,200}); core.handle({E::StartTimer,1}); core.handle({E::Tick,201});
    CHECK(core.state().screen == pico::Screen::Home && !core.state().display_asleep);
    CHECK(pico::display::brightness_percent(core.state()) == 50); // preview discarded
    pico::State state; state.display_asleep=true;
    std::array<std::uint8_t,1024> pixels{}; pixels.fill(255);
    CHECK(pico::display::render(state,{pixels.data(),pixels.size(),128,64,pico::display::MonoLayout::HorizontalMsb}));
    CHECK(std::all_of(pixels.begin(),pixels.end(),[](auto b){ return b==0; }));
    state.display_asleep=false; state.screen=pico::Screen::Settings;
    for (unsigned row=0; row<11; ++row) {
        state.settings_index=row;
        CHECK(pico::display::render(state,{pixels.data(),pixels.size(),128,64,pico::display::MonoLayout::HorizontalMsb}));
    }
}
