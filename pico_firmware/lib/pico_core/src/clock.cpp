#include <pico/clock.hpp>
#include <algorithm>
#include <cstdio>
#include <limits>

namespace pico::clock {
namespace {
constexpr std::int32_t kFirstDay = static_cast<std::int32_t>(kMinUtc/86400U);
constexpr std::int32_t kLastDay = static_cast<std::int32_t>(kMaxUtc/86400U);
std::int64_t local_seconds(const State &s)
{ return static_cast<std::int64_t>(s.utc) + s.settings.offset_minutes * 60; }
std::uint32_t add_saturated(std::uint32_t value, std::uint32_t increment)
{ return increment > UINT32_MAX-value ? UINT32_MAX : value+increment; }
std::uint32_t read32(const std::uint8_t *p)
{ return std::uint32_t(p[0]) | (std::uint32_t(p[1])<<8) | (std::uint32_t(p[2])<<16) | (std::uint32_t(p[3])<<24); }
void write32(std::uint8_t *p,std::uint32_t value)
{ for (unsigned i=0;i<4;++i) p[i]=static_cast<std::uint8_t>(value>>(8*i)); }
std::uint32_t checksum(const std::uint8_t *data,std::size_t size)
{ std::uint32_t h=2166136261U; for (std::size_t i=0;i<size;++i) { h^=data[i]; h*=16777619U; } return h; }
}
bool leap(unsigned y) { return y%4==0 && (y%100!=0 || y%400==0); }
unsigned days_in_month(unsigned y,unsigned m)
{
    constexpr unsigned days[]{31,28,31,30,31,30,31,31,30,31,30,31};
    return m>=1 && m<=12 ? days[m-1]+(m==2 && leap(y) ? 1U : 0U) : 0U;
}
bool to_epoch(const Date &d,std::uint32_t &seconds)
{
    if (d.year<2000 || d.year>2099 || !d.month || d.month>12 || !d.day ||
        d.day>days_in_month(d.year,d.month) || d.hour>23 || d.minute>59 || d.second>59) return false;
    std::uint32_t days=kMinUtc/86400U;
    for (unsigned y=2000;y<d.year;++y) days+=leap(y) ? 366U : 365U;
    for (unsigned m=1;m<d.month;++m) days+=days_in_month(d.year,m);
    seconds=(days+d.day-1)*86400U + d.hour*3600U+d.minute*60U+d.second;
    return true;
}
Date from_epoch(std::int64_t seconds)
{
    // Formatting is also used for the small timezone margins around the UTC range.
    Date d;
    auto days=seconds/86400;
    auto remainder=seconds%86400;
    if (remainder<0) { remainder+=86400; --days; }
    d.weekday=static_cast<unsigned>((days+3)%7+7)%7;
    d.hour=static_cast<unsigned>(remainder/3600);
    d.minute=static_cast<unsigned>(remainder/60%60); d.second=static_cast<unsigned>(remainder%60);
    d.year=1970;
    while (days>=static_cast<std::int64_t>(leap(d.year) ? 366 : 365)) {
        days-=leap(d.year) ? 366 : 365; ++d.year;
    }
    d.month=1;
    while (days>=days_in_month(d.year,d.month)) { days-=days_in_month(d.year,d.month); ++d.month; }
    d.day=static_cast<unsigned>(days+1);
    return d;
}
bool valid_time(std::uint32_t utc,int offset)
{
    const auto local=static_cast<std::int64_t>(utc)+static_cast<std::int64_t>(offset)*60;
    return utc>=kMinUtc && utc<=kMaxUtc && offset>=-840 && offset<=840 && local>=kMinUtc && local<=kMaxUtc;
}
void format_time(char *out,std::size_t size,const Date &d,bool hour24)
{ std::snprintf(out,size,"%02u:%02u",hour24 ? d.hour : (d.hour%12 ? d.hour%12 : 12),d.minute); }
void format_date(char *out,std::size_t size,const Date &d)
{
    constexpr const char *days[]{"MON","TUE","WED","THU","FRI","SAT","SUN"};
    std::snprintf(out,size,"%s %02u/%02u",days[d.weekday%7],d.month,d.day);
}
bool valid_settings(const Settings &s)
{
    if (s.offset_minutes < -840 || s.offset_minutes > 840) return false;
    for (const auto &a:s.alarms)
        if (a.minute>=1440 || a.weekdays>127 || (a.once_day!=-1 &&
            (a.once_day<kFirstDay || a.once_day>kLastDay)) || (a.weekdays && a.once_day!=-1)) return false;
    return true;
}
bool Engine::resolve_once(State &s,unsigned i)
{
    auto &a=s.settings.alarms[i];
    if (!s.valid || !a.enabled || a.weekdays || a.once_day!=-1) return false;
    const auto local=local_seconds(s);
    auto day=static_cast<std::int32_t>(local/86400);
    if (day*std::int64_t(86400)+a.minute*60<=local) ++day;
    if (day>kLastDay) { a.enabled=false; a.once_day=-1; return true; }
    a.once_day=day;
    return true;
}
void Engine::schedule(State &s,unsigned i)
{
    due_[i]=0;
    const auto &a=s.settings.alarms[i];
    if (!s.valid || !a.enabled) return;
    const auto day=local_seconds(s)/86400;
    if (!a.weekdays) {
        if (a.once_day!=-1) due_[i]=a.once_day*std::int64_t(86400)+a.minute*60-s.settings.offset_minutes*60;
        return;
    }
    for (unsigned ahead=0;ahead<=7;++ahead) {
        const auto candidate_day=day+ahead;
        const auto candidate=candidate_day*86400+a.minute*60-s.settings.offset_minutes*60;
        if (candidate>s.utc && candidate_day!=last_fired_[i] &&
            (a.weekdays & (1U<<((candidate_day+3)%7)))) { due_[i]=candidate; return; }
    }
}
bool Engine::synchronize(State &s,std::uint32_t utc,int offset)
{
    if (!valid_time(utc,offset)) return false;
    s.utc=utc; s.valid=true; s.sync_age=0; s.sync_due=false;
    s.settings.offset_minutes=static_cast<std::int16_t>(offset);
    for (unsigned i=0;i<kAlarmCount;++i) {
        resolve_once(s,i);
        auto &a=s.settings.alarms[i];
        const auto candidate=a.once_day*std::int64_t(86400)+a.minute*60-offset*60;
        if (a.enabled && !a.weekdays && a.once_day!=-1 && candidate<=utc) a.enabled=false;
        schedule(s,i);
    }
    return true;
}
bool Engine::configure(State &s,unsigned i,Alarm a)
{
    if (i>=kAlarmCount || a.minute>=1440 || a.weekdays>127) return false;
    a.once_day=-1;
    s.settings.alarms[i]=a;
    last_fired_[i]=-1;
    s.ringing=static_cast<std::uint8_t>(s.ringing & ~(1U<<i));
    resolve_once(s,i); schedule(s,i);
    return true;
}
bool Engine::restore(State &s,const Settings &settings)
{
    if (!valid_settings(settings)) return false;
    s.settings=settings;
    for (unsigned i=0;i<kAlarmCount;++i) schedule(s,i);
    return true;
}
void Engine::dismiss(State &s) { s.ringing=0; }
void Engine::stopwatch(State &s,StopwatchAction action)
{
    switch (action) {
    case StopwatchAction::Start: s.stopwatch_seconds=0; s.stopwatch_fraction_ms=0; s.stopwatch_running=true; break;
    case StopwatchAction::Pause: s.stopwatch_running=false; break;
    case StopwatchAction::Resume: s.stopwatch_running=true; break;
    case StopwatchAction::Reset: s.stopwatch_running=false; s.stopwatch_seconds=0; s.stopwatch_fraction_ms=0; break;
    }
}
void Engine::reboot(State &s)
{
    const auto settings=s.settings;
    s=State{}; s.settings=settings;
    *this=Engine{};
}
bool Engine::advance_stopwatch(State &s,std::uint32_t now_ms)
{
    const auto delta=now_ms-s.monotonic_ms;
    // A whole-second Tick may follow a newer millisecond MusicTick. Ignore
    // that <1 s backward rounding; genuine uint32 ms rollover remains valid.
    if (delta>UINT32_MAX-1000U) return false;
    s.monotonic_ms=now_ms;
    if (!s.stopwatch_running) return false;
    const auto total=static_cast<std::uint64_t>(s.stopwatch_fraction_ms)+delta;
    const auto before=s.stopwatch_seconds;
    s.stopwatch_seconds=add_saturated(s.stopwatch_seconds,static_cast<std::uint32_t>(total/1000U));
    s.stopwatch_fraction_ms=static_cast<std::uint16_t>(total%1000U);
    return before!=s.stopwatch_seconds;
}
Update Engine::advance(State &s,std::uint32_t now)
{
    Update result;
    const auto delta=now-mono_;
    mono_=now;
    advance_stopwatch(s,now*1000U);
    if (s.ringing && now-ring_started_>=60U) { s.ringing=0; result.alarm_changed=true; }
    if (!s.valid) return result;
    const auto previous_minute=local_seconds(s)/60;
    const auto utc=static_cast<std::uint64_t>(s.utc)+delta;
    if (utc>kMaxUtc || !valid_time(static_cast<std::uint32_t>(utc),s.settings.offset_minutes)) {
        s.valid=false; s.sync_due=true; result.sync_changed=true; result.minute_changed=true; return result;
    }
    s.utc=static_cast<std::uint32_t>(utc); s.sync_age=add_saturated(s.sync_age,delta);
    if (!s.sync_due && s.sync_age>=86400U) { s.sync_due=true; result.sync_changed=true; }
    result.minute_changed=previous_minute!=local_seconds(s)/60;
    for (unsigned i=0;i<kAlarmCount;++i) {
        if (!due_[i] || due_[i]>s.utc) continue;
        auto &a=s.settings.alarms[i];
        last_fired_[i]=static_cast<std::int32_t>((due_[i]+s.settings.offset_minutes*60)/86400);
        // Bounded late wake grace; sync corrections never trigger retrospective alarms.
        if (static_cast<std::int64_t>(s.utc)-due_[i]<=120) {
            if (!s.ringing) ring_started_=now;
            s.ringing=static_cast<std::uint8_t>(s.ringing | (1U<<i));
            result.alarm_changed=true;
        }
        if (!a.weekdays) { a.enabled=false; result.settings_changed=true; }
        schedule(s,i);
    }
    return result;
}
Blob encode(const Settings &s)
{
    Blob b{}; b[0]='P';b[1]='C';b[2]='L';b[3]='K';b[4]=1;
    b[5]=static_cast<std::uint8_t>((s.hour24 ? 1 : 0)|(s.glance ? 2 : 0)|(s.usb_desk ? 4 : 0));
    b[6]=static_cast<std::uint8_t>(s.offset_minutes); b[7]=static_cast<std::uint8_t>(static_cast<std::uint16_t>(s.offset_minutes)>>8);
    for (unsigned i=0;i<kAlarmCount;++i) {
        const auto &a=s.alarms[i]; auto *p=&b[8+i*8];
        p[0]=static_cast<std::uint8_t>(a.minute);p[1]=static_cast<std::uint8_t>(a.minute>>8);
        p[2]=a.weekdays;p[3]=a.enabled ? 1 : 0;write32(p+4,static_cast<std::uint32_t>(a.once_day));
    }
    write32(&b[40],checksum(b.data(),40)); return b;
}
bool decode(const std::uint8_t *b,std::size_t size,Settings &s)
{
    if (!b || size!=44 || b[0]!='P' || b[1]!='C' || b[2]!='L' || b[3]!='K' || b[4]!=1 ||
        b[5]>7 || read32(b+40)!=checksum(b,40)) return false;
    Settings candidate;
    candidate.hour24=(b[5]&1)!=0; candidate.glance=(b[5]&2)!=0; candidate.usb_desk=(b[5]&4)!=0;
    const int raw=b[6]|(unsigned(b[7])<<8);
    candidate.offset_minutes=static_cast<std::int16_t>(raw>=32768 ? raw-65536 : raw);
    for (unsigned i=0;i<kAlarmCount;++i) {
        const auto *p=b+8+i*8; auto &a=candidate.alarms[i];
        if (p[3]>1) return false;
        a.minute=static_cast<std::uint16_t>(p[0]|(unsigned(p[1])<<8));
        a.weekdays=p[2];a.enabled=p[3]!=0;
        const auto day=read32(p+4);
        if (day!=UINT32_MAX && day>static_cast<std::uint32_t>(kLastDay)) return false;
        a.once_day=day==UINT32_MAX ? -1 : static_cast<std::int32_t>(day);
    }
    if (!valid_settings(candidate)) return false;
    s=candidate; return true;
}
} // namespace pico::clock
