#include <pico/core.hpp>
#include <pico/display_model.hpp>
#include <pico/features.hpp>
#include <pico/persistence.hpp>
#include <pico/protocol.hpp>

#include <array>
#include <cstdlib>
#include <iostream>

namespace {

int failures = 0;

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            std::cerr << __FILE__ << ':' << __LINE__                            \
                      << " CHECK failed: " #condition << '\n';                  \
            ++failures;                                                         \
        }                                                                       \
    } while (false)

void test_interaction_and_mood_reset()
{
    pico::Core core;
    const auto effects = core.handle({pico::EventKind::PrimaryPressed, 0});

    CHECK(core.state().mood == pico::Mood::Happy);
    CHECK(core.state().interaction_count == 1U);
    CHECK(pico::has_effect(effects, pico::Effect::Chirp));
    CHECK(pico::has_effect(effects, pico::Effect::Persist));
    CHECK(core.persistent_state().mood == pico::Mood::Neutral);

    core.handle({pico::EventKind::Tick, 3});
    CHECK(core.state().mood == pico::Mood::Happy);
    core.handle({pico::EventKind::Tick, 4});
    CHECK(core.state().mood == pico::Mood::Neutral);
}

void test_car_dock_owns_driving_mode()
{
    pico::Core core;
    core.handle({pico::EventKind::SetMode,
                 static_cast<std::uint32_t>(pico::Mode::Music)});
    core.handle({pico::EventKind::DockChanged,
                 static_cast<std::uint32_t>(pico::Dock::Car)});

    CHECK(core.state().mode == pico::Mode::Driving);
    core.handle({pico::EventKind::SetMode,
                 static_cast<std::uint32_t>(pico::Mode::Focus)});
    CHECK(core.state().mode == pico::Mode::Driving);

    core.handle({pico::EventKind::DockChanged,
                 static_cast<std::uint32_t>(pico::Dock::None)});
    CHECK(core.state().mode == pico::Mode::Music);
}

void test_timer_uses_deadline_not_tick_count()
{
    pico::Core core;
    core.handle({pico::EventKind::Tick, 100});
    core.handle({pico::EventKind::StartTimer, 25});
    core.handle({pico::EventKind::Tick, 111});
    CHECK(core.state().timer_remaining_seconds == 14U);

    const auto effects = core.handle({pico::EventKind::Tick, 125});
    CHECK(!core.state().timer_running);
    CHECK(core.state().timer_remaining_seconds == 0U);
    CHECK(core.state().mood == pico::Mood::Alert);
    CHECK(pico::has_effect(effects, pico::Effect::Chirp));
}

void test_protocol_rejects_invalid_commands()
{
    const std::uint8_t empty_timer[]{0x04, 0, 0, 0, 0};
    CHECK(pico::protocol::decode_command(empty_timer, sizeof(empty_timer)).status ==
          pico::protocol::DecodeStatus::InvalidValue);

    const std::uint8_t driving_command[]{
        0x02, static_cast<std::uint8_t>(pico::Mode::Driving)};
    CHECK(pico::protocol::decode_command(driving_command,
                                         sizeof(driving_command)).status ==
          pico::protocol::DecodeStatus::InvalidValue);

    const std::uint8_t timer[]{0x04, 0x2C, 0x01, 0x00, 0x00};
    const auto decoded = pico::protocol::decode_command(timer, sizeof(timer));
    CHECK(decoded.status == pico::protocol::DecodeStatus::Ok);
    CHECK(decoded.event.kind == pico::EventKind::StartTimer);
    CHECK(decoded.event.value == 300U);

    const std::uint8_t excessive_timer[]{0x04, 0x01, 0x3B, 0x09, 0x00};
    CHECK(pico::protocol::decode_command(excessive_timer,
                                         sizeof(excessive_timer)).status ==
          pico::protocol::DecodeStatus::InvalidValue);
}

void test_status_packet_is_stable()
{
    pico::Core core;
    core.handle({pico::EventKind::Connected, 0});
    core.handle({pico::EventKind::StartTimer, 300});

    std::uint8_t status[pico::protocol::kStatusSize]{};
    const auto capabilities = pico::capability(pico::Capability::Display) |
                              pico::capability(pico::Capability::Speaker);
    pico::protocol::encode_status(core.state(), capabilities, status);

    CHECK(status[0] == pico::kProtocolVersion);
    CHECK((status[4] & 0x03U) == 0x03U);
    CHECK(status[6] == 0x2CU);
    CHECK(status[7] == 0x01U);
    CHECK(status[14] == capabilities);
}

