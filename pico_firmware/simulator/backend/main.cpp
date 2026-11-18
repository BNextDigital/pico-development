#include <cmath>
#include <pico/core.hpp>
#include <pico/display_model.hpp>
#include <pico/features.hpp>
#include <pico/persistence.hpp>
#include <pico/protocol.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr std::uint32_t kSaveDelaySeconds = 5;
constexpr pico::Capabilities kSimulatedCapabilities =
    pico::capability(pico::Capability::Display) |
    pico::capability(pico::Capability::Speaker) |
    pico::capability(pico::Capability::HomeDock) |
    pico::capability(pico::Capability::CarDock);

struct Result {
    bool ok{true};
    pico::Effects effects{pico::effect(pico::Effect::None)};
    bool rendered{false};
    bool notified{false};
    bool chirped{false};
    bool persist_queued{false};
    bool storage_saved{false};
    std::string event{"Status"};
    std::string message;
};

std::string json_escape(std::string_view input)
{
    std::ostringstream output;
    for (const unsigned char character : input) {
        switch (character) {
        case '\\':
            output << "\\\\";
            break;
        case '"':
            output << "\\\"";
            break;
        case '\n':
            output << "\\n";
            break;
        case '\r':
            output << "\\r";
            break;
        case '\t':
            output << "\\t";
            break;
        default:
            if (character < 0x20U) {
                output << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                       << static_cast<unsigned int>(character) << std::dec;
            } else {
                output << character;
            }
        }
    }
    return output.str();
}

std::string quoted(std::string_view value)
{
    return '"' + json_escape(value) + '"';
}

template <typename Container>
std::string hex_string(const Container &bytes)
{
    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (const auto byte : bytes) {
        output << std::setw(2) << static_cast<unsigned int>(byte);
    }
    return output.str();
}

std::string mood_name(pico::Mood mood)
{
    switch (mood) {
    case pico::Mood::Neutral:
        return "neutral";
    case pico::Mood::Happy:
        return "happy";
    case pico::Mood::Curious:
        return "curious";
    case pico::Mood::Sleepy:
        return "sleepy";
    case pico::Mood::Alert:
        return "alert";
    case pico::Mood::Count:
        break;
    }
    return "unknown";
}

std::string mode_name(pico::Mode mode)
{
    const auto *feature = pico::feature_for(mode);
    return feature == nullptr ? "unknown" : feature->name;
}

std::string dock_name(pico::Dock dock)
{
    switch (dock) {
    case pico::Dock::None:
        return "none";
    case pico::Dock::Home:
        return "home";
    case pico::Dock::Car:
        return "car";
    case pico::Dock::Count:
        break;
    }
    return "unknown";
}

