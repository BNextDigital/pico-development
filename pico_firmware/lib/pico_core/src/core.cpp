#include <pico/core.hpp>
#include <pico/features.hpp>
#include <limits>

namespace pico {
namespace {
bool valid_mode(Mode m) { return m < Mode::Count; }
bool valid_mood(Mood m) { return m < Mood::Count; }
bool valid_theme(FaceTheme t) { return t < FaceTheme::Count; }
constexpr Mode menu_modes[]{Mode::Focus, Mode::Cooking, Mode::Companion,
                            Mode::Music, Mode::Wayfinding, Mode::Notes};
}
void Core::activity()
{
    personality_.activity(state_, animation_ms_);
    display_activity_ms_ = animation_ms_;
    state_.display_asleep = false;
}
void Core::sync_timer()
{
    state_.timer_running = timer_.running();
    state_.timer_paused = timer_.paused();
    state_.timer_total_seconds = timer_.total();
    state_.timer_remaining_seconds = timer_.remaining();
}
void Core::sync_metronome()
{
    state_.metronome_running = metronome_.running();
    state_.metronome_bpm = static_cast<std::uint16_t>(metronome_.bpm());
    state_.beats_per_bar = static_cast<std::uint8_t>(metronome_.beats());
    state_.current_beat = static_cast<std::uint8_t>(metronome_.beat());
    state_.beat_pulse = metronome_.pulse();
}
Effects Core::music_beat()
{
    state_.sound_cue = metronome_.beat() == 1 ? audio::Cue::MetronomeAccent : audio::Cue::MetronomeClick;
    return Effect::Render | Effect::Chirp;
}
void Core::sync_focus()
{
    state_.focus_phase = focus_.phase();
    state_.focus_waiting = focus_.waiting();
    state_.completed_work_sessions = focus_.completed_work();
    state_.active_focus_goal = static_cast<std::uint8_t>(focus_.goal());
    state_.focus_finished = focus_.finished();
    state_.focus_work_seconds = focus_.work_seconds();
}
void Core::archive_focus(bool finished)
{
    if (!focus_.completed_work()) return;
    for (unsigned i=3; i>0; --i) state_.focus_history[i] = state_.focus_history[i-1];
    state_.focus_history[0] = {focus_.completed_work(),focus_.work_seconds(),finished};
    if (state_.focus_history_count < 4) ++state_.focus_history_count;
    state_.focus_history_index = 0;
}
void Core::cancel_focus()
{
    if (focus_.phase() != FocusPhase::None) archive_focus(false);
    focus_.cancel();
}
Effects Core::start_focus_phase()
{
    timer_.start(focus_.phase() == FocusPhase::Work ? state_.configured_duration_seconds :
                 state_.break_duration_seconds, state_.uptime_seconds);
    activity(); state_.timer_completed = false; sync_timer(); sync_focus();
    personality_.react(state_, Mood::Neutral, animation_ms_);
    return Effect::Render | Effect::Notify;
}
Effects Core::timer_complete()
{
    state_.reference_playing = false;
    metronome_.stop(); sync_metronome();
    if (state_.screen == Screen::DurationEdit || state_.screen == Screen::SettingEdit) state_.screen = Screen::Home;
    state_.tuner_running = false;
    state_.sound_cue = focus_.phase() == FocusPhase::Work ? audio::Cue::WorkComplete :
                       focus_.phase() == FocusPhase::Break ? audio::Cue::BreakComplete : audio::Cue::TimerComplete;
    focus_.complete(focus_.phase() == FocusPhase::Work ? timer_.total() : 0);
    if (focus_.finished()) archive_focus(true);
    sync_focus();
    state_.timer_completed = true;
    display_activity_ms_ = animation_ms_; state_.display_asleep = false;
    personality_.react(state_, Mood::Alert, animation_ms_);
    return Effect::Render | Effect::Notify | Effect::Chirp;
}
Effects Core::set_theme(FaceTheme theme)
{
    if (!valid_theme(theme) || theme == state_.theme) return effect(Effect::None);
    activity();
    state_.theme = theme;
    return Effect::Render | Effect::Notify | Effect::Persist;
}
Effects Core::set_mode(Mode mode)
{
    if (!valid_mode(mode) || mode == Mode::Driving || state_.dock == Dock::Car ||
        mode == state_.mode) return effect(Effect::None);
    activity();
    state_.reference_playing = false;
    if (state_.screen == Screen::ReferenceTone) state_.screen = Screen::Home;
    if (state_.screen == Screen::DurationEdit || state_.screen == Screen::SettingEdit ||
        state_.screen == Screen::FocusSummary) state_.screen = Screen::Home;
    if ((focus_.phase() != FocusPhase::None || focus_.finished()) && mode != Mode::Focus) {
        timer_.cancel(); cancel_focus(); sync_timer(); sync_focus(); state_.timer_completed = false;
    }
    if (mode != Mode::Music) {
        state_.tuner_running = false;
        metronome_.stop(); sync_metronome();
        if (state_.screen == Screen::Music || state_.screen == Screen::Tuner) state_.screen = Screen::Home;
    }
    state_.mode = mode;
    if (mode != Mode::Focus && state_.utility_index >= 4) state_.utility_index = 1;
    return Effect::Render | Effect::Notify | Effect::Persist;
}
Effects Core::set_mood(Mood mood)
{
    if (!valid_mood(mood)) return effect(Effect::None);
    activity();
    personality_.react(state_, mood, animation_ms_);
    return Effect::Render | Effect::Notify;
}
Effects Core::navigate(EventKind kind)
{
    if (state_.dock == Dock::Car) return effect(Effect::None);
    activity();
    if (state_.screen == Screen::ReferenceTone) {
        if (kind == EventKind::Back || kind == EventKind::OpenMenu) {
            state_.reference_playing = false;
            state_.screen = kind == EventKind::OpenMenu ? Screen::Menu : Screen::Music;
        } else if (kind == EventKind::NavigateNext || kind == EventKind::NavigatePrevious) {
            state_.reference_index = static_cast<std::uint8_t>((state_.reference_index + 6 +
                (kind == EventKind::NavigateNext ? 1 : -1)) % 6);
        } else if (kind == EventKind::Select) {
            const auto note = state_.reference_note;
            switch (state_.reference_index) {
            case 0: return handle({EventKind::SetReferenceNote,note > 40U ? note-1U : 40U});
            case 1: return handle({EventKind::SetReferenceNote,note < 84U ? note+1U : 84U});
            case 2: return handle({state_.reference_playing ? EventKind::StopReference : EventKind::PlayReference,0});
            case 3: return handle({EventKind::SetReferenceNote,note > 51U ? note-12U : 40U});
            case 4: return handle({EventKind::SetReferenceNote,note < 73U ? note+12U : 84U});
            case 5: state_.reference_playing = false; state_.screen = Screen::Music; break;
            }
        }
        return effect(Effect::Render);
    }
    if (state_.screen == Screen::FocusSummary) {
        if (kind == EventKind::NavigateNext || kind == EventKind::NavigatePrevious) {
            const auto count = state_.focus_history_count;
            if (count) state_.focus_history_index = static_cast<std::uint8_t>((state_.focus_history_index + count +
                (kind == EventKind::NavigateNext ? 1 : -1)) % count);
        } else { state_.screen = kind == EventKind::OpenMenu ? Screen::Menu : Screen::Utility; }
        return effect(Effect::Render);
    }
    if (state_.screen == Screen::DurationEdit) return edit_duration(kind);
    if (state_.screen == Screen::SettingEdit) return edit_setting(kind);
    if ((state_.screen == Screen::Music || state_.screen == Screen::Tuner) &&
        (kind == EventKind::Back || kind == EventKind::OpenMenu)) {
        metronome_.stop(); sync_metronome();
        state_.tuner_running = false;
        state_.screen = Screen::Menu;
        return Effect::Render | Effect::Notify;
    }
    if (kind == EventKind::Back) {
        state_.screen = state_.screen == Screen::Menu || state_.screen == Screen::Home ? Screen::Home : Screen::Menu;
        return effect(Effect::Render);
    }
    if (kind == EventKind::OpenMenu || state_.screen == Screen::Home) {
        if (kind == EventKind::OpenMenu) state_.menu_index = 0;
        state_.screen = Screen::Menu;
        return effect(Effect::Render);
    }
    const int direction = kind == EventKind::NavigatePrevious ? -1 : 1;
    if (kind == EventKind::NavigateNext || kind == EventKind::NavigatePrevious) {
        if (state_.screen == Screen::Menu)
            state_.menu_index = static_cast<std::uint8_t>((state_.menu_index + 9 + direction) % 9);
        else if (state_.screen == Screen::Settings)
            state_.settings_index = static_cast<std::uint8_t>((state_.settings_index+11+direction)%11);
        else if (state_.screen == Screen::Utility) {
            const int count = state_.mode == Mode::Focus ? 8 : 4;
            state_.utility_index = static_cast<std::uint8_t>((state_.utility_index + count + direction) % count);
        }
        else if (state_.screen == Screen::Music)
            state_.music_index = static_cast<std::uint8_t>((state_.music_index + 8 + direction) % 8);
        else if (state_.screen == Screen::Themes) {
            const auto next = (static_cast<int>(state_.theme) + 8 + direction) % 8;
            return set_theme(static_cast<FaceTheme>(next)) | Effect::Render;
        }
        return effect(Effect::Render);
    }
    if (kind != EventKind::Select) return effect(Effect::None);
    if (state_.screen == Screen::Menu) {
        if (state_.menu_index == 6) state_.screen = Screen::Themes;
        else if (state_.menu_index == 7) return handle({EventKind::OpenSettings,0});
        else if (state_.menu_index == 8 || state_.menu_index == 2) {
            state_.screen = Screen::Home;
            return set_mode(Mode::Companion) | Effect::Render;
        } else {
            const auto mode = menu_modes[state_.menu_index];
            const auto effects = set_mode(mode);
            state_.utility_index = 1;
            state_.screen = mode == Mode::Music ? Screen::Music :
                feature_for(mode)->prototype_ready ? Screen::Utility : Screen::Unavailable;
            return effects | Effect::Render;
        }
    } else if (state_.screen == Screen::Settings) {
        switch (state_.settings_index) {
        case 0: return open_setting(SettingKey::Theme);
        case 1: return open_setting(SettingKey::Mute);
        case 2: return open_setting(SettingKey::Volume);
        case 3: return open_duration_editor(false);
        case 4: return open_duration_editor(true);
        case 5: return open_setting(SettingKey::Motion);
        case 6: return handle({EventKind::TestSound,static_cast<unsigned>(audio::Cue::TimerComplete)});
        case 7: return open_setting(SettingKey::Brightness);
        case 8: return open_setting(SettingKey::DisplayTimeout);
        case 9: return handle({EventKind::SleepDisplay,0});
        case 10: state_.screen = Screen::Menu; break;
        }
    } else if (state_.screen == Screen::Tuner) {
        return handle({state_.tuner_running ? EventKind::StopTuner : EventKind::StartTuner,0});
    } else if (state_.screen == Screen::Music) {
        switch (state_.music_index) {
        case 0: return handle({EventKind::SetMetronomeBpm, state_.metronome_bpm > 34U ? state_.metronome_bpm - 5U : 30U});
        case 1: return handle({EventKind::SetMetronomeBpm, state_.metronome_bpm < 236U ? state_.metronome_bpm + 5U : 240U});
        case 2: return handle({state_.metronome_running ? EventKind::StopMetronome : EventKind::StartMetronome, 0});
        case 3: return handle({EventKind::TapTempo, 0});
        case 4: return handle({EventKind::SetMetronomeBeats, state_.beats_per_bar % 8U + 1U});
        case 5: return handle({EventKind::StartTuner,0});
        case 6: return handle({EventKind::OpenReference,0});
        case 7: state_.screen = Screen::Home; return set_mode(Mode::Companion) | Effect::Render;
        }
    } else if (state_.screen == Screen::Themes) state_.screen = Screen::Home;
    else if (state_.screen == Screen::Unavailable) state_.screen = Screen::Menu;
    else if (state_.screen == Screen::Utility) {
        switch (state_.utility_index) {
        case 0: return open_duration_editor(false);
        case 1:
            return handle({state_.timer_running ? EventKind::PauseTimer :
                           state_.timer_paused ? EventKind::ResumeTimer : EventKind::StartTimer,
                           state_.configured_duration_seconds}) | Effect::Render;
        case 2: return handle({EventKind::CancelTimer, 0}) | Effect::Render;
        case 3: state_.screen = Screen::Home; break;
        case 4: return handle({state_.focus_waiting ? EventKind::NextFocusPhase : EventKind::StartFocusCycle, 0});
        case 5: return open_duration_editor(true);
        case 6: return open_setting(SettingKey::FocusGoal);
        case 7: return handle({EventKind::OpenFocusSummary,0});
        }
    }
    return effect(Effect::Render);
}
Effects Core::handle(const Event &event)
{
    // A sleeping screen consumes the first physical-style input as wake only.
    if (state_.display_asleep && (event.kind == EventKind::PrimaryPressed ||
        event.kind == EventKind::SecondaryPressed || event.kind == EventKind::FocusButtonTapped ||
        event.kind == EventKind::FocusButtonHeld || event.kind == EventKind::NavigateNext ||
        event.kind == EventKind::NavigatePrevious || event.kind == EventKind::Select || event.kind == EventKind::Back)) {
        activity(); return Effect::Render | Effect::Notify;
    }
    switch (event.kind) {
    case EventKind::AudioReady:
        if (event.value > 1) return effect(Effect::None);
        state_.audio_ready = event.value != 0;
        if (!state_.audio_ready) state_.reference_playing = false;
        return effect(Effect::Render);
    case EventKind::OpenReference: {
        if (state_.dock == Dock::Car) return effect(Effect::None);
        const auto effects = set_mode(Mode::Music); metronome_.stop(); sync_metronome();
        state_.tuner_running = false; state_.reference_playing = false;
        state_.screen = Screen::ReferenceTone; state_.reference_index = 2; activity();
        return effects | Effect::Render | Effect::Notify;
    }
    case EventKind::SetReferenceNote:
        if (event.value < 40 || event.value > 84 || event.value == state_.reference_note) return effect(Effect::None);
        state_.reference_playing = false; state_.reference_note = static_cast<std::uint8_t>(event.value);
        activity(); return effect(Effect::Render);
    case EventKind::PlayReference: {
        if (state_.dock == Dock::Car || !state_.audio_ready || state_.sound_muted ||
            state_.sound_volume_percent == 0 || state_.reference_playing || state_.timer_completed) return effect(Effect::None);
        const auto effects = set_mode(Mode::Music); metronome_.stop(); sync_metronome(); state_.tuner_running = false;
        state_.reference_playing = true; reference_started_ms_ = music_ms_;
        state_.sound_cue = audio::Cue::ReferenceTone; state_.screen = Screen::ReferenceTone; activity();
        return effects | Effect::Render | Effect::Chirp | Effect::Notify;
    }
    case EventKind::StopReference:
        if (!state_.reference_playing) return effect(Effect::None);
        state_.reference_playing = false; return effect(Effect::Render);
    case EventKind::SetFocusGoal:
        if (event.value > 12 || state_.focus_goal == event.value) return effect(Effect::None);
        state_.focus_goal = static_cast<std::uint8_t>(event.value); activity();
        return effect(Effect::Render);
    case EventKind::OpenFocusSummary:
        if (state_.mode != Mode::Focus || state_.dock == Dock::Car) return effect(Effect::None);
        activity(); state_.screen = Screen::FocusSummary; state_.focus_history_index = 0;
        return effect(Effect::Render);
    case EventKind::SetBrightness:
        if (event.value < 5 || event.value > 100 || event.value == state_.display_brightness_percent)
            return effect(Effect::None);
        state_.display_brightness_percent = static_cast<std::uint8_t>(event.value);
        activity(); return Effect::Render | Effect::Persist;
    case EventKind::SetDisplayTimeout:
        if (event.value > 30 || event.value == state_.display_timeout_minutes) return effect(Effect::None);
        state_.display_timeout_minutes = static_cast<std::uint8_t>(event.value);
        activity(); return Effect::Render | Effect::Persist;
    case EventKind::SleepDisplay:
        if (state_.timer_completed || state_.tuner_running || state_.metronome_running || state_.reference_playing)
            return effect(Effect::None);
        state_.screen = Screen::Home; state_.display_asleep = true;
        return effect(Effect::Render);
    case EventKind::Boot:
        state_.mode = Mode::Companion;
        state_.screen = Screen::Home;
        state_.timer_completed = false;
        state_.tuner_running = false; state_.reference_playing = false;
        state_.reference_note = 69;
        state_.motion_settled = false;
        timer_.cancel(); focus_.cancel();
        state_.focus_history = {}; state_.focus_history_count = 0; state_.focus_goal = 0;
        metronome_ = Metronome{}; sync_metronome(); sync_timer(); sync_focus(); activity();
        return Effect::Render | Effect::Notify;
    case EventKind::Tick: return on_tick(event.value);
    case EventKind::AnimationTick: {
        animation_ms_ = event.value;
        const bool was_asleep = state_.display_asleep;
        if (display::auto_sleep_allowed(state_) &&
            animation_ms_ - display_activity_ms_ >= state_.display_timeout_minutes * 60000U)
            state_.display_asleep = true;
        const auto before = state_.mood;
        const bool changed = personality_.advance(state_, animation_ms_);
        Effects effects = changed || was_asleep != state_.display_asleep ? effect(Effect::Render) : effect(Effect::None);
        if (before != state_.mood) effects |= effect(Effect::Notify);
        if (state_.timer_completed && state_.mood != Mood::Alert) {
            state_.timer_completed = false; state_.screen = focus_.finished() ? Screen::FocusSummary : Screen::Home;
            effects |= effect(Effect::Render);
        }
        return effects;
    }
    case EventKind::PrimaryPressed:
        state_.sound_cue = audio::Cue::Interact;
        activity();
        if (state_.interaction_count != std::numeric_limits<std::uint32_t>::max()) ++state_.interaction_count;
        personality_.react(state_, Mood::Happy, animation_ms_);
        return Effect::Render | Effect::Notify | Effect::Persist | Effect::Chirp;
    case EventKind::SecondaryPressed: return navigate(EventKind::NavigateNext);
    case EventKind::OpenMenu: case EventKind::NavigateNext:
    case EventKind::NavigatePrevious: case EventKind::Select: case EventKind::Back:
        return navigate(event.kind);
    case EventKind::FocusButtonTapped: {
        if (state_.screen != Screen::Home) return navigate(EventKind::Select);
        if (state_.focus_waiting) return handle({EventKind::NextFocusPhase, 0});
        if (state_.timer_running || state_.timer_paused) return handle({EventKind::PrimaryPressed, 0});
        if (state_.dock == Dock::Car) return effect(Effect::None);
        Effects effects = set_mode(Mode::Focus);
        effects |= handle({EventKind::StartTimer, state_.configured_duration_seconds});
        effects |= handle({EventKind::PrimaryPressed, 0});
        return effects;
    }
    case EventKind::FocusButtonHeld: {
        if (state_.screen != Screen::Home) return navigate(EventKind::Back);
        if (state_.dock == Dock::Car || state_.mode != Mode::Focus ||
            (!state_.timer_running && !state_.timer_paused && !state_.focus_waiting)) return effect(Effect::None);
        Effects effects = handle({EventKind::CancelTimer, 0});
        effects |= set_mode(Mode::Companion);
        effects |= set_mood(Mood::Neutral);
        return effects;
    }
    case EventKind::Connected:
        if (state_.connected) return effect(Effect::None);
        state_.connected = true; return Effect::Render | Effect::Notify;
    case EventKind::Disconnected:
        if (!state_.connected) return effect(Effect::None);
        state_.connected = false; return Effect::Render | Effect::Notify;
    case EventKind::DockChanged: {
        if (event.value >= static_cast<std::uint32_t>(Dock::Count)) return effect(Effect::None);
        const auto dock = static_cast<Dock>(event.value);
        if (dock == state_.dock) return effect(Effect::None);
        const auto previous = state_.dock;
        state_.reference_playing = false;
        state_.dock = dock;
        state_.screen = Screen::Home;
        state_.tuner_running = false;
        metronome_.stop(); sync_metronome();
        if (dock == Dock::Car) {
            if (focus_.phase() != FocusPhase::None || focus_.finished()) {
                timer_.cancel(); cancel_focus(); sync_timer(); sync_focus(); state_.timer_completed = false;
            }
            mode_before_car_dock_ = state_.mode;
            state_.mode = Mode::Driving;
        } else if (previous == Dock::Car && state_.mode == Mode::Driving)
            state_.mode = mode_before_car_dock_;
        return Effect::Render | Effect::Notify | Effect::Persist;
    }
    case EventKind::SetTheme:
        return event.value < static_cast<unsigned>(FaceTheme::Count) ? set_theme(static_cast<FaceTheme>(event.value)) : effect(Effect::None);
    case EventKind::CycleTheme:
        return set_theme(static_cast<FaceTheme>((static_cast<unsigned>(state_.theme) + 1) % 8));
    case EventKind::SetMode:
        return event.value < static_cast<unsigned>(Mode::Count) ? set_mode(static_cast<Mode>(event.value)) : effect(Effect::None);
    case EventKind::SetMood:
        return event.value < static_cast<unsigned>(Mood::Count) ? set_mood(static_cast<Mood>(event.value)) : effect(Effect::None);
    case EventKind::SetBattery:
        if ((event.value > 100 && event.value != kBatteryUnknown) || event.value == state_.battery_percent) return effect(Effect::None);
        state_.battery_percent = static_cast<std::uint8_t>(event.value);
        return Effect::Render | Effect::Notify;
    case EventKind::SetTimerDuration:
        if (!event.value || event.value > kMaxTimerSeconds || event.value == state_.configured_duration_seconds) return effect(Effect::None);
        activity(); state_.configured_duration_seconds = event.value;
        return Effect::Render | Effect::Persist;
    case EventKind::StartTimer:
        if (!timer_.start(event.value, state_.uptime_seconds)) return effect(Effect::None);
        cancel_focus(); sync_focus();
        activity(); state_.timer_completed = false; sync_timer();
        return Effect::Render | Effect::Notify;
    case EventKind::PauseTimer: {
        if (!timer_.running()) return effect(Effect::None);
        activity();
        const bool finished = timer_.pause(state_.uptime_seconds);
        sync_timer();
        return finished ? timer_complete() : Effect::Render | Effect::Notify;
    }
    case EventKind::ResumeTimer:
        if (!timer_.resume(state_.uptime_seconds)) return effect(Effect::None);
        activity(); sync_timer(); return Effect::Render | Effect::Notify;
    case EventKind::CancelTimer:
        if (!timer_.running() && !timer_.paused() && !state_.timer_completed && focus_.phase() == FocusPhase::None) return effect(Effect::None);
        timer_.cancel(); cancel_focus(); sync_timer(); sync_focus(); activity(); state_.timer_completed = false;
        return Effect::Render | Effect::Notify;
    case EventKind::StartFocusCycle: {
        if (state_.dock == Dock::Car || timer_.running() || timer_.paused() ||
            focus_.phase() != FocusPhase::None) return effect(Effect::None);
        const auto effects = set_mode(Mode::Focus);
        if (state_.screen == Screen::FocusSummary) state_.screen = Screen::Utility;
        focus_.start(state_.focus_goal);
        return effects | start_focus_phase();
    }
    case EventKind::NextFocusPhase:
        if (state_.dock == Dock::Car || !focus_.next()) return effect(Effect::None);
        return start_focus_phase();
    case EventKind::SetBreakDuration:
        if (!event.value || event.value > kMaxTimerSeconds || event.value == state_.break_duration_seconds)
            return effect(Effect::None);
        state_.break_duration_seconds = event.value; activity(); return Effect::Render | Effect::Notify | Effect::Persist;
    case EventKind::TestSound:
        if (state_.tuner_running || state_.sound_muted || state_.sound_volume_percent == 0 || event.value >= static_cast<unsigned>(audio::Cue::MetronomeClick)) return effect(Effect::None);
        state_.sound_cue = static_cast<audio::Cue>(event.value); return effect(Effect::Chirp);
    case EventKind::SetMuted:
        if (event.value > 1 || state_.sound_muted == (event.value != 0)) return effect(Effect::None);
        if (event.value) state_.reference_playing = false;
        state_.sound_muted = event.value != 0; return Effect::Render | Effect::Notify | Effect::Persist;
    case EventKind::MusicTick: {
        music_ms_ = event.value;
        const bool ended = state_.reference_playing && music_ms_ - reference_started_ms_ >= 2000U;
        if (ended) state_.reference_playing = false;
        const bool previous_pulse = state_.beat_pulse;
        const bool due = metronome_.advance(music_ms_);
        sync_metronome();
        if (due) return music_beat();
        return ended || previous_pulse != state_.beat_pulse ? effect(Effect::Render) : effect(Effect::None);
    }
    case EventKind::StartMetronome: {
        state_.reference_playing = false;
        if (state_.dock == Dock::Car || metronome_.running()) return effect(Effect::None);
        const auto effects = set_mode(Mode::Music);
        state_.screen = Screen::Music;
        state_.tuner_running = false;
        metronome_.start(music_ms_); sync_metronome(); activity();
        state_.timer_completed = false;
        personality_.react(state_, Mood::Neutral, animation_ms_);
        return effects | music_beat() | Effect::Notify;
    }
    case EventKind::StopMetronome:
        if (!metronome_.running()) return effect(Effect::None);
        metronome_.stop(); sync_metronome(); activity(); return Effect::Render | Effect::Notify;
    case EventKind::SetMetronomeBpm:
        if (!metronome_.set_bpm(event.value)) return effect(Effect::None);
        sync_metronome(); activity(); return Effect::Render | Effect::Notify;
    case EventKind::SetMetronomeBeats:
        if (!metronome_.set_beats(event.value)) return effect(Effect::None);
        sync_metronome(); activity(); return Effect::Render | Effect::Notify;
    case EventKind::TapTempo:
        metronome_.tap(music_ms_); sync_metronome(); activity(); return Effect::Render | Effect::Notify;
    case EventKind::StartTuner: {
        state_.reference_playing = false;
        if (state_.dock == Dock::Car || state_.tuner_running) return effect(Effect::None);
        const auto effects = set_mode(Mode::Music);
        metronome_.stop(); sync_metronome();
        state_.screen = Screen::Tuner;
        state_.timer_completed = false;
        state_.tuner_running = state_.microphone_ready;
        ++state_.tuner_session;
        state_.tuner = {};
        state_.tuner.signal = state_.microphone_ready ? pitch::Signal::Listening : pitch::Signal::Unavailable;
        activity();
        return effects | Effect::Render | Effect::Notify;
    }
    case EventKind::StopTuner:
        if (!state_.tuner_running) return effect(Effect::None);
        state_.tuner_running = false; state_.tuner = {};
        activity(); return Effect::Render | Effect::Notify;
    case EventKind::MicrophoneReady:
        if (event.value > 1) return effect(Effect::None);
        state_.microphone_ready = event.value != 0;
        if (!state_.microphone_ready) { state_.tuner_running = false; state_.tuner.signal = pitch::Signal::Unavailable; }
        return Effect::Render | Effect::Notify;
    case EventKind::TunerReading: {
        pitch::Reading reading; std::uint8_t session;
        if (!state_.tuner_running || !pitch::unpack(event.value,reading,session) || session != state_.tuner_session)
            return effect(Effect::None);
        state_.tuner = reading;
        return effect(Effect::Render);
    }
    case EventKind::MotionReady:
        if (event.value > 1) return effect(Effect::None);
        state_.motion_ready = event.value != 0;
        state_.motion_settled = false;
        return Effect::Render | Effect::Notify;
    case EventKind::SetMotionConfig:
        if (event.value > motion::kAll || state_.motion_config == event.value) return effect(Effect::None);
        state_.motion_config = static_cast<std::uint8_t>(event.value);
        state_.motion_settled = false;
        return Effect::Render | Effect::Notify | Effect::Persist;
    case EventKind::MotionSettled: case EventKind::MotionActive:
        if (!state_.motion_ready || !state_.motion_config) return effect(Effect::None);
        state_.motion_settled = event.kind == EventKind::MotionSettled;
        return Effect::Render | Effect::Notify;
    case EventKind::MotionPickup: case EventKind::MotionRock: case EventKind::MotionRest: {
        const unsigned flag = event.kind == EventKind::MotionPickup ? motion::kPickup :
            event.kind == EventKind::MotionRock ? motion::kRock : motion::kRest;
        if (!state_.motion_ready || !(state_.motion_config & flag) || state_.dock != Dock::None ||
            state_.mode != Mode::Companion || state_.screen != Screen::Home ||
            state_.timer_running || state_.timer_paused || state_.timer_completed || state_.focus_waiting ||
            state_.tuner_running || state_.metronome_running || state_.mood == Mood::Happy || state_.mood == Mood::Alert ||
            static_cast<std::int32_t>(animation_ms_-event.value) > 500) return effect(Effect::None);
        animation_ms_ = event.value;
        if (event.kind == EventKind::MotionRest) {
            if (state_.mood == Mood::Sleepy || personality_.inactive_for(animation_ms_) < 60000U)
                return effect(Effect::None);
            personality_.rest(state_);
        } else {
            activity();
            personality_.react(state_,event.kind == EventKind::MotionRock ? Mood::Happy : Mood::Curious,
                               animation_ms_,event.kind == EventKind::MotionRock ? 3000U : 2000U);
        }
        return Effect::Render | Effect::Notify;
    }
    case EventKind::SetVolume:
        if (event.value > 100 || state_.sound_volume_percent == event.value) return effect(Effect::None);
        if (!event.value) state_.reference_playing = false;
        state_.sound_volume_percent = static_cast<std::uint8_t>(event.value);
        return Effect::Render | Effect::Notify | Effect::Persist;
    case EventKind::OpenSettings:
        state_.reference_playing = false;
        if (state_.dock == Dock::Car) return effect(Effect::None);
        metronome_.stop(); sync_metronome(); state_.tuner_running = false;
        state_.screen = Screen::Settings; activity(); return Effect::Render | Effect::Notify;
    case EventKind::RequestStatus: return effect(Effect::Notify);
    }
    return effect(Effect::None);
}
Effects Core::on_tick(std::uint32_t now)
{
    animation_ms_ = now * 1000U;
    state_.uptime_seconds = now;
    const auto previous = state_.timer_remaining_seconds;
    const bool complete = timer_.advance(now);
    sync_timer();
    Effects effects = effect(Effect::None);
    if (previous != state_.timer_remaining_seconds) effects |= Effect::Render | Effect::Notify;
    if (complete) effects |= timer_complete();
    effects |= handle({EventKind::AnimationTick, animation_ms_});
    return effects;
}
PersistedState Core::persistent_state() const
{
    return {state_.mode == Mode::Driving ? mode_before_car_dock_ : state_.mode,
            Mood::Neutral, state_.interaction_count, state_.theme,
            state_.configured_duration_seconds,state_.break_duration_seconds,state_.sound_muted,
            state_.sound_volume_percent,state_.motion_config,
            state_.display_brightness_percent,state_.display_timeout_minutes};
}
bool Core::restore(const PersistedState &p)
{
    if (!valid_mode(p.mode) || p.mode == Mode::Driving || !valid_mood(p.mood) ||
        !valid_theme(p.theme) || !p.configured_duration_seconds ||
        p.configured_duration_seconds > kMaxTimerSeconds || !p.break_duration_seconds ||
        p.break_duration_seconds > kMaxTimerSeconds || p.sound_volume_percent > 100 || p.motion_config > motion::kAll ||
        p.display_brightness_percent < 5 || p.display_brightness_percent > 100 || p.display_timeout_minutes > 30) return false;
    state_.mode = p.mode; state_.mood = p.mood;
    state_.theme = p.theme; state_.interaction_count = p.interaction_count;
    state_.configured_duration_seconds = p.configured_duration_seconds;
    state_.break_duration_seconds = p.break_duration_seconds; state_.sound_muted = p.sound_muted;
    state_.sound_volume_percent = p.sound_volume_percent; state_.motion_config = p.motion_config;
    state_.display_brightness_percent = p.display_brightness_percent;
    state_.display_timeout_minutes = p.display_timeout_minutes;
    mode_before_car_dock_ = p.mode;
    return true;
}
}
