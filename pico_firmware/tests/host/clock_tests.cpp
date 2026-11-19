#include <pico/core.hpp>
#include <pico/protocol.hpp>
#include <pico/display_model.hpp>
#include <pico/display_power.hpp>
#include <array>
#include <cassert>
#include <cstring>
#include <iostream>
using pico::EventKind;
namespace {
std::uint32_t epoch(unsigned y,unsigned m,unsigned d,unsigned h=0,unsigned minute=0,unsigned second=0)
{
    std::uint32_t utc{};
    assert(pico::clock::to_epoch({y,m,d,h,minute,second,0},utc)); return utc;
}
pico::Event sync(std::uint32_t utc,int offset=0)
{ return {EventKind::SyncTime,utc,static_cast<std::uint32_t>(offset+840)}; }
pico::Event alarm(unsigned slot,unsigned minute,unsigned days=0,bool enabled=true)
{ return {EventKind::ConfigureAlarm,slot,minute|(days<<16)|(enabled ? 0x01000000U : 0U)}; }
void calendar()
{
    using namespace pico::clock;
    assert(leap(2000)); assert(!leap(2100)); assert(days_in_month(2024,2)==29);
    std::uint32_t ignored;
    assert(!to_epoch({2023,2,29,0,0,0,0},ignored));
    assert(!to_epoch({2100,1,1,0,0,0,0},ignored));
    for (std::uint64_t day=kMinUtc;day<=kMaxUtc;day+=86400) {
        const auto date=from_epoch(static_cast<std::int64_t>(day));
        std::uint32_t decoded{}; assert(to_epoch(date,decoded)); assert(decoded==day);
    }
    auto d=from_epoch(epoch(2026,11,19,23,59,59)); assert(d.weekday==3);
    d=from_epoch(epoch(2026,12,31,23,59,59)+1); assert(d.year==2027 && d.day==1 && d.month==1);
    char text[24];
    d=from_epoch(epoch(2026,11,19)); format_time(text,sizeof(text),d,false); assert(!std::strcmp(text,"12:00"));
    d.hour=12; format_time(text,sizeof(text),d,false); assert(!std::strcmp(text,"12:00"));
    d.hour=23; format_time(text,sizeof(text),d,false); assert(!std::strcmp(text,"11:00"));
    format_time(text,sizeof(text),d,true); assert(!std::strcmp(text,"23:00"));
    assert(!valid_time(kMinUtc,-1)); assert(!valid_time(kMaxUtc,1));
    assert(!valid_time(epoch(2026,1,1),841));
}
void time_and_stopwatch()
{
    pico::Core c;
    c.handle({EventKind::Boot,0}); assert(!c.state().clock.valid);
    assert(c.state().clock.sync_due);
    c.handle({EventKind::Tick,10}); c.handle(sync(epoch(2026,11,19,23,59,55)));
    c.handle({EventKind::StopwatchStart,0}); c.handle({EventKind::StartTimer,30});
    c.handle({EventKind::Tick,20}); assert(c.state().clock.stopwatch_seconds==10);
    assert(c.state().timer_remaining_seconds==20);
    c.handle(sync(epoch(2025,1,1),330)); // Correct backwards and change timezone.
    assert(c.state().clock.stopwatch_seconds==10); assert(c.state().timer_remaining_seconds==20);
    c.handle({EventKind::Disconnected,0}); c.handle({EventKind::Tick,25});
    assert(c.state().clock.utc==epoch(2025,1,1)+5); assert(c.state().clock.stopwatch_seconds==15);
    c.handle({EventKind::StopwatchPause,0}); c.handle({EventKind::Tick,35});
    assert(c.state().clock.stopwatch_seconds==15);
    c.handle({EventKind::StopwatchResume,0}); c.handle({EventKind::Tick,40});
    assert(c.state().clock.stopwatch_seconds==20);
    c.handle({EventKind::StopwatchReset,0}); assert(c.state().clock.stopwatch_seconds==0);
    c.handle({EventKind::Tick,86420}); assert(c.state().clock.sync_due);
    c.handle(sync(epoch(2026,11,20),-300)); assert(!c.state().clock.sync_due);
    const auto unchanged=c.handle(sync(epoch(2026,11,20,0,1),-300));
    assert(!pico::has_effect(unchanged,pico::Effect::PersistClock));
    const auto utc=c.state().clock.utc;
    c.handle({EventKind::SyncTime,0,840}); assert(c.state().clock.utc==utc);
    c.handle({EventKind::Boot,0}); assert(!c.state().clock.valid); assert(c.state().clock.stopwatch_seconds==0);
}
void alarms()
{
    using namespace pico::clock;
    Engine engine; State state;
    assert(engine.synchronize(state,epoch(2026,11,19,6,59,58),0)); // Thursday.
    assert(engine.configure(state,0,{420,127,true,-1}));
    assert(engine.configure(state,1,{420,0,true,-1}));
    const auto result=engine.advance(state,2);
    assert(state.ringing==3); assert(result.settings_changed);
    assert(state.settings.alarms[0].enabled); assert(!state.settings.alarms[1].enabled);
    engine.dismiss(state); assert(!state.ringing);
    engine.synchronize(state,epoch(2026,11,19,6,30),0); // DST/back correction must not replay same-day repeat.
    engine.advance(state,1802); assert(!state.ringing);
    engine.synchronize(state,epoch(2026,11,20,6,59,59),0);
    engine.advance(state,1803); assert(state.ringing==1);
    engine.advance(state,1863); assert(!state.ringing); // Bounded 60-second alert.
    // Weekday-only recurrence skips weekend.
    engine.configure(state,0,{420,31,true,-1});
    engine.synchronize(state,epoch(2026,11,21,6,59,59),0);
    engine.advance(state,1864); assert(!state.ringing);
    // Offset update reschedules in local time. 12:00 UTC = 07:00 at -5h.
    engine.configure(state,2,{420,127,true,-1});
    engine.synchronize(state,epoch(2026,11,23,11,59,59),-300);
    engine.advance(state,1865); assert(state.ringing & 4);
    // A correction jumping past an alarm does not trigger a retrospective alarm.
    engine.dismiss(state);
    engine.configure(state,3,{480,0,true,-1});
    engine.synchronize(state,epoch(2026,11,23,14),-300);
    assert(!state.ringing && !state.settings.alarms[3].enabled);
    // Alarm still fires after ordinary scheduler lateness, but not a many-hour gap.
    engine.configure(state,2,{600,127,true,-1});
    engine.synchronize(state,epoch(2026,11,23,14,59,59),-300);
    engine.advance(state,1870); assert(state.ringing & 4);
    engine.dismiss(state); engine.configure(state,1,{601,0,true,-1});
    engine.advance(state,10000); assert(!state.ringing && !state.settings.alarms[1].enabled);
}
void persistence()
{
    using namespace pico::clock;
    Settings settings; settings.hour24=false; settings.glance=true; settings.offset_minutes=-300;
    settings.alarms[0]={400,31,true,-1};
    const auto blob=encode(settings); Settings restored;
    assert(decode(blob.data(),blob.size(),restored)); assert(!restored.hour24 && restored.glance);
    assert(restored.alarms[0].minute==400 && restored.alarms[0].weekdays==31);
    for (std::size_t n=0;n<blob.size();++n) assert(!decode(blob.data(),n,restored));
    for (std::size_t n=0;n<blob.size();++n) { auto corrupt=blob; corrupt[n]^=1; assert(!decode(corrupt.data(),corrupt.size(),restored)); }
    pico::Core c; assert(c.restore_clock(settings)); c.handle({EventKind::Boot,0});
    assert(!c.state().clock.valid && c.state().clock.settings.alarms[0].enabled);
    c.handle(sync(epoch(2026,11,19,12),-300)); assert(!c.state().clock.ringing);
}
void controls_power_concurrency()
{
    pico::Core c;
    c.handle({EventKind::Boot,0}); c.handle(sync(epoch(2026,11,19,6,59,59)));
    c.handle({EventKind::OpenClock,0}); assert(c.state().screen==pico::Screen::Clock);
    c.handle({EventKind::Select,0}); assert(c.state().screen==pico::Screen::Alarms);
    c.handle({EventKind::Select,0}); assert(c.state().screen==pico::Screen::AlarmEdit);
    c.handle({EventKind::Back,0}); assert(c.state().screen==pico::Screen::Alarms);
    c.handle({EventKind::Back,0}); assert(c.state().screen==pico::Screen::Clock);
    c.handle({EventKind::NavigateNext,0}); c.handle({EventKind::Select,0});
    assert(c.state().screen==pico::Screen::Stopwatch);
    c.handle({EventKind::FocusButtonTapped,0}); assert(c.state().clock.stopwatch_running);
    c.handle(alarm(0,420)); c.handle({EventKind::Tick,1});
    assert(c.state().clock.ringing && !c.state().display_asleep);
    assert(c.next_music_ms(1000)==UINT32_MAX);
    const auto interactions=c.state().interaction_count;
    c.handle({EventKind::FocusButtonTapped,0}); assert(!c.state().clock.ringing);
    assert(c.state().interaction_count==interactions && c.state().clock.stopwatch_running);
    c.handle({EventKind::SetDisplayTimeout,1}); c.handle({EventKind::SleepDisplay,0});
    const auto asleep=c.handle({EventKind::Tick,2}); assert(c.state().display_asleep);
    assert(!pico::has_effect(asleep,pico::Effect::Render));
    c.handle({EventKind::Tick,62}); assert(c.state().display_asleep);
    c.handle({EventKind::FocusButtonTapped,0}); assert(!c.state().display_asleep);
    c.handle({EventKind::OpenClock,0}); c.handle({EventKind::SetClockOptions,5});
    c.handle({EventKind::UsbPower,1}); assert(!pico::display::auto_sleep_allowed(c.state()));
    c.handle({EventKind::UsbPower,0}); assert(pico::display::auto_sleep_allowed(c.state()));
    // Alarm and existing timer are independently pending at the same deadline.
    pico::Core both; both.handle(sync(epoch(2026,11,19,6,59,59)));
    both.handle(alarm(0,420)); both.handle({EventKind::StartTimer,1}); both.handle({EventKind::Tick,1});
    assert(both.state().timer_completed && both.state().clock.ringing);
    both.handle({EventKind::DismissAlarm,0}); assert(both.state().timer_completed);
    both.handle({EventKind::AnimationTick,1001}); assert(both.state().timer_completed);
}
void packets_and_frames()
{
    const std::uint8_t sync_packet[]{0x60,0x00,0x7f,0xff,0x6a,0xd4,0xfe};
    const auto decoded=pico::protocol::decode_command(sync_packet,sizeof(sync_packet));
    assert(decoded.status==pico::protocol::DecodeStatus::Ok);
    assert(decoded.event.auxiliary==540); // -300 minutes, biased by +840.
    const std::uint8_t alarm_packet[]{0x62,3,0xa4,1,31,1};
    assert(pico::protocol::decode_command(alarm_packet,6).status==pico::protocol::DecodeStatus::Ok);
    for (unsigned n=0;n<6;++n) assert(pico::protocol::decode_command(alarm_packet,n).status!=pico::protocol::DecodeStatus::Ok);
    const std::uint8_t invalid[]{0x62,4,0,0,0,1};
    assert(pico::protocol::decode_command(invalid,6).status==pico::protocol::DecodeStatus::InvalidValue);
    pico::Core c; c.handle(decoded.event); c.handle({EventKind::OpenClock,0});
    std::array<std::uint8_t,1024> first{},second{};
    pico::display::MonoSurface surface{first.data(),first.size(),128,64,pico::display::MonoLayout::HorizontalMsb};
    assert(pico::display::render(c.state(),surface));
    c.handle({EventKind::SetClockOptions,0}); surface.data=second.data();
    assert(pico::display::render(c.state(),surface)); assert(first!=second);
    for (auto screen:{pico::Screen::Clock,pico::Screen::Stopwatch,pico::Screen::Alarms,pico::Screen::ClockEdit,pico::Screen::AlarmEdit}) {
        pico::State s=c.state(); s.screen=screen;
        std::array<std::uint8_t,1040> guarded{}; guarded.fill(0xa5);
        surface.data=guarded.data()+8; assert(pico::display::render(s,surface));
        for (unsigned i=0;i<8;++i) assert(guarded[i]==0xa5 && guarded[1032+i]==0xa5);
    }
    std::uint8_t status[pico::protocol::kClockStatusSize]{}; pico::protocol::encode_clock(c.state(),status);
    assert(status[0]==1 && (status[1]&1));
}
}
int main()
{
    pico::Core precise;
    precise.handle({EventKind::MusicTick,900}); precise.handle({EventKind::StopwatchStart,0});
    precise.handle({EventKind::MusicTick,1100}); precise.handle({EventKind::Tick,1});
    precise.handle({EventKind::StopwatchPause,0}); assert(precise.state().clock.stopwatch_seconds==0);
    precise.handle({EventKind::MusicTick,2000}); precise.handle({EventKind::StopwatchResume,0});
    precise.handle({EventKind::MusicTick,2800}); assert(precise.state().clock.stopwatch_seconds==1);
    calendar(); time_and_stopwatch(); alarms(); persistence(); controls_power_concurrency(); packets_and_frames();
    std::cout << "Clock calendar, alarms, monotonic timing, power, controls, codec and protocol checks passed\n";
}
