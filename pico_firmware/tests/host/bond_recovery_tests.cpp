#include <pico/core.hpp>
#include <pico/persistence.hpp>
#include <pico/protocol.hpp>
#include <cassert>
#include <iostream>
using pico::EventKind;
void open_recovery(pico::Core &core)
{
    core.handle({EventKind::OpenSettings,0});
    // Index 11 is appended; existing index 10 Back stays unchanged.
    while (core.state().settings_index!=11) core.handle({EventKind::NavigateNext,0});
    core.handle({EventKind::Select,0});
    assert(core.state().screen==pico::Screen::PairingRecovery);
}
int main()
{
    pico::Core core;
    core.handle({EventKind::Boot,0});
    core.handle({EventKind::SetTheme,4});
    core.handle({EventKind::SetClockOptions,6});
    const auto personality=pico::persistence::encode(core.persistent_state());
    const auto clock=pico::clock::encode(core.clock_settings());
    // Holding Home never forgets phones.
    assert(!pico::has_effect(core.handle({EventKind::FocusButtonHeld,0}),pico::Effect::ForgetCompanionBonds));
    open_recovery(core);
    const std::uint8_t select[]{0x13};
    const auto decoded=pico::protocol::decode_command(select,1);
    assert(decoded.status==pico::protocol::DecodeStatus::Ok);
    assert(!pico::has_effect(core.handle(decoded.event),pico::Effect::ForgetCompanionBonds));
    assert(!pico::has_effect(core.handle({EventKind::FocusButtonTapped,0}),pico::Effect::ForgetCompanionBonds));
    core.handle({EventKind::SleepDisplay,0});
    assert(core.state().display_asleep);
    assert(!pico::has_effect(core.handle({EventKind::FocusButtonHeld,0}),pico::Effect::ForgetCompanionBonds));
    assert(!core.state().display_asleep);
    // Sleep Now returns Home, so reopen the confirmation before holding.
    open_recovery(core);
    const auto request=core.handle({EventKind::FocusButtonHeld,0});
    assert(pico::has_effect(request,pico::Effect::ForgetCompanionBonds));
    assert(!pico::has_effect(request,pico::Effect::Persist));
    assert(!pico::has_effect(request,pico::Effect::PersistClock));
    assert(core.state().pairing_reset==pico::PairingResetState::Pending);
    assert(!pico::has_effect(core.handle({EventKind::FocusButtonHeld,0}),pico::Effect::ForgetCompanionBonds));
    core.handle({EventKind::PairingResetResult,1});
    assert(core.state().pairing_reset==pico::PairingResetState::Failed);
    open_recovery(core);
    core.handle({EventKind::FocusButtonHeld,0});
    core.handle({EventKind::PairingResetResult,0});
    assert(core.state().pairing_reset==pico::PairingResetState::Cleared);
    assert(personality==pico::persistence::encode(core.persistent_state()));
    assert(clock==pico::clock::encode(core.clock_settings()));
    core.handle({EventKind::Back,0}); assert(core.state().screen==pico::Screen::Settings);
    const std::uint8_t unsupported[]{0x48};
    assert(pico::protocol::decode_command(unsupported,1).status==pico::protocol::DecodeStatus::UnknownCommand);
    std::cout << "Local bond-reset confirmation and settings-preservation checks passed\n";
}