std::string event_name(pico::EventKind kind)
{
    switch (kind) {
    case pico::EventKind::StartFocusCycle: return "StartFocusCycle";
    case pico::EventKind::NextFocusPhase: return "NextFocusPhase";
    case pico::EventKind::SetBreakDuration: return "SetBreakDuration";
    case pico::EventKind::TestSound: return "TestSound";
    case pico::EventKind::SetMuted: return "SetMuted";
    case pico::EventKind::MusicTick: return "MusicTick";
    case pico::EventKind::StartMetronome: return "StartMetronome";
    case pico::EventKind::StopMetronome: return "StopMetronome";
    case pico::EventKind::SetMetronomeBpm: return "SetMetronomeBpm";
    case pico::EventKind::SetMetronomeBeats: return "SetMetronomeBeats";
    case pico::EventKind::TapTempo: return "TapTempo";
    case pico::EventKind::StartTuner: return "StartTuner";
    case pico::EventKind::StopTuner: return "StopTuner";
    case pico::EventKind::MicrophoneReady: return "MicrophoneReady";
    case pico::EventKind::TunerReading: return "TunerReading";
    case pico::EventKind::MotionReady: return "MotionReady";
    case pico::EventKind::MotionSettled: return "MotionSettled";
    case pico::EventKind::MotionActive: return "MotionActive";
    case pico::EventKind::MotionPickup: return "MotionPickup";
    case pico::EventKind::MotionRock: return "MotionRock";
    case pico::EventKind::MotionRest: return "MotionRest";
    case pico::EventKind::SetMotionConfig: return "SetMotionConfig";
    case pico::EventKind::Boot:
        return "Boot";
    case pico::EventKind::Tick:
        return "Tick";
    case pico::EventKind::PrimaryPressed:
        return "PrimaryPressed";
    case pico::EventKind::SecondaryPressed:
        return "SecondaryPressed";
    case pico::EventKind::Connected:
        return "Connected";
    case pico::EventKind::Disconnected:
        return "Disconnected";
    case pico::EventKind::DockChanged:
        return "DockChanged";
    case pico::EventKind::SetMode:
        return "SetMode";
    case pico::EventKind::SetMood:
        return "SetMood";
    case pico::EventKind::SetBattery:
        return "SetBattery";
    case pico::EventKind::StartTimer:
        return "StartTimer";
    case pico::EventKind::CancelTimer:
        return "CancelTimer";
    case pico::EventKind::RequestStatus:
        return "RequestStatus";
    case pico::EventKind::SetTheme:
        return "SetTheme";
    case pico::EventKind::CycleTheme:
        return "CycleTheme";
    case pico::EventKind::FocusButtonTapped:
        return "FocusButtonTapped";
    case pico::EventKind::FocusButtonHeld:
        return "FocusButtonHeld";
    case pico::EventKind::AnimationTick:
        return "AnimationTick";
    case pico::EventKind::OpenMenu:
        return "OpenMenu";
    case pico::EventKind::NavigateNext:
        return "NavigateNext";
    case pico::EventKind::NavigatePrevious:
        return "NavigatePrevious";
    case pico::EventKind::Select:
        return "Select";
    case pico::EventKind::Back:
        return "Back";
    case pico::EventKind::PauseTimer:
        return "PauseTimer";
    case pico::EventKind::ResumeTimer:
        return "ResumeTimer";
    case pico::EventKind::SetTimerDuration:
        return "SetTimerDuration";

    }
    return "Event" + std::to_string(static_cast<unsigned int>(kind));
}

std::optional<pico::EventKind> parse_event_kind(const std::string &token)
{
    constexpr std::array<pico::EventKind, 48> kinds{
        pico::EventKind::Boot,          pico::EventKind::Tick,
        pico::EventKind::PrimaryPressed, pico::EventKind::SecondaryPressed,
        pico::EventKind::Connected,     pico::EventKind::Disconnected,
        pico::EventKind::DockChanged,   pico::EventKind::SetMode,
        pico::EventKind::SetMood,       pico::EventKind::SetBattery,
        pico::EventKind::StartTimer,    pico::EventKind::CancelTimer,
        pico::EventKind::RequestStatus,
        pico::EventKind::SetTheme,      pico::EventKind::CycleTheme,
        pico::EventKind::FocusButtonTapped, pico::EventKind::FocusButtonHeld,
        pico::EventKind::AnimationTick,
        pico::EventKind::OpenMenu,
        pico::EventKind::NavigateNext,
        pico::EventKind::NavigatePrevious,
        pico::EventKind::Select,
        pico::EventKind::Back,
        pico::EventKind::PauseTimer,
        pico::EventKind::ResumeTimer,
        pico::EventKind::SetTimerDuration,
        pico::EventKind::StartFocusCycle,
        pico::EventKind::NextFocusPhase,
        pico::EventKind::SetBreakDuration,
        pico::EventKind::TestSound,
        pico::EventKind::SetMuted,
        pico::EventKind::MusicTick,
        pico::EventKind::StartMetronome,
        pico::EventKind::StopMetronome,
        pico::EventKind::SetMetronomeBpm,
        pico::EventKind::SetMetronomeBeats,
        pico::EventKind::TapTempo,
        pico::EventKind::StartTuner,
        pico::EventKind::StopTuner,
        pico::EventKind::MicrophoneReady,
        pico::EventKind::TunerReading,
        pico::EventKind::MotionReady,
        pico::EventKind::MotionSettled,
        pico::EventKind::MotionActive,
        pico::EventKind::MotionPickup,
        pico::EventKind::MotionRock,
        pico::EventKind::MotionRest,
        pico::EventKind::SetMotionConfig,





    };
    for (const auto kind : kinds) {
        if (token == event_name(kind)) {
            return kind;
        }
    }

    try {
        std::size_t consumed = 0;
        const auto value = std::stoul(token, &consumed, 0);
        if (consumed == token.size() && value <= 0xFFU) {
            return static_cast<pico::EventKind>(value);
        }
    } catch (...) {
    }
    return std::nullopt;
}

