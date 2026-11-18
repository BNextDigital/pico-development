#pragma once

#include <cstddef>
#include <cstdint>

namespace pico {

inline constexpr std::uint8_t kProtocolVersion = 1;
inline constexpr std::uint8_t kBatteryUnknown = 0xFF;
inline constexpr std::uint32_t kMaxTimerSeconds = 7U * 24U * 60U * 60U;

enum class Mode : std::uint8_t {
    Companion = 0,
    Focus,
    Music,
    Cooking,
    Wayfinding,
    Notes,
    Driving,
    Count,
};

enum class Mood : std::uint8_t {
    Neutral = 0,
    Happy,
    Curious,
    Sleepy,
    Alert,
    Count,
};

enum class Dock : std::uint8_t {
    None = 0,
    Home,
    Car,
    Count,
};

// Stored in persistence byte 7. Keep these IDs stable when adding themes.
enum class FaceTheme : std::uint8_t {
    Modern = 0,
    Chibi,
    Anime,
    RetroIbm,
    Mecha,
    VintageCartoon,
    MinimalEmoticon,
    KawaiiMascot,
    Count,
};

enum class EventKind : std::uint8_t {
    Boot = 0,
    Tick,
    PrimaryPressed,
    SecondaryPressed,
    Connected,
    Disconnected,
    DockChanged,
    SetMode,
    SetMood,
    SetBattery,
    StartTimer,
    CancelTimer,
    RequestStatus,
    SetTheme,
    CycleTheme,
};

struct Event {
    EventKind kind{EventKind::Boot};
    std::uint32_t value{0};
};

static_assert(sizeof(Event) <= 8, "Events must stay cheap enough for the firmware queue");

enum class Effect : std::uint16_t {
    None = 0,
    Render = 1U << 0,
    Notify = 1U << 1,
    Persist = 1U << 2,
    Chirp = 1U << 3,
};

using Effects = std::uint16_t;

constexpr Effects effect(Effect value)
{
    return static_cast<Effects>(value);
}

constexpr Effects operator|(Effect lhs, Effect rhs)
{
    return effect(lhs) | effect(rhs);
}

constexpr Effects operator|(Effects lhs, Effect rhs)
{
    return lhs | effect(rhs);
}

constexpr bool has_effect(Effects effects, Effect wanted)
{
    return (effects & effect(wanted)) != 0U;
}

struct State {
    Mode mode{Mode::Companion};
    Mood mood{Mood::Neutral};
    Dock dock{Dock::None};
    bool connected{false};
    bool timer_running{false};
    std::uint8_t battery_percent{kBatteryUnknown};
    std::uint32_t uptime_seconds{0};
    std::uint32_t timer_total_seconds{0};
    std::uint32_t timer_remaining_seconds{0};
    std::uint32_t interaction_count{0};
    FaceTheme theme{FaceTheme::Modern};
};

struct PersistedState {
    Mode mode{Mode::Companion};
    Mood mood{Mood::Neutral};
    std::uint32_t interaction_count{0};
    FaceTheme theme{FaceTheme::Modern};
};

} // namespace pico
