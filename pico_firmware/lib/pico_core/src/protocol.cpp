#include <pico/protocol.hpp>

namespace pico::protocol {
namespace {

std::uint32_t read_u32_le(const std::uint8_t *data)
{
    return static_cast<std::uint32_t>(data[0]) |
           (static_cast<std::uint32_t>(data[1]) << 8U) |
           (static_cast<std::uint32_t>(data[2]) << 16U) |
           (static_cast<std::uint32_t>(data[3]) << 24U);
}

void write_u32_le(std::uint32_t value, std::uint8_t *output)
{
    output[0] = static_cast<std::uint8_t>(value);
    output[1] = static_cast<std::uint8_t>(value >> 8U);
    output[2] = static_cast<std::uint8_t>(value >> 16U);
    output[3] = static_cast<std::uint8_t>(value >> 24U);
}

DecodeResult wrong_length()
{
    return {DecodeStatus::WrongLength, {}};
}

} // namespace

DecodeResult decode_command(const std::uint8_t *data, std::size_t size)
{
    if (data == nullptr || size == 0U) {
        return {DecodeStatus::Empty, {}};
    }

    switch (static_cast<Command>(data[0])) {
    case Command::StartFinding: {
        if (size != 3U) return wrong_length();
        const unsigned seconds=static_cast<unsigned>(data[1]) | (static_cast<unsigned>(data[2])<<8U);
        if (!seconds || seconds>120U) return {DecodeStatus::InvalidValue,{}};
        return {DecodeStatus::Ok,{EventKind::StartFinding,seconds}};
    }
    case Command::StopFinding:
        return size==1U ? DecodeResult{DecodeStatus::Ok,{EventKind::StopFinding,0}} : wrong_length();
    case Command::Interact:
        return size == 1U
                   ? DecodeResult{DecodeStatus::Ok, {EventKind::PrimaryPressed, 0}}
                   : wrong_length();

    case Command::SetMode:
        if (size != 2U) {
            return wrong_length();
        }
        if (data[1] >= static_cast<std::uint8_t>(Mode::Count) ||
            data[1] == static_cast<std::uint8_t>(Mode::Driving)) {
            return {DecodeStatus::InvalidValue, {}};
        }
        return {DecodeStatus::Ok, {EventKind::SetMode, data[1]}};

    case Command::SetMood:
        if (size != 2U) {
            return wrong_length();
        }
        if (data[1] >= static_cast<std::uint8_t>(Mood::Count)) {
            return {DecodeStatus::InvalidValue, {}};
        }
        return {DecodeStatus::Ok, {EventKind::SetMood, data[1]}};

    case Command::StartTimer:
        if (size != 5U) {
            return wrong_length();
        }
        if (read_u32_le(&data[1]) == 0U ||
            read_u32_le(&data[1]) > kMaxTimerSeconds) {
            return {DecodeStatus::InvalidValue, {}};
        }
        return {DecodeStatus::Ok,
                {EventKind::StartTimer, read_u32_le(&data[1])}};

    case Command::CancelTimer:
        return size == 1U
                   ? DecodeResult{DecodeStatus::Ok, {EventKind::CancelTimer, 0}}
                   : wrong_length();

    case Command::SetTheme:
        if (size != 2U) return wrong_length();
        if (data[1] >= static_cast<unsigned>(FaceTheme::Count))
            return {DecodeStatus::InvalidValue, {}};
        return {DecodeStatus::Ok, {EventKind::SetTheme, data[1]}};
    case Command::SetBreakDuration:
    case Command::SetTimerDuration:
        if (size != 5U) return wrong_length();
        if (!read_u32_le(data + 1) || read_u32_le(data + 1) > kMaxTimerSeconds)
            return {DecodeStatus::InvalidValue, {}};
        return {DecodeStatus::Ok, {static_cast<Command>(data[0]) == Command::SetBreakDuration ? EventKind::SetBreakDuration : EventKind::SetTimerDuration, read_u32_le(data + 1)}};
    case Command::SetMotionConfig:
        if (size != 2U) return wrong_length();
        if (data[1] > motion::kAll) return {DecodeStatus::InvalidValue, {}};
        return {DecodeStatus::Ok, {EventKind::SetMotionConfig,data[1]}};
    case Command::StartTuner:
    case Command::StopTuner:
    case Command::StartMetronome:
    case Command::StopMetronome:
    case Command::TapTempo:
    case Command::StartFocusCycle:
    case Command::NextFocusPhase:
    case Command::PauseTimer:
    case Command::ResumeTimer:
    case Command::OpenSettings:
    case Command::OpenMenu:
    case Command::NavigateNext:
    case Command::NavigatePrevious:
    case Command::Select:
    case Command::Back: {
        if (size != 1U) return wrong_length();
        EventKind kind = EventKind::PauseTimer;
        switch (static_cast<Command>(data[0])) {
        case Command::StartTuner: kind = EventKind::StartTuner; break;
        case Command::StopTuner: kind = EventKind::StopTuner; break;
        case Command::StartMetronome: kind = EventKind::StartMetronome; break;
        case Command::StopMetronome: kind = EventKind::StopMetronome; break;
        case Command::TapTempo: kind = EventKind::TapTempo; break;
        case Command::StartFocusCycle: kind = EventKind::StartFocusCycle; break;
        case Command::NextFocusPhase: kind = EventKind::NextFocusPhase; break;
        case Command::PauseTimer: kind = EventKind::PauseTimer; break;
        case Command::ResumeTimer: kind = EventKind::ResumeTimer; break;
        case Command::OpenSettings: kind = EventKind::OpenSettings; break;
        case Command::OpenMenu: kind = EventKind::OpenMenu; break;
        case Command::NavigateNext: kind = EventKind::NavigateNext; break;
        case Command::NavigatePrevious: kind = EventKind::NavigatePrevious; break;
        case Command::Select: kind = EventKind::Select; break;
        case Command::Back: kind = EventKind::Back; break;
        default: break;
        }
        return {DecodeStatus::Ok, {kind, 0}};
    }

    case Command::SetMetronomeBpm: {
        if (size != 3U) return wrong_length();
        const unsigned bpm = static_cast<unsigned>(data[1]) | (static_cast<unsigned>(data[2]) << 8U);
        if (bpm < 30U || bpm > 240U) return {DecodeStatus::InvalidValue, {}};
        return {DecodeStatus::Ok, {EventKind::SetMetronomeBpm, bpm}};
    }
    case Command::SetMetronomeBeats:
        if (size != 2U) return wrong_length();
        if (data[1] < 1U || data[1] > 8U) return {DecodeStatus::InvalidValue, {}};
        return {DecodeStatus::Ok, {EventKind::SetMetronomeBeats, data[1]}};
    case Command::SetVolume:
        if (size != 2U) return wrong_length();
        if (data[1] > 100) return {DecodeStatus::InvalidValue, {}};
        return {DecodeStatus::Ok,{EventKind::SetVolume,data[1]}};
    case Command::TestSound:
    case Command::SetMuted:
        if (size != 2U) return wrong_length();
        if (data[1] >= (static_cast<Command>(data[0]) == Command::TestSound ? static_cast<unsigned>(audio::Cue::MetronomeClick) : 2U))
            return {DecodeStatus::InvalidValue, {}};
        return {DecodeStatus::Ok, {static_cast<Command>(data[0]) == Command::TestSound ? EventKind::TestSound : EventKind::SetMuted, data[1]}};
    case Command::RequestStatus:
        return size == 1U
                   ? DecodeResult{DecodeStatus::Ok, {EventKind::RequestStatus, 0}}
                   : wrong_length();
    }

    return {DecodeStatus::UnknownCommand, {}};
}

void encode_locator(const State &state, Capabilities capabilities,
                    std::uint8_t (&output)[kLocatorStatusSize])
{
    output[0]=1;
    output[1]=static_cast<std::uint8_t>((state.finding ? 1U : 0U) |
        ((capabilities & capability(Capability::Speaker)) && !state.sound_muted && state.sound_volume_percent ? 2U : 0U));
    output[2]=static_cast<std::uint8_t>(state.finding_remaining_seconds);
    output[3]=static_cast<std::uint8_t>(state.finding_remaining_seconds>>8U);
}

void encode_status(const State &state, Capabilities capabilities,
                   std::uint8_t (&output)[kStatusSize])
{
    output[0] = kProtocolVersion;
    output[1] = static_cast<std::uint8_t>(state.mode);
    output[2] = static_cast<std::uint8_t>(state.mood);
    output[3] = static_cast<std::uint8_t>(state.dock);
    output[4] = (state.connected ? 1U : 0U) |
                (state.timer_running ? 1U << 1U : 0U) |
                (state.timer_paused ? 1U << 2U : 0U) |
                (state.focus_waiting ? 1U << 3U : 0U) |
                (state.sound_muted ? 1U << 4U : 0U) |
                (state.metronome_running ? 1U << 5U : 0U) |
                (state.tuner_running ? 1U << 6U : 0U) |
                (state.motion_ready && state.motion_config ? 1U << 7U : 0U);
    output[5] = state.battery_percent;
    write_u32_le(state.timer_remaining_seconds, &output[6]);
    write_u32_le(state.interaction_count, &output[10]);
    output[14] = static_cast<std::uint8_t>(capabilities);
    output[15] = static_cast<std::uint8_t>(capabilities >> 8U);
}

} // namespace pico::protocol