std::optional<std::uint32_t> parse_u32(const std::string &token)
{
    try {
        std::size_t consumed = 0;
        const auto value = std::stoull(token, &consumed, 0);
        if (consumed == token.size() &&
            value <= std::numeric_limits<std::uint32_t>::max()) {
            return static_cast<std::uint32_t>(value);
        }
    } catch (...) {
    }
    return std::nullopt;
}

std::optional<std::vector<std::uint8_t>> parse_hex(std::string text)
{
    text.erase(std::remove_if(text.begin(), text.end(), [](unsigned char value) {
                   return std::isspace(value) != 0 || value == ':' || value == '-' ||
                          value == ',';
               }),
               text.end());
    if (text.empty() || text.size() % 2U != 0U) {
        return std::nullopt;
    }

    std::vector<std::uint8_t> bytes;
    bytes.reserve(text.size() / 2U);
    for (std::size_t index = 0; index < text.size(); index += 2U) {
        const auto pair = text.substr(index, 2);
        try {
            std::size_t consumed = 0;
            const auto value = std::stoul(pair, &consumed, 16);
            if (consumed != pair.size() || value > 0xFFU) {
                return std::nullopt;
            }
            bytes.push_back(static_cast<std::uint8_t>(value));
        } catch (...) {
            return std::nullopt;
        }
    }
    return bytes;
}

std::string decode_status_name(pico::protocol::DecodeStatus status)
{
    switch (status) {
    case pico::protocol::DecodeStatus::Ok:
        return "ok";
    case pico::protocol::DecodeStatus::Empty:
        return "empty";
    case pico::protocol::DecodeStatus::UnknownCommand:
        return "unknown-command";
    case pico::protocol::DecodeStatus::WrongLength:
        return "wrong-length";
    case pico::protocol::DecodeStatus::InvalidValue:
        return "invalid-value";
    }
    return "decode-error";
}

class Simulator {
public:
    explicit Simulator(std::filesystem::path state_path, std::uint32_t seed)
        : core_(seed), seed_(seed), state_path_(std::move(state_path))
    {
        load_state();
        core_.handle({pico::EventKind::MotionReady,1});
        core_.handle({pico::EventKind::MicrophoneReady,1});
        last_result_ = dispatch({pico::EventKind::Boot, 0});
        last_result_.message = restored_ ? "Booted with saved state"
                                         : "Booted with default state";
    }

    const Result &last_result() const { return last_result_; }

    Result dispatch(const pico::Event &event)
    {
        Result result;
        result.event = event_name(event.kind);
        const auto old_motion_config = core_.state().motion_config;
        result.effects = core_.handle(event);
        if (old_motion_config != core_.state().motion_config || !core_.state().motion_ready)
            motion_detector_.reset();
        apply_effects(result);
        result.storage_saved = flush_if_due();
        last_result_ = result;
        return result;
    }

