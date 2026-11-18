#include <vector>
#include <pico/core.hpp>
#include <pico/button.hpp>
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
    CHECK(pico::feature_for(pico::Mode::Music)->prototype_ready);
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
    CHECK(pico::display::pixel_at(horizontal_surface, 3, 3));

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

void test_theme_events_and_persistence()
{
    pico::Core core;
    CHECK(core.state().theme == pico::FaceTheme::Modern);
    const auto changed = core.handle({pico::EventKind::SetTheme,
        static_cast<std::uint32_t>(pico::FaceTheme::KawaiiMascot)});
    CHECK(core.state().theme == pico::FaceTheme::KawaiiMascot);
    CHECK(pico::has_effect(changed, pico::Effect::Render));
    CHECK(pico::has_effect(changed, pico::Effect::Persist));
    core.handle({pico::EventKind::PrimaryPressed, 0});

    const auto blob = pico::persistence::encode(core.persistent_state());
    pico::PersistedState persisted{};
    CHECK(pico::persistence::decode(blob, persisted));
    pico::Core restored;
    CHECK(restored.restore(persisted));
    CHECK(restored.state().theme == pico::FaceTheme::KawaiiMascot);
    CHECK(restored.state().interaction_count == 1U);

    core.handle({pico::EventKind::CycleTheme, 0});
    CHECK(core.state().theme == pico::FaceTheme::Modern);
    for (const auto invalid : {static_cast<std::uint32_t>(pico::FaceTheme::Count),
                               256U, UINT32_MAX}) {
        CHECK(core.handle({pico::EventKind::SetTheme, invalid}) ==
              pico::effect(pico::Effect::None));
        CHECK(core.state().theme == pico::FaceTheme::Modern);
    }

    persisted.theme = pico::FaceTheme::Count;
    CHECK(!restored.restore(persisted));
    CHECK(!pico::persistence::decode(pico::persistence::encode(persisted), persisted));

    // A real version-1 record: Companion, Neutral, reserved byte 0,
    // interaction count 42, followed by its original FNV-1a checksum.
    const std::array<std::uint8_t, 16> legacy{
        0x50, 0x49, 0x43, 0x4f, 0x01, 0x00, 0x00, 0x00,
        0x2a, 0x00, 0x00, 0x00, 0xaf, 0x6c, 0x55, 0xef,
    };
    CHECK(pico::persistence::decode(legacy.data(), legacy.size(), persisted));
    CHECK(persisted.theme == pico::FaceTheme::Modern);
    CHECK(persisted.interaction_count == 42U);
}

void test_button_debounce_and_hold()
{
    using pico::ButtonAction;
    pico::Button button;
    CHECK(button.update(true, 100) == ButtonAction::None);
    CHECK(button.update(false, 110) == ButtonAction::None);
    CHECK(button.update(false, 150) == ButtonAction::None); // bounce ignored
    CHECK(button.update(true, 200) == ButtonAction::None);
    CHECK(button.update(true, 230) == ButtonAction::None);
    CHECK(button.update(false, 300) == ButtonAction::None);
    CHECK(button.update(true, 310) == ButtonAction::None); // release bounce
    CHECK(button.update(false, 320) == ButtonAction::None);
    CHECK(button.update(false, 350) == ButtonAction::Tap);
    CHECK(button.update(false, 400) == ButtonAction::None);

    CHECK(button.update(true, 500) == ButtonAction::None);
    CHECK(button.update(true, 530) == ButtonAction::None);
    CHECK(button.update(true, 1529) == ButtonAction::None);
    CHECK(button.update(true, 1530) == ButtonAction::Hold);
    CHECK(button.update(true, 1600) == ButtonAction::None);
    CHECK(button.update(false, 1700) == ButtonAction::None);
    CHECK(button.update(false, 1730) == ButtonAction::None); // no tap after hold

    pico::Button boundary;
    boundary.update(true, 100);
    boundary.update(true, 130);
    boundary.update(false, 1130);
    CHECK(boundary.update(false, 1160) == ButtonAction::Hold);

    pico::Button wrap;
    wrap.update(true, UINT32_MAX - 50U);
    wrap.update(true, UINT32_MAX - 20U);
    CHECK(wrap.update(true, 980U) == ButtonAction::Hold);
    wrap.update(false, 1000U);
    CHECK(wrap.update(false, 1030U) == ButtonAction::None);
}

void test_focus_button_standalone_controls()
{
    pico::Core core;
    core.handle({pico::EventKind::Tick, 100});
    core.handle({pico::EventKind::PrimaryPressed, 0}); // BLE pat remains a pat
    CHECK(!core.state().timer_running);
    CHECK(core.state().mode == pico::Mode::Companion);

    const auto started = core.handle({pico::EventKind::FocusButtonTapped, 0});
    CHECK(core.state().mode == pico::Mode::Focus);
    CHECK(core.state().timer_running);
    CHECK(core.state().timer_remaining_seconds == 1500U);
    CHECK(pico::has_effect(started, pico::Effect::Persist));
    core.handle({pico::EventKind::Tick, 111});
    core.handle({pico::EventKind::FocusButtonTapped, 0});
    CHECK(core.state().mood == pico::Mood::Happy);
    CHECK(core.state().timer_remaining_seconds == 1489U);
    core.handle({pico::EventKind::Tick, 112});
    CHECK(core.state().timer_remaining_seconds == 1488U); // no timer restart

    core.handle({pico::EventKind::FocusButtonHeld, 0});
    CHECK(!core.state().timer_running);
    CHECK(core.state().mode == pico::Mode::Companion);
    CHECK(core.state().mood == pico::Mood::Neutral);
    CHECK(core.handle({pico::EventKind::FocusButtonHeld, 0}) ==
          pico::effect(pico::Effect::None));

    core.handle({pico::EventKind::SetMode,
                 static_cast<std::uint32_t>(pico::Mode::Cooking)});
    core.handle({pico::EventKind::StartTimer, 30});
    core.handle({pico::EventKind::FocusButtonTapped, 0});
    core.handle({pico::EventKind::FocusButtonHeld, 0});
    CHECK(core.state().mode == pico::Mode::Cooking);
    CHECK(core.state().timer_remaining_seconds == 30U);
    CHECK(core.state().timer_running); // leave other modes' timers alone
    core.handle({pico::EventKind::CancelTimer, 0});
    core.handle({pico::EventKind::DockChanged,
                 static_cast<std::uint32_t>(pico::Dock::Car)});
    core.handle({pico::EventKind::FocusButtonTapped, 0});
    CHECK(core.state().mode == pico::Mode::Driving);
    CHECK(!core.state().timer_running);
}

#include "sprint01_tests.hpp"
#include "sprint02_tests.hpp"
#include "music_tests.hpp"

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
    test_theme_events_and_persistence();
    run_sprint01_tests();
    run_sprint02_tests();
    run_music_tests();
    test_button_debounce_and_hold();
    test_focus_button_standalone_controls();

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return EXIT_FAILURE;
    }

    std::cout << "All Pico core tests passed\n";
    return EXIT_SUCCESS;
}
