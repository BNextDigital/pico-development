#include <pico/core.hpp>
#include <algorithm>

namespace pico {
namespace {
constexpr std::uint8_t repeat_masks[]{0,127,31,96,1,2,4,8,16,32,64};
bool clock_visible(const State &s)
{
    return s.screen==Screen::Clock || (s.screen==Screen::Home &&
        (s.clock.settings.glance || (s.clock.settings.usb_desk && s.clock.usb_powered)));
}
}
Effects Core::clock_event(const Event &e)
{
    auto &s=state_.clock;
    switch (e.kind) {
    case EventKind::OpenClock:
        if (state_.dock==Dock::Car) return effect(Effect::None);
        state_.screen=Screen::Clock; state_.clock_index=0; activity();
        return effect(Effect::Render);
    case EventKind::SyncTime: {
        if (e.auxiliary>1680U) return effect(Effect::None);
        const auto before=clock::encode(s.settings);
        if (!clock_.synchronize(s,e.value,static_cast<int>(e.auxiliary)-840)) return effect(Effect::None);
        Effects effects=effect(Effect::NotifyClock);
        if (before!=clock::encode(s.settings)) effects|=effect(Effect::PersistClock);
        if (!state_.display_asleep && clock_visible(state_)) effects|=effect(Effect::Render);
        return effects;
    }
    case EventKind::SetClockOptions:
        if (e.value>7) return effect(Effect::None);
        if (e.value==((s.settings.hour24 ? 1U : 0U)|(s.settings.glance ? 2U : 0U)|(s.settings.usb_desk ? 4U : 0U)))
            return effect(Effect::None);
        s.settings.hour24=(e.value&1)!=0; s.settings.glance=(e.value&2)!=0; s.settings.usb_desk=(e.value&4)!=0;
        return Effect::Render | Effect::PersistClock | Effect::NotifyClock;
    case EventKind::ConfigureAlarm: {
        if ((e.auxiliary & ~0x017fffffU)!=0) return effect(Effect::None);
        clock::Alarm alarm;
        alarm.minute=static_cast<std::uint16_t>(e.auxiliary);
        alarm.weekdays=static_cast<std::uint8_t>((e.auxiliary>>16)&127U);
        alarm.enabled=(e.auxiliary & 0x01000000U)!=0;
        if (!clock_.configure(s,e.value,alarm)) return effect(Effect::None);
        return Effect::Render | Effect::PersistClock | Effect::NotifyClock;
    }
    case EventKind::DismissAlarm:
        if (!s.ringing) return effect(Effect::None);
        clock_.dismiss(s); ++state_.tuner_session;
        if (state_.timer_completed) {
            display_activity_ms_=animation_ms_; state_.display_asleep=false;
        } else {
            activity(); personality_.react(state_,Mood::Happy,animation_ms_,1500U);
        }
        return Effect::Render | Effect::NotifyClock;
    case EventKind::StopwatchStart: case EventKind::StopwatchPause:
    case EventKind::StopwatchResume: case EventKind::StopwatchReset: {
        const auto action=e.kind==EventKind::StopwatchStart ? clock::StopwatchAction::Start :
            e.kind==EventKind::StopwatchPause ? clock::StopwatchAction::Pause :
            e.kind==EventKind::StopwatchResume ? clock::StopwatchAction::Resume : clock::StopwatchAction::Reset;
        clock_.stopwatch(s,action);
        return Effect::Render | Effect::NotifyClock;
    }
    case EventKind::UsbPower:
        if (e.value>1 || s.usb_powered==(e.value!=0)) return effect(Effect::None);
        s.usb_powered=e.value!=0;
        return Effect::Render | Effect::NotifyClock;
    default: break;
    }
    return effect(Effect::None);
}
Effects Core::advance_clock(std::uint32_t now)
{
    const auto old_ringing=state_.clock.ringing;
    const auto old_stopwatch=state_.clock.stopwatch_seconds;
    const auto update=clock_.advance(state_.clock,now);
    Effects effects=effect(Effect::None);
    if (update.settings_changed) effects|=effect(Effect::PersistClock);
    if (update.alarm_changed || update.sync_changed) effects|=effect(Effect::NotifyClock);
    if (old_ringing!=state_.clock.ringing) { ++state_.tuner_session; effects|=effect(Effect::Render); }
    if (state_.clock.ringing && !state_.timer_completed) {
        state_.display_asleep=false;
        if (!old_ringing || now-alarm_signal_seconds_>=3U) {
            alarm_signal_seconds_=now;
            personality_.react(state_,Mood::Alert,animation_ms_,4000U);
            state_.sound_cue=audio::Cue::TimerComplete;
            effects|=Effect::Chirp | Effect::Render;
        }
    }
    if (!state_.display_asleep && ((update.minute_changed && clock_visible(state_)) ||
        (state_.screen==Screen::Stopwatch && old_stopwatch!=state_.clock.stopwatch_seconds))) effects|=effect(Effect::Render);
    return effects;
}
Effects Core::clock_navigation(EventKind kind)
{
    auto &s=state_;
    if (kind==EventKind::Back || kind==EventKind::OpenMenu) {
        if (kind==EventKind::OpenMenu || s.screen==Screen::Clock) s.screen=Screen::Menu;
        else if (s.screen==Screen::AlarmEdit) s.screen=Screen::Alarms;
        else s.screen=Screen::Clock;
        s.clock_index=0;
        return effect(Effect::Render);
    }
    const bool editing=s.screen==Screen::ClockEdit || s.screen==Screen::AlarmEdit;
    const auto count=editing ? 5U : s.screen==Screen::Clock ? 5U : s.screen==Screen::Alarms ? 5U :
        s.screen==Screen::ClockOptions ? 4U : 3U;
    auto &index=editing ? s.clock_action : s.clock_index;
    if (kind==EventKind::NavigateNext || kind==EventKind::NavigatePrevious) {
        index=static_cast<std::uint8_t>((index+count+(kind==EventKind::NavigateNext ? 1 : -1))%count);
        return effect(Effect::Render);
    }
    if (kind!=EventKind::Select) return effect(Effect::None);
    if (s.screen==Screen::Clock) {
        switch (index) {
        case 0: s.screen=Screen::Alarms; break;
        case 1: s.screen=Screen::Stopwatch; break;
        case 2:
            s.clock_draft=s.clock.valid ? clock::from_epoch(static_cast<std::int64_t>(s.clock.utc)+s.clock.settings.offset_minutes*60) : clock::Date{};
            s.clock_draft.second=0; s.clock_offset_draft=s.clock.settings.offset_minutes;
            s.clock_edit_field=0; s.clock_action=1; s.screen=Screen::ClockEdit; break;
        case 3: s.screen=Screen::ClockOptions; break;
        case 4: s.screen=Screen::Home; break;
        }
        s.clock_index=0;
    } else if (s.screen==Screen::ClockOptions) {
        if (index==3) { s.screen=Screen::Clock; s.clock_index=0; }
        else {
            const auto flags=(s.clock.settings.hour24 ? 1U : 0U)|(s.clock.settings.glance ? 2U : 0U)|(s.clock.settings.usb_desk ? 4U : 0U);
            return clock_event({EventKind::SetClockOptions,flags^(1U<<index)});
        }
    } else if (s.screen==Screen::Alarms) {
        if (index==4) { s.screen=Screen::Clock; s.clock_index=0; }
        else {
            s.alarm_index=index; s.alarm_draft=s.clock.settings.alarms[index];
            s.clock_edit_field=0; s.clock_action=1; s.screen=Screen::AlarmEdit;
        }
    } else if (s.screen==Screen::Stopwatch) {
        if (index==2) { s.screen=Screen::Clock; s.clock_index=0; }
        else if (index==1) return clock_event({EventKind::StopwatchReset,0});
        else return clock_event({s.clock.stopwatch_running ? EventKind::StopwatchPause :
            s.clock.stopwatch_seconds ? EventKind::StopwatchResume : EventKind::StopwatchStart,0});
    } else if (editing) {
        if (index==4) { s.screen=s.screen==Screen::AlarmEdit ? Screen::Alarms : Screen::Clock; s.clock_index=0; }
        else if (index==2) s.clock_edit_field=static_cast<std::uint8_t>((s.clock_edit_field+1)%(s.screen==Screen::ClockEdit ? 6 : 4));
        else if (index==3) {
            Effects effects=effect(Effect::None);
            if (s.screen==Screen::ClockEdit) {
                std::uint32_t local;
                if (!clock::to_epoch(s.clock_draft,local)) return effect(Effect::None);
                const auto utc=static_cast<std::int64_t>(local)-s.clock_offset_draft*60;
                if (utc<clock::kMinUtc || utc>clock::kMaxUtc || !clock::valid_time(static_cast<std::uint32_t>(utc),s.clock_offset_draft)) return effect(Effect::None);
                effects=clock_event({EventKind::SyncTime,static_cast<std::uint32_t>(utc),static_cast<std::uint32_t>(s.clock_offset_draft+840)});
                s.screen=Screen::Clock;
            } else {
                effects=clock_event({EventKind::ConfigureAlarm,s.alarm_index,
                    s.alarm_draft.minute | (std::uint32_t(s.alarm_draft.weekdays)<<16) | (s.alarm_draft.enabled ? 0x01000000U : 0U)});
                s.screen=Screen::Alarms;
            }
            s.clock_index=0; return effects | Effect::Render;
        } else {
            const int direction=index==0 ? -1 : 1;
            const auto cycle=[&](unsigned value,unsigned minimum,unsigned maximum) {
                return static_cast<unsigned>((static_cast<int>(value-minimum)+static_cast<int>(maximum-minimum+1)+direction)%
                    static_cast<int>(maximum-minimum+1))+minimum;
            };
            if (s.screen==Screen::ClockEdit) {
                auto &d=s.clock_draft;
                switch (s.clock_edit_field) {
                case 0: d.year=cycle(d.year,2000,2099); break;
                case 1: d.month=cycle(d.month,1,12); break;
                case 2: d.day=cycle(d.day,1,clock::days_in_month(d.year,d.month)); break;
                case 3: d.hour=cycle(d.hour,0,23); break;
                case 4: d.minute=cycle(d.minute,0,59); break;
                case 5: s.clock_offset_draft=static_cast<std::int16_t>(std::clamp<int>(s.clock_offset_draft+direction*15,-840,840)); break;
                }
                d.day=std::min(d.day,clock::days_in_month(d.year,d.month));
            } else {
                auto &a=s.alarm_draft;
                if (s.clock_edit_field==0) a.minute=static_cast<std::uint16_t>(cycle(a.minute/60,0,23)*60+a.minute%60);
                if (s.clock_edit_field==1) a.minute=static_cast<std::uint16_t>((a.minute/60)*60+cycle(a.minute%60,0,59));
                if (s.clock_edit_field==2) {
                    unsigned position=0;
                    for (unsigned i=0;i<11;++i) if (repeat_masks[i]==a.weekdays) position=i;
                    a.weekdays=repeat_masks[cycle(position,0,10)];
                }
                if (s.clock_edit_field==3) a.enabled=!a.enabled;
            }
        }
    }
    return effect(Effect::Render);
}
} // namespace pico