    Result advance_ms(std::uint64_t milliseconds)
    {
        now_ms_ += milliseconds;
        const auto seconds = static_cast<std::uint32_t>(now_ms_ / 1000U);
        Result result;
        result.event = "Advance";
        result.effects |= core_.handle({pico::EventKind::MusicTick, static_cast<std::uint32_t>(now_ms_)});
        if (seconds != now_seconds_) {
            now_seconds_ = seconds;
            result.effects |= core_.handle({pico::EventKind::Tick, now_seconds_});
        }
        result.effects |= core_.handle({pico::EventKind::AnimationTick,
                                       static_cast<std::uint32_t>(now_ms_)});
        apply_effects(result);
        result.storage_saved = flush_if_due();
        last_result_ = result;
        return result;
    }
    Result advance(std::uint32_t seconds)
    { return advance_ms(static_cast<std::uint64_t>(seconds) * 1000U); }

    Result motion_sample(pico::motion::Sample sample,std::uint32_t elapsed)
    {
        auto advanced = advance_ms(elapsed);
        Result result;
        result.event = "MotionSample";
        if (!core_.state().motion_ready || !core_.state().motion_config) motion_detector_.reset();
        else {
            const auto signals = motion_detector_.sample(sample,static_cast<std::uint32_t>(now_ms_));
            const struct { pico::motion::Signal signal; pico::EventKind event; } mapping[]{
                {pico::motion::Settled,pico::EventKind::MotionSettled},{pico::motion::Active,pico::EventKind::MotionActive},
                {pico::motion::Pickup,pico::EventKind::MotionPickup},{pico::motion::Rock,pico::EventKind::MotionRock},
                {pico::motion::Rest,pico::EventKind::MotionRest}};
            for (const auto &entry:mapping) if (signals & entry.signal)
                result.effects |= core_.handle({entry.event,static_cast<std::uint32_t>(now_ms_)});
        }
        apply_effects(result);
        result.effects |= advanced.effects;
        result.rendered |= advanced.rendered;
        result.notified |= advanced.notified;
        result.chirped |= advanced.chirped;
        result.persist_queued |= advanced.persist_queued;
        result.storage_saved = advanced.storage_saved;
        last_result_ = result;
        return result;
    }
    Result motion_test(const std::string &name)
    {
        motion_detector_.reset();
        Result result;
        const auto hold = [&](pico::motion::Sample sample,unsigned ms) {
            for (unsigned i=0; i<ms; i+=40) {
                const auto step = motion_sample(sample,40);
                result.effects |= step.effects;
                result.rendered |= step.rendered;
                result.notified |= step.notified;
                result.chirped |= step.chirped;
                result.persist_queued |= step.persist_queued;
                result.storage_saved |= step.storage_saved;
            }
        };
        hold({0,0,1000},2200);
        if (name == "pickup") hold({450,0,893},320);
        else if (name == "rock") {
            hold({500,0,866},240); hold({-500,0,866},240); hold({0,0,1000},240);
        } else if (name == "rest") hold({0,0,1000},61000);
        else if (name == "bump") { hold({2500,0,1000},40); hold({0,0,1000},240); }
        result.event = "MotionTest:"+name;
        last_result_ = result;
        return result;
    }
    Result tone(unsigned millihz)
    {
        pico::pitch::Detector detector;
        std::array<std::int16_t,pico::pitch::kFrameSamples> samples{};
        for (std::size_t i=0; i<samples.size(); ++i)
            samples[i] = static_cast<std::int16_t>(6000.0 * std::sin(6.283185307179586 *
                static_cast<double>(millihz) * static_cast<double>(i) / (1000.0 * pico::pitch::kSampleRate)));
        detector.analyze(samples.data(),samples.size());
        const auto reading = detector.analyze(samples.data(),samples.size());
        return dispatch({pico::EventKind::TunerReading,pico::pitch::pack(reading,core_.state().tuner_session)});
    }
    Result ble_command(const std::vector<std::uint8_t> &bytes)
    {
        const auto decoded = pico::protocol::decode_command(bytes.data(), bytes.size());
        if (decoded.status != pico::protocol::DecodeStatus::Ok) {
            Result result;
            result.ok = false;
            result.event = "BleCommand";
            result.message = "BLE packet rejected: " + decode_status_name(decoded.status);
            last_result_ = result;
            return result;
        }

        auto result = dispatch(decoded.event);
        result.event = "BLE:" + event_name(decoded.event.kind);
        result.message = "Accepted BLE packet " + hex_string(bytes);
        last_result_ = result;
        return result;
    }

