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
    case Command::SyncTime: {
        if (size!=7U) return wrong_length();
        const auto raw=static_cast<unsigned>(data[5])|(static_cast<unsigned>(data[6])<<8);
        const int offset=raw>=32768U ? static_cast<int>(raw)-65536 : static_cast<int>(raw);
        const auto utc=read_u32_le(data+1);
        if (!clock::valid_time(utc,offset)) return {DecodeStatus::InvalidValue,{}};
        return {DecodeStatus::Ok,{EventKind::SyncTime,utc,static_cast<std::uint32_t>(offset+840)}};
    }
    case Command::SetClockOptions:
        if (size!=2U) return wrong_length();
        if (data[1]>7) return {DecodeStatus::InvalidValue,{}};
        return {DecodeStatus::Ok,{EventKind::SetClockOptions,data[1]}};
    case Command::ConfigureAlarm: {
        if (size!=6U) return wrong_length();
        const unsigned minute=data[2]|(unsigned(data[3])<<8);
        if (data[1]>=clock::kAlarmCount || minute>=1440 || data[4]>127 || data[5]>1)
            return {DecodeStatus::InvalidValue,{}};
        return {DecodeStatus::Ok,{EventKind::ConfigureAlarm,data[1],
            minute|(std::uint32_t(data[4])<<16)|(std::uint32_t(data[5])<<24)}};
    }
    case Command::ParkingReady:
        if (size != 2U) return {DecodeStatus::WrongLength, {}};
        if (data[1] > 1U) return {DecodeStatus::InvalidValue, {}};
        return {DecodeStatus::Ok, {EventKind::ParkingReady, data[1]}};
    case Command::ParkingResult: {
        if (size != kParkingStatusSize) return {DecodeStatus::WrongLength, {}};
        const auto boot = read_u32_le(data + 2);
        const auto request = std::uint32_t(data[6]) | (std::uint32_t(data[7]) << 8U) |
            (std::uint32_t(data[8]) << 16U);
        if (!boot || !request || !parking::app_result(static_cast<parking::Phase>(data[1])))
            return {DecodeStatus::InvalidValue, {}};
        return {DecodeStatus::Ok, {EventKind::ParkingResult, boot, request | (std::uint32_t(data[1]) << 24U)}};
    }
    case Command::DismissAlarm: case Command::StopwatchStart: case Command::StopwatchPause:
    case Command::StopwatchResume: case Command::StopwatchReset: case Command::OpenClock: {
        if (size!=1U) return wrong_length();
        const auto command=static_cast<Command>(data[0]);
        const auto kind=command==Command::DismissAlarm ? EventKind::DismissAlarm :
            command==Command::StopwatchStart ? EventKind::StopwatchStart :
            command==Command::StopwatchPause ? EventKind::StopwatchPause :
            command==Command::StopwatchResume ? EventKind::StopwatchResume :
            command==Command::StopwatchReset ? EventKind::StopwatchReset : EventKind::OpenClock;
        return {DecodeStatus::Ok,{kind,0}};
    }
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

void encode_clock(const State &state,std::uint8_t (&output)[kClockStatusSize])
{
    const auto &s=state.clock;
    output[0]=1;
    output[1]=static_cast<std::uint8_t>((s.valid ? 1 : 0)|(s.sync_due ? 2 : 0)|
        (s.settings.hour24 ? 4 : 0)|(s.settings.glance ? 8 : 0)|(s.settings.usb_desk ? 16 : 0)|
        (s.usb_powered ? 32 : 0)|(s.stopwatch_running ? 64 : 0)|(s.ringing ? 128 : 0));
    write_u32_le(s.valid ? s.utc : 0,output+2);
    output[6]=static_cast<std::uint8_t>(s.settings.offset_minutes);
    output[7]=static_cast<std::uint8_t>(static_cast<std::uint16_t>(s.settings.offset_minutes)>>8);
    write_u32_le(s.stopwatch_seconds,output+8);
    output[12]=s.ringing; output[13]=0;
    for (unsigned i=0;i<clock::kAlarmCount;++i) if (s.settings.alarms[i].enabled) output[13]|=static_cast<std::uint8_t>(1U<<i);
    write_u32_le(s.sync_age,output+14);
    output[18]=static_cast<std::uint8_t>(s.stopwatch_fraction_ms);
    output[19]=static_cast<std::uint8_t>(s.stopwatch_fraction_ms>>8);
}

void encode_parking(const State &state, std::uint8_t (&output)[kParkingStatusSize])
{
    const auto &request = state.parking;
    output[0] = 1;
    output[1] = static_cast<std::uint8_t>(request.phase);
    write_u32_le(request.boot_id, output + 2);
    for (unsigned i = 0; i < 3; ++i)
        output[6 + i] = static_cast<std::uint8_t>(request.sequence >> (8U * i));
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