void test_persisted_state_validation()
{
    pico::Core core;
    CHECK(core.restore({pico::Mode::Focus, pico::Mood::Curious, 42}));
    CHECK(core.state().mode == pico::Mode::Focus);
    CHECK(core.state().interaction_count == 42U);
    CHECK(!core.restore({pico::Mode::Driving, pico::Mood::Neutral, 0}));

    CHECK(core.restore({pico::Mode::Companion, pico::Mood::Neutral,
                        UINT32_MAX}));
    core.handle({pico::EventKind::PrimaryPressed, 0});
    CHECK(core.state().interaction_count == UINT32_MAX);
}

void test_feature_registry_and_cycle()
{
    std::size_t count = 0;
    const auto *registry = pico::features(count);
    CHECK(registry != nullptr);
    CHECK(count == 7U);
    CHECK(pico::feature_for(pico::Mode::Focus)->prototype_ready);
    CHECK(!pico::feature_for(pico::Mode::Music)->prototype_ready);
    CHECK(pico::next_user_mode(pico::Mode::Notes) == pico::Mode::Companion);
    CHECK(pico::next_user_mode(pico::Mode::Driving) == pico::Mode::Companion);
}

void test_decoder_handles_arbitrary_short_packets()
{
    std::uint8_t packet[8]{};
    for (std::uint16_t command = 0; command <= 0xFFU; ++command) {
        packet[0] = static_cast<std::uint8_t>(command);
        for (std::size_t size = 0; size <= sizeof(packet); ++size) {
            const auto result = pico::protocol::decode_command(packet, size);
            CHECK(result.status <= pico::protocol::DecodeStatus::InvalidValue);
        }
    }
}

void test_display_model_is_layout_independent()
{
    pico::State state{};
    state.connected = true;
    state.mood = pico::Mood::Happy;
    state.timer_running = true;
    state.timer_total_seconds = 60;
    state.timer_remaining_seconds = 30;

    std::array<std::uint8_t, pico::display::kDefaultBufferSize> horizontal{};
    std::array<std::uint8_t, pico::display::kDefaultBufferSize> vertical{};
    pico::display::MonoSurface horizontal_surface{
        horizontal.data(), horizontal.size(), pico::display::kDefaultWidth,
        pico::display::kDefaultHeight, pico::display::MonoLayout::HorizontalMsb,
    };
    pico::display::MonoSurface vertical_surface{
        vertical.data(), vertical.size(), pico::display::kDefaultWidth,
        pico::display::kDefaultHeight, pico::display::MonoLayout::VerticalLsb,
    };

    CHECK(pico::display::render(state, horizontal_surface));
    CHECK(pico::display::render(state, vertical_surface));
    for (std::uint16_t y = 0; y < pico::display::kDefaultHeight; ++y) {
        for (std::uint16_t x = 0; x < pico::display::kDefaultWidth; ++x) {
            CHECK(pico::display::pixel_at(horizontal_surface, x, y) ==
                  pico::display::pixel_at(vertical_surface, x, y));
        }
    }

    CHECK(pico::display::pixel_at(horizontal_surface, 3, 3));
    CHECK(pico::display::pixel_at(horizontal_surface, 41, 24));

    pico::display::MonoSurface short_surface{
        horizontal.data(), 4, pico::display::kDefaultWidth,
        pico::display::kDefaultHeight, pico::display::MonoLayout::HorizontalMsb,
    };
    CHECK(!pico::display::render(state, short_surface));
}

void test_persistence_codec_round_trip_and_validation()
{
    const pico::PersistedState original{
        pico::Mode::Focus, pico::Mood::Curious, 123456U,
    };
    auto blob = pico::persistence::encode(original);
    CHECK(blob[0] == 'P');
    CHECK(blob[1] == 'I');
    CHECK(blob[2] == 'C');
    CHECK(blob[3] == 'O');

    pico::PersistedState restored{};
    CHECK(pico::persistence::decode(blob, restored));
    CHECK(restored.mode == original.mode);
    CHECK(restored.mood == original.mood);
    CHECK(restored.interaction_count == original.interaction_count);

    blob[8] ^= 0x01U;
    CHECK(!pico::persistence::decode(blob, restored));

    const auto driving = pico::persistence::encode(
        {pico::Mode::Driving, pico::Mood::Neutral, 0});
    CHECK(!pico::persistence::decode(driving, restored));
}

} // namespace

int main()
{
    test_interaction_and_mood_reset();
    test_car_dock_owns_driving_mode();
    test_timer_uses_deadline_not_tick_count();
    test_protocol_rejects_invalid_commands();
    test_status_packet_is_stable();
    test_persisted_state_validation();
    test_feature_registry_and_cycle();
    test_decoder_handles_arbitrary_short_packets();
    test_display_model_is_layout_independent();
    test_persistence_codec_round_trip_and_validation();

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return EXIT_FAILURE;
    }

    std::cout << "All Pico core tests passed\n";
    return EXIT_SUCCESS;
}