    Result power_cycle(bool factory_reset)
    {
        pending_dirty_ = false;
        save_due_seconds_ = 0;
        now_seconds_ = 0;
        now_ms_ = 0;
        core_ = pico::Core{seed_};
        motion_detector_.reset();
        restored_ = false;
        if (factory_reset) {
            std::error_code error;
            std::filesystem::remove(state_path_, error);
        } else {
            load_state();
        }

        core_.handle({pico::EventKind::MotionReady,1});
        core_.handle({pico::EventKind::MicrophoneReady,1});
        auto result = dispatch({pico::EventKind::Boot, 0});
        result.event = factory_reset ? "FactoryReset" : "PowerCycle";
        result.message = factory_reset
                             ? "Cleared simulated flash and rebooted"
                             : (restored_ ? "Rebooted with saved state"
                                          : "Rebooted with default state");
        last_result_ = result;
        return result;
    }

    Result status()
    {
        auto result = dispatch({pico::EventKind::RequestStatus, 0});
        result.event = "Status";
        result.message = "Status snapshot";
        last_result_ = result;
        return result;
    }

    std::string json(const Result &result) const
    {
        const auto &state = core_.state();
        std::ostringstream output;
        output << '{'
               << "\"type\":\"snapshot\"," << "\"ok\":"
               << (result.ok ? "true" : "false") << ','
               << "\"event\":" << quoted(result.event) << ','
               << "\"message\":" << quoted(result.message) << ','
               << "\"effects\":" << result.effects << ','
               << "\"effect_names\":" << effect_names_json(result.effects) << ','
               << "\"rendered\":" << (result.rendered ? "true" : "false") << ','
               << "\"notified\":" << (result.notified ? "true" : "false") << ','
               << "\"chirped\":" << (result.chirped ? "true" : "false") << ','
               << "\"persist_queued\":"
               << (result.persist_queued ? "true" : "false") << ','
               << "\"storage_saved\":"
               << (result.storage_saved ? "true" : "false") << ','
               << "\"storage_pending\":"
               << (pending_dirty_ ? "true" : "false") << ','
               << "\"save_due_seconds\":" << save_due_seconds_ << ','
               << "\"led_on\":"
               << (state.connected || state.uptime_seconds % 4U == 0U ? "true"
                                                                      : "false")
               << ',' << "\"capabilities\":" << capabilities() << ','
               << "\"state_path\":" << quoted(state_path_.string()) << ','
               << "\"status_hex\":" << quoted(hex_string(status_packet_)) << ','
               << "\"frame_hex\":" << quoted(hex_string(frame_buffer_)) << ','
               << "\"state\":{"
               << "\"mode\":" << static_cast<unsigned int>(state.mode) << ','
               << "\"mode_name\":" << quoted(mode_name(state.mode)) << ','
               << "\"mood\":" << static_cast<unsigned int>(state.mood) << ','
               << "\"mood_name\":" << quoted(mood_name(state.mood)) << ','
               << "\"dock\":" << static_cast<unsigned int>(state.dock) << ','
               << "\"dock_name\":" << quoted(dock_name(state.dock)) << ','
               << "\"connected\":" << (state.connected ? "true" : "false")
               << ',' << "\"timer_running\":"
               << (state.timer_running ? "true" : "false") << ','
               << "\"battery_percent\":"
               << static_cast<unsigned int>(state.battery_percent) << ','
               << "\"uptime_seconds\":" << state.uptime_seconds << ','
               << "\"timer_total_seconds\":" << state.timer_total_seconds << ','
               << "\"timer_remaining_seconds\":"
               << state.timer_remaining_seconds << ','
               << "\"theme\":" << static_cast<unsigned>(state.theme) << ','
               << "\"screen\":" << static_cast<unsigned>(state.screen) << ','
               << "\"menu_index\":" << static_cast<unsigned>(state.menu_index) << ','
               << "\"eyes_closed\":" << (state.eyes_closed ? "true" : "false") << ','
               << "\"draft_duration_seconds\":" << state.draft_duration_seconds << ','
               << "\"editing_break_duration\":" << (state.editing_break_duration ? "true" : "false") << ','
               << "\"duration_action\":" << static_cast<unsigned>(state.duration_action) << ','
               << "\"motion_ready\":" << (state.motion_ready ? "true" : "false") << ','
               << "\"motion_settled\":" << (state.motion_settled ? "true" : "false") << ','
               << "\"motion_config\":" << static_cast<unsigned>(state.motion_config) << ','
               << "\"microphone_ready\":" << (state.microphone_ready ? "true" : "false") << ','
               << "\"tuner_running\":" << (state.tuner_running ? "true" : "false") << ','
               << "\"tuner_signal\":" << static_cast<unsigned>(state.tuner.signal) << ','
               << "\"tuner_note\":" << static_cast<unsigned>(state.tuner.note) << ','
               << "\"tuner_cents\":" << static_cast<int>(state.tuner.cents) << ','
               << "\"tuner_level\":" << static_cast<unsigned>(state.tuner.level) << ','
               << "\"metronome_running\":" << (state.metronome_running ? "true" : "false") << ','
               << "\"metronome_bpm\":" << state.metronome_bpm << ','
               << "\"beats_per_bar\":" << static_cast<unsigned>(state.beats_per_bar) << ','
               << "\"current_beat\":" << static_cast<unsigned>(state.current_beat) << ','
               << "\"beat_pulse\":" << (state.beat_pulse ? "true" : "false") << ','
               << "\"music_index\":" << static_cast<unsigned>(state.music_index) << ','
               << "\"focus_phase\":" << static_cast<unsigned>(state.focus_phase) << ','
               << "\"focus_waiting\":" << (state.focus_waiting ? "true" : "false") << ','
               << "\"completed_work_sessions\":" << state.completed_work_sessions << ','
               << "\"break_duration_seconds\":" << state.break_duration_seconds << ','
               << "\"sound_muted\":" << (state.sound_muted ? "true" : "false") << ','
               << "\"sound_cue\":" << static_cast<unsigned>(state.sound_cue) << ','
               << "\"timer_paused\":" << (state.timer_paused ? "true" : "false") << ','
               << "\"configured_duration_seconds\":" << state.configured_duration_seconds << ','
               << "\"interaction_count\":" << state.interaction_count << "},"
               << "\"catalog\":" << catalog_json() << '}';
        return output.str();
    }

private:
    pico::motion::Detector motion_detector_;
    pico::Capabilities capabilities() const
    {
        return static_cast<pico::Capabilities>(kSimulatedCapabilities |
            (core_.state().microphone_ready ? pico::capability(pico::Capability::Microphone) : 0U) |
            (core_.state().motion_ready ? pico::capability(pico::Capability::Motion) : 0U));
    }
    void apply_effects(Result &result)
    {
        if (pico::has_effect(result.effects, pico::Effect::Persist)) {
            pending_blob_ = pico::persistence::encode(core_.persistent_state());
            pending_dirty_ = true;
            save_due_seconds_ = now_seconds_ + kSaveDelaySeconds;
            result.persist_queued = true;
        }

        if (pico::has_effect(result.effects, pico::Effect::Render)) {
            pico::display::MonoSurface surface{
                frame_buffer_.data(), frame_buffer_.size(),
                pico::display::kDefaultWidth, pico::display::kDefaultHeight,
                pico::display::MonoLayout::HorizontalMsb,
            };
            result.rendered = pico::display::render(core_.state(), surface);
        }

        if (pico::has_effect(result.effects, pico::Effect::Notify)) {
            std::uint8_t encoded[pico::protocol::kStatusSize]{};
            pico::protocol::encode_status(core_.state(), capabilities(),
                                          encoded);
            std::copy(std::begin(encoded), std::end(encoded), status_packet_.begin());
            result.notified = true;
        }

        result.chirped = pico::has_effect(result.effects, pico::Effect::Chirp);
    }

