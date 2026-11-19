#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace pico::clock {
inline constexpr std::uint32_t kMinUtc = 946684800U; // 2000-01-01
inline constexpr std::uint32_t kMaxUtc = 4102444799U; // 2099-12-31
inline constexpr unsigned kAlarmCount = 4;
inline constexpr std::int32_t kUnresolvedDay = -1;
struct Date {
    unsigned year{2000}, month{1}, day{1}, hour{0}, minute{0}, second{0}, weekday{5}; // Monday=0
};
bool leap(unsigned year);
unsigned days_in_month(unsigned year, unsigned month);
bool to_epoch(const Date &date, std::uint32_t &seconds);
Date from_epoch(std::int64_t seconds);
bool valid_time(std::uint32_t utc, int offset_minutes);
void format_time(char *out, std::size_t size, const Date &date, bool hour24);
void format_date(char *out, std::size_t size, const Date &date);

struct Alarm {
    std::uint16_t minute{420}; // Local minutes after midnight.
    std::uint8_t weekdays{0}; // 0=one shot, bit0=Monday ... bit6=Sunday.
    bool enabled{false};
    std::int32_t once_day{kUnresolvedDay}; // Local Unix day, resolved only with valid time.
};
struct Settings {
    std::array<Alarm,kAlarmCount> alarms{};
    std::int16_t offset_minutes{0}; // Local = UTC + offset; phone updates DST.
    bool hour24{true}, glance{false}, usb_desk{false};
};
bool valid_settings(const Settings &settings);
struct State {
    Settings settings{};
    std::uint32_t utc{0}, sync_age{0}, stopwatch_seconds{0}, monotonic_ms{0};
    std::uint16_t stopwatch_fraction_ms{0};
    bool valid{false}, sync_due{true}, stopwatch_running{false}, usb_powered{false};
    std::uint8_t ringing{0}; // Alarm bitmask; simultaneous alarms share one alert.
};
struct Update {
    bool settings_changed{false}, alarm_changed{false}, minute_changed{false}, sync_changed{false};
};
enum class StopwatchAction : std::uint8_t { Start, Pause, Resume, Reset };
class Engine {
public:
    Update advance(State &state, std::uint32_t monotonic_seconds);
    bool advance_stopwatch(State &state,std::uint32_t monotonic_ms);
    bool synchronize(State &state, std::uint32_t utc, int offset_minutes);
    bool configure(State &state, unsigned index, Alarm alarm);
    bool restore(State &state, const Settings &settings);
    void dismiss(State &state);
    void stopwatch(State &state, StopwatchAction action);
    // Clears volatile time/stopwatch/ringing, retains settings. No wall time survives reboot.
    void reboot(State &state);
private:
    void schedule(State &state, unsigned index);
    bool resolve_once(State &state, unsigned index);
    std::array<std::int64_t,kAlarmCount> due_{};
    std::array<std::int32_t,kAlarmCount> last_fired_{{-1,-1,-1,-1}};
    std::uint32_t mono_{0}, ring_started_{0};
};

// Separate versioned record: old pico/state v5 remains byte-for-byte compatible.
using Blob = std::array<std::uint8_t,44>;
Blob encode(const Settings &settings);
bool decode(const std::uint8_t *bytes, std::size_t size, Settings &settings);
} // namespace pico::clock
