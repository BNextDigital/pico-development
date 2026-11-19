void test_focus_goal_and_bounded_finish()
{
    using E=pico::EventKind;
    pico::Core c; c.handle({E::Boot,0});
    c.handle({E::SetTimerDuration,2}); c.handle({E::SetBreakDuration,1});
    CHECK(!pico::has_effect(c.handle({E::SetFocusGoal,2}),pico::Effect::Persist));
    c.handle({E::StartFocusCycle,0});
    c.handle({E::SetFocusGoal,4}); // next-session goal, current target stays two
    CHECK(c.state().active_focus_goal == 2);
    c.handle({E::Tick,1}); c.handle({E::PauseTimer,0}); c.handle({E::Tick,20});
    c.handle({E::ResumeTimer,0}); c.handle({E::Tick,21});
    CHECK(c.state().completed_work_sessions == 1 && c.state().focus_waiting);
    CHECK(c.state().focus_work_seconds == 2);
    c.handle({E::NextFocusPhase,0}); c.handle({E::Tick,22});
    CHECK(c.state().focus_work_seconds == 2); // breaks excluded
    c.handle({E::NextFocusPhase,0}); c.handle({E::Tick,24});
    CHECK(c.state().focus_finished && !c.state().focus_waiting);
    CHECK(c.state().focus_phase == pico::FocusPhase::None && !c.state().timer_running);
    CHECK(c.state().focus_history_count == 1);
    CHECK(c.state().focus_history[0].finished && c.state().focus_history[0].rounds == 2);
    CHECK(c.state().focus_history[0].work_seconds == 4);
    CHECK(c.handle({E::NextFocusPhase,0}) == pico::effect(pico::Effect::None));
    c.handle({E::Tick,28}); CHECK(c.state().screen == pico::Screen::FocusSummary);
    c.handle({E::Select,0}); CHECK(c.state().screen == pico::Screen::Utility);
    CHECK(c.state().focus_history_count == 1); // no duplicate on idle ticks
    c.handle({E::StartFocusCycle,0}); CHECK(c.state().active_focus_goal == 4);
    c.handle({E::Tick,30}); c.handle({E::NextFocusPhase,0});
    c.handle({E::CancelTimer,0});
    CHECK(c.state().focus_history_count == 2 && !c.state().focus_history[0].finished);
    CHECK(c.state().focus_history[0].rounds == 1 && c.state().focus_history[0].work_seconds == 2);
    c.handle({E::CancelTimer,0}); CHECK(c.state().focus_history_count == 2);
}
void test_focus_history_bounds_exits_and_restart()
{
    using E=pico::EventKind;
    pico::Core c; c.handle({E::SetTimerDuration,1}); c.handle({E::SetFocusGoal,1});
    for (unsigned now=1; now<=6; ++now) {
        c.handle({E::StartFocusCycle,0}); c.handle({E::Tick,now});
    }
    CHECK(c.state().focus_history_count == 4);
    c.handle({E::Tick,10});
    c.handle({E::NavigatePrevious,0}); CHECK(c.state().focus_history_index == 3);
    c.handle({E::NavigateNext,0}); CHECK(c.state().focus_history_index == 0);
    CHECK(c.handle({E::SetFocusGoal,13}) == pico::effect(pico::Effect::None));
    c.handle({E::SetFocusGoal,0}); c.handle({E::StartFocusCycle,0}); c.handle({E::Tick,11});
    c.handle({E::SetMode,static_cast<unsigned>(pico::Mode::Cooking)});
    CHECK(!c.state().focus_history[0].finished && c.state().focus_phase == pico::FocusPhase::None);
    c.handle({E::StartFocusCycle,0}); c.handle({E::Tick,12});
    c.handle({E::DockChanged,static_cast<unsigned>(pico::Dock::Car)});
    CHECK(c.state().focus_phase == pico::FocusPhase::None && !c.state().timer_running);
    pico::Core reboot; CHECK(reboot.restore(c.persistent_state())); reboot.handle({E::Boot,0});
    CHECK(reboot.state().focus_goal == 0 && reboot.state().focus_history_count == 0);
    CHECK(reboot.state().configured_duration_seconds == 1);
    pico::Core exit;
    exit.handle({E::SetFocusGoal,1}); exit.handle({E::SetTimerDuration,1});
    exit.handle({E::StartFocusCycle,0}); exit.handle({E::Tick,1}); exit.handle({E::Tick,5});
    CHECK(exit.state().screen == pico::Screen::FocusSummary);
    exit.handle({E::SetMode,static_cast<unsigned>(pico::Mode::Music)});
    CHECK(exit.state().screen == pico::Screen::Home && !exit.state().focus_finished);
    exit.handle({E::StartFocusCycle,0}); exit.handle({E::Tick,6}); exit.handle({E::Tick,10});
    exit.handle({E::StartFocusCycle,0});
    CHECK(exit.state().screen == pico::Screen::Utility && exit.state().timer_running);
    pico::FocusSession overflow; overflow.start(); overflow.complete(UINT32_MAX);
    overflow.next(); overflow.complete(); overflow.next(); overflow.complete(100);
    CHECK(overflow.work_seconds() == UINT32_MAX);
}
void test_focus_goal_editor_and_frames()
{
    using E=pico::EventKind;
    pico::Core c; c.handle({E::OpenMenu,0}); c.handle({E::Select,0});
    while(c.state().utility_index != 6) c.handle({E::NavigateNext,0});
    c.handle({E::Select,0}); c.handle({E::Select,0});
    CHECK(c.state().setting_draft == 1 && c.state().focus_goal == 0);
    c.handle({E::Back,0}); CHECK(c.state().screen == pico::Screen::Utility && c.state().focus_goal == 0);
    c.handle({E::Select,0}); c.handle({E::Select,0}); save_setting(c);
    CHECK(c.state().screen == pico::Screen::Utility && c.state().focus_goal == 1);
    c.handle({E::OpenFocusSummary,0});
    std::array<std::uint8_t,1024> pixels{};
    CHECK(pico::display::render(c.state(),{pixels.data(),pixels.size(),128,64,pico::display::MonoLayout::HorizontalMsb}));
    c.handle({E::Back,0});
    for (unsigned i=0; i<8; ++i) {
        CHECK(pico::display::render(c.state(),{pixels.data(),pixels.size(),128,64,pico::display::MonoLayout::HorizontalMsb}));
        c.handle({E::NavigateNext,0});
    }
}