    bool flush_if_due()
    {
        if (!pending_dirty_ || now_seconds_ < save_due_seconds_) {
            return false;
        }

        std::error_code error;
        const auto parent = state_path_.parent_path();
        if (!parent.empty()) {
            std::filesystem::create_directories(parent, error);
            if (error) {
                return false;
            }
        }

        std::ofstream output(state_path_, std::ios::binary | std::ios::trunc);
        if (!output) {
            return false;
        }
        output.write(reinterpret_cast<const char *>(pending_blob_.data()),
                     static_cast<std::streamsize>(pending_blob_.size()));
        output.close();
        if (!output) {
            return false;
        }
        pending_dirty_ = false;
        return true;
    }

    void load_state()
    {
        std::ifstream input(state_path_, std::ios::binary);
        if (!input) {
            return;
        }

        pico::persistence::Blob blob{};
        input.read(reinterpret_cast<char *>(blob.data()),
                   static_cast<std::streamsize>(blob.size()));
        const auto bytes_read = static_cast<std::size_t>(input.gcount());
        if (bytes_read == blob.size() && input.peek() != std::char_traits<char>::eof()) return;
        pico::PersistedState state{};
        restored_ = pico::persistence::decode(blob.data(), bytes_read, state) && core_.restore(state);
    }

