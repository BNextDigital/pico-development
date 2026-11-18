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
    }
    return "Event" + std::to_string(static_cast<unsigned int>(kind));
}

std::optional<pico::EventKind> parse_event_kind(const std::string &token)
{
    constexpr std::array<pico::EventKind, 13> kinds{
        pico::EventKind::Boot,          pico::EventKind::Tick,
        pico::EventKind::PrimaryPressed, pico::EventKind::SecondaryPressed,
        pico::EventKind::Connected,     pico::EventKind::Disconnected,
        pico::EventKind::DockChanged,   pico::EventKind::SetMode,
        pico::EventKind::SetMood,       pico::EventKind::SetBattery,
        pico::EventKind::StartTimer,    pico::EventKind::CancelTimer,
        pico::EventKind::RequestStatus,
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
    explicit Simulator(std::filesystem::path state_path)
        : state_path_(std::move(state_path))
    {
        load_state();
        last_result_ = dispatch({pico::EventKind::Boot, 0});
        last_result_.message = restored_ ? "Booted with saved state"
                                         : "Booted with default state";
    }

    const Result &last_result() const { return last_result_; }

    Result dispatch(const pico::Event &event)
    {
        Result result;
        result.event = event_name(event.kind);
        result.effects = core_.handle(event);
        apply_effects(result);
        result.storage_saved = flush_if_due();
        last_result_ = result;
        return result;
    }

    Result advance(std::uint32_t seconds)
    {
        if (seconds > std::numeric_limits<std::uint32_t>::max() - now_seconds_) {
            now_seconds_ = std::numeric_limits<std::uint32_t>::max();
        } else {
            now_seconds_ += seconds;
        }
        auto result = dispatch({pico::EventKind::Tick, now_seconds_});
        result.event = "Advance";
        result.message = "Advanced simulated time by " + std::to_string(seconds) +
                         " second" + (seconds == 1U ? "" : "s");
        last_result_ = result;
        return result;
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
        core_ = pico::Core{};
        restored_ = false;
        if (factory_reset) {
            std::error_code error;
            std::filesystem::remove(state_path_, error);
        } else {
            load_state();
        }

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
               << ',' << "\"capabilities\":" << kSimulatedCapabilities << ','
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
               << "\"interaction_count\":" << state.interaction_count << "},"
               << "\"catalog\":" << catalog_json() << '}';
        return output.str();
    }

private:
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
            pico::protocol::encode_status(core_.state(), kSimulatedCapabilities,
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
        if (input.gcount() != static_cast<std::streamsize>(blob.size())) {
            return;
        }

        pico::PersistedState state{};
        restored_ = pico::persistence::decode(blob, state) && core_.restore(state);
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
    std::filesystem::path state_path_;
    std::uint32_t now_seconds_{0};
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
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--state" && index + 1 < argc) {
            state_path = argv[++index];
        } else if (argument == "--help") {
            std::cout << "pico_sim_backend [--state FILE]\n";
            return 0;
        } else {
            std::cerr << "Unknown argument: " << argument << '\n';
            return 2;
        }
    }

    Simulator simulator(state_path);
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
