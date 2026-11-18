// Included inside the host test namespace.
void open_duration(pico::Core &core,bool break_duration=false)
{
    using E=pico::EventKind;
    core.handle({E::OpenMenu,0}); core.handle({E::Select,0}); // Focus, Start index 1
    core.handle({E::NavigatePrevious,0}); // duration index 0
    if (break_duration) core.handle({E::NavigatePrevious,0}); // wrap to break index 5
    core.handle({E::Select,0});
}
void test_minute_duration_preview_save_and_cancel()
{
    using E=pico::EventKind;
    pico::Core core; core.handle({E::Boot,0});
    open_duration(core);
    CHECK(core.state().screen == pico::Screen::DurationEdit && core.state().draft_duration_seconds == 1500);
    CHECK(!pico::has_effect(core.handle({E::Select,0}),pico::Effect::Persist));
    CHECK(core.state().draft_duration_seconds == 1560 && core.state().configured_duration_seconds == 1500);
    core.handle({E::Back,0});
    CHECK(core.state().screen == pico::Screen::Utility && core.state().configured_duration_seconds == 1500);
    core.handle({E::Select,0}); core.handle({E::Select,0}); // open, +1
    core.handle({E::NavigateNext,0}); // Save
    const auto saved=core.handle({E::Select,0});
    CHECK(core.state().screen == pico::Screen::Utility && core.state().configured_duration_seconds == 1560);
    CHECK(pico::has_effect(saved,pico::Effect::Persist));
    core.handle({E::Select,0}); // open again
    core.handle({E::NavigatePrevious,0}); // -1
    core.handle({E::Select,0});
    CHECK(core.state().draft_duration_seconds == 1500);
    core.handle({E::NavigatePrevious,0}); // wrap to Cancel
    core.handle({E::Select,0});
    CHECK(core.state().configured_duration_seconds == 1560);
}
void test_minute_bounds_and_legacy_seconds()
{
    using E=pico::EventKind;
    pico::Core core;
    core.handle({E::SetTimerDuration,10});
    open_duration(core);
    CHECK(core.state().draft_duration_seconds == 60 && core.state().configured_duration_seconds == 10);
    core.handle({E::NavigatePrevious,0}); core.handle({E::Select,0});
    CHECK(core.state().draft_duration_seconds == 60); // no zero duration/wrap
    core.handle({E::Back,0}); CHECK(core.state().configured_duration_seconds == 10);
    core.handle({E::SetTimerDuration,61}); core.handle({E::Select,0});
    CHECK(core.state().draft_duration_seconds == 120);
    core.handle({E::Back,0});
    core.handle({E::SetTimerDuration,pico::kMaxTimerSeconds}); core.handle({E::Select,0});
    core.handle({E::Select,0}); // +1 at max
    CHECK(core.state().draft_duration_seconds == pico::kMaxTimerSeconds);
    core.handle({E::NavigatePrevious,0}); core.handle({E::Select,0});
    CHECK(core.state().draft_duration_seconds == pico::kMaxTimerSeconds-60);
}
void test_break_minutes_apply_to_next_phase_without_restarting_timer()
{
    using E=pico::EventKind;
    pico::Core core;
    core.handle({E::StartFocusCycle,0}); core.handle({E::Tick,10});
    const auto remaining=core.state().timer_remaining_seconds;
    open_duration(core,true);
    CHECK(core.state().editing_break_duration && core.state().draft_duration_seconds == 300);
    core.handle({E::Select,0}); core.handle({E::NavigateNext,0});
    core.handle({E::Select,0});
    CHECK(core.state().break_duration_seconds == 360 && core.state().timer_running);
    CHECK(core.state().timer_remaining_seconds == remaining);
    core.handle({E::Tick,1500}); core.handle({E::NextFocusPhase,0});
    CHECK(core.state().focus_phase == pico::FocusPhase::Break && core.state().timer_remaining_seconds == 360);
}
void test_duration_interrupts_and_cooking_menu()
{
    using E=pico::EventKind;
    pico::Core core;
    core.handle({E::SetTimerDuration,10}); core.handle({E::StartTimer,1});
    open_duration(core); core.handle({E::Select,0});
    core.handle({E::Tick,1});
    CHECK(core.state().timer_completed && core.state().screen == pico::Screen::Home);
    CHECK(core.state().configured_duration_seconds == 10);
    core.handle({E::CancelTimer,0});
    open_duration(core); core.handle({E::SetMode,static_cast<unsigned>(pico::Mode::Cooking)});
    CHECK(core.state().screen == pico::Screen::Home);
    core.handle({E::OpenMenu,0}); core.handle({E::NavigateNext,0}); core.handle({E::Select,0});
    core.handle({E::NavigatePrevious,0}); core.handle({E::Select,0});
    CHECK(core.state().screen == pico::Screen::DurationEdit && !core.state().editing_break_duration);
    core.handle({E::FocusButtonHeld,0});
    CHECK(core.state().screen == pico::Screen::Utility && core.state().configured_duration_seconds == 10);
}
void run_duration_tests()
{
    test_minute_duration_preview_save_and_cancel();
    test_minute_bounds_and_legacy_seconds();
    test_break_minutes_apply_to_next_phase_without_restarting_timer();
    test_duration_interrupts_and_cooking_menu();
}