    static std::string effect_names_json(pico::Effects effects)
    {
        std::ostringstream output;
        output << '[';
        bool first = true;
        const auto append = [&](pico::Effect effect, const char *name) {
            if (!pico::has_effect(effects, effect)) {
                return;
            }
            if (!first) {
                output << ',';
            }
            first = false;
            output << quoted(name);
        };
        append(pico::Effect::Render, "Render");
        append(pico::Effect::Notify, "Notify");
        append(pico::Effect::Persist, "Persist");
        append(pico::Effect::Chirp, "Chirp");
        output << ']';
        return output.str();
    }

    static std::string catalog_json()
    {
        std::ostringstream output;
        output << "{\"modes\":[";
        std::size_t count = 0;
        const auto *registry = pico::features(count);
        for (std::size_t index = 0; index < count; ++index) {
            if (index != 0U) {
                output << ',';
            }
            output << "{\"id\":" << static_cast<unsigned int>(registry[index].mode)
                   << ",\"name\":" << quoted(registry[index].name)
                   << ",\"prototype_ready\":"
                   << (registry[index].prototype_ready ? "true" : "false") << '}';
        }
        output << "],\"moods\":[";
        for (std::uint8_t index = 0;
             index < static_cast<std::uint8_t>(pico::Mood::Count); ++index) {
            if (index != 0U) {
                output << ',';
            }
            output << "{\"id\":" << static_cast<unsigned int>(index)
                   << ",\"name\":"
                   << quoted(mood_name(static_cast<pico::Mood>(index))) << '}';
        }
        output << "]}";
        return output.str();
    }

    pico::Core core_{};
    std::uint32_t seed_;
    std::filesystem::path state_path_;
    std::uint32_t now_seconds_{0};
    std::uint64_t now_ms_{0};
    pico::persistence::Blob pending_blob_{};
    bool pending_dirty_{false};
    std::uint32_t save_due_seconds_{0};
    bool restored_{false};
    std::array<std::uint8_t, pico::display::kDefaultBufferSize> frame_buffer_{};
    std::array<std::uint8_t, pico::protocol::kStatusSize> status_packet_{};
    Result last_result_{};
};

