#pragma once

#include <cstddef>
#include <cstdint>
#include <pico/audio.hpp>
#include <pico/pitch.hpp>
#include <pico/motion.hpp>
#include <pico/focus_session.hpp>

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
    FocusButtonTapped,
    FocusButtonHeld,
    AnimationTick,
    OpenMenu,
    NavigateNext,
    NavigatePrevious,
    Select,
    Back,
    PauseTimer,
    ResumeTimer,
    SetTimerDuration,
    StartFocusCycle,
    NextFocusPhase,
    SetBreakDuration,
    TestSound,
    SetMuted,
    MusicTick,
    StartMetronome,
    StopMetronome,
    SetMetronomeBpm,
    SetMetronomeBeats,
    TapTempo,
    StartTuner,
    StopTuner,
    MicrophoneReady,
    TunerReading,
    MotionReady,
    MotionSettled,
    MotionActive,
    MotionPickup,
    MotionRock,
    MotionRest,
    SetMotionConfig,
    SetVolume,
    OpenSettings,
};

enum class Screen : std::uint8_t { Home, Menu, Utility, Themes, Unavailable, Music, Tuner, DurationEdit, Settings, SettingEdit };

enum class SettingKey : std::uint8_t { Theme, Mute, Volume, Motion };

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
    bool timer_paused{false};
    bool timer_completed{false};
    bool eyes_closed{false};
    std::int8_t gaze_x{0};
    Screen screen{Screen::Home};
    std::uint8_t menu_index{0};
    std::uint8_t utility_index{1};
    std::uint32_t configured_duration_seconds{1500};
    FocusPhase focus_phase{FocusPhase::None};
    bool focus_waiting{false};
    std::uint32_t completed_work_sessions{0};
    std::uint32_t break_duration_seconds{300};
    audio::Cue sound_cue{audio::Cue::Interact};
    bool sound_muted{false};
    bool metronome_running{false};
    bool beat_pulse{false};
    std::uint16_t metronome_bpm{120};
    std::uint8_t beats_per_bar{4};
    std::uint8_t current_beat{1};
    std::uint8_t music_index{2};
    bool microphone_ready{false};
    bool tuner_running{false};
    std::uint8_t tuner_session{0};
    pitch::Reading tuner{};
    bool motion_ready{false};
    bool motion_settled{false};
    std::uint8_t motion_config{motion::kAll};
    std::uint32_t draft_duration_seconds{1500};
    bool editing_break_duration{false};
    Screen duration_return_screen{Screen::Utility};
    std::uint8_t settings_index{0};
    SettingKey setting_key{SettingKey::Theme};
    std::uint32_t setting_draft{0};
    std::uint8_t setting_action{1};
    std::uint8_t sound_volume_percent{25};
    std::uint8_t duration_action{1}; // -1 minute, +1 minute, Save, Cancel
};

struct PersistedState {
    Mode mode{Mode::Companion};
    Mood mood{Mood::Neutral};
    std::uint32_t interaction_count{0};
    FaceTheme theme{FaceTheme::Modern};
    std::uint32_t configured_duration_seconds{1500};
    std::uint32_t break_duration_seconds{300};
    bool sound_muted{false};
    std::uint8_t sound_volume_percent{25};
    std::uint8_t motion_config{motion::kAll};
};

} // namespace pico