Result command_error(std::string message)
{
    Result result;
    result.ok = false;
    result.event = "CommandError";
    result.message = std::move(message);
    return result;
}

} // namespace

int main(int argc, char **argv)
{
    std::filesystem::path state_path = "simulator/build/pico-sim-state.bin";
    std::uint32_t seed = 0x5049434fU;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--state" && index + 1 < argc) {
            state_path = argv[++index];
        } else if (argument == "--seed" && index + 1 < argc) {
            const auto parsed = parse_u32(argv[++index]);
            if (!parsed) { std::cerr << "Invalid seed\n"; return 2; }
            seed = *parsed;
        } else if (argument == "--help") {
            std::cout << "pico_sim_backend [--state FILE] [--seed UINT32]\n";
            return 0;
        } else {
            std::cerr << "Unknown argument: " << argument << '\n';
            return 2;
        }
    }

    Simulator simulator(state_path, seed);
    std::cout << simulator.json(simulator.last_result()) << '\n' << std::flush;

    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream input(line);
        std::string command;
        input >> command;
        Result result;

        if (command == "quit") {
            break;
        } else if (command == "event") {
            std::string kind_token;
            std::string value_token{"0"};
            input >> kind_token;
            if (!(input >> value_token)) {
                value_token = "0";
            }
            const auto kind = parse_event_kind(kind_token);
            const auto value = parse_u32(value_token);
            result = !kind.has_value() || !value.has_value()
                         ? command_error("Usage: event EVENT_NAME_OR_ID [VALUE]")
                         : simulator.dispatch({*kind, *value});
        } else if (command == "advance") {
            std::string seconds_token;
            input >> seconds_token;
            const auto seconds = parse_u32(seconds_token);
            result = !seconds.has_value()
                         ? command_error("Usage: advance SECONDS")
                         : simulator.advance(*seconds);
        } else if (command == "motion") {
            std::int32_t x,y,z; std::uint32_t elapsed;
            if (input >> x >> y >> z >> elapsed && x>=-4000 && x<=4000 && y>=-4000 && y<=4000 &&
                z>=-4000 && z<=4000 && elapsed<=60000)
                result = simulator.motion_sample({x,y,z},elapsed);
            else result = command_error("Usage: motion X_MG Y_MG Z_MG DELTA_MS");
        } else if (command == "motion_test") {
            std::string name; input >> name;
            result = name == "pickup" || name == "rock" || name == "rest" || name == "bump" || name == "still" ?
                simulator.motion_test(name) : command_error("Usage: motion_test still|pickup|rock|rest|bump");
        } else if (command == "tone") {
            std::string token;
            input >> token;
            const auto frequency = parse_u32(token);
            result = frequency && *frequency <= 2000000 ? simulator.tone(*frequency) :
                command_error("Usage: tone MILLIHZ (0..2000000)");
        } else if (command == "advance_ms") {
            std::string token; input >> token;
            const auto milliseconds = parse_u32(token);
            result = milliseconds ? simulator.advance_ms(*milliseconds)
                                  : command_error("Usage: advance_ms MILLISECONDS");
        } else if (command == "ble") {
            std::string packet;
            std::getline(input, packet);
            const auto bytes = parse_hex(packet);
            result = !bytes.has_value()
                         ? command_error("Usage: ble HEX_BYTES")
                         : simulator.ble_command(*bytes);
        } else if (command == "power_cycle") {
            result = simulator.power_cycle(false);
        } else if (command == "factory_reset") {
            result = simulator.power_cycle(true);
        } else if (command == "status" || command.empty()) {
            result = simulator.status();
        } else {
            result = command_error("Unknown command: " + command);
        }

        std::cout << simulator.json(result) << '\n' << std::flush;
    }
    return 0;
}
