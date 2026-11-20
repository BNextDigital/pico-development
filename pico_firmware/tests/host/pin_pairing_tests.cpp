#include <pico/core.hpp>
#include <pico/persistence.hpp>
#include <pico/protocol.hpp>
#include <pico/display_model.hpp>
#include <array>
#include <cassert>
#include <iostream>
using namespace pico;
void open(Core &core) {
 core.handle({EventKind::OpenSettings,0});
 while (core.state().settings_index!=12) core.handle({EventKind::NavigateNext,0});
 core.handle({EventKind::Select,0});
 assert(core.state().screen==Screen::Pairing);
}
int main() {
 Core core; core.handle({EventKind::Boot,0}); core.handle({EventKind::Tick,10});
 core.handle({EventKind::SetTheme,4});
 const auto durable=persistence::encode(core.persistent_state());
 const auto clock=clock::encode(core.clock_settings());
 open(core);
 core.handle({EventKind::SleepDisplay,0});
 assert(core.state().display_asleep);
 assert(!has_effect(core.handle({EventKind::FocusButtonHeld,0}),Effect::OpenPairing));
 assert(!core.state().display_asleep);
 open(core);
 assert(!has_effect(core.handle({EventKind::Select,0}),Effect::OpenPairing));
 assert(!has_effect(core.handle({EventKind::FocusButtonTapped,0}),Effect::OpenPairing));
 const auto start=core.handle({EventKind::FocusButtonHeld,0});
 assert(has_effect(start,Effect::OpenPairing) && !has_effect(start,Effect::Persist));
 const auto token=core.state().pairing.token;
 assert(core.state().pairing.active());
 core.handle({EventKind::PairingPasskey,37,token+1});
 assert(core.state().pairing.stage==PairingStage::Waiting);
 core.handle({EventKind::PairingPasskey,1000000,token});
 assert(core.state().pairing.stage==PairingStage::Waiting);
 core.handle({EventKind::PairingPasskey,37,token});
 assert(core.state().pairing.stage==PairingStage::Passkey && core.state().pairing.passkey==37);
 core.handle({EventKind::SleepDisplay,0}); assert(!core.state().display_asleep);
 std::array<std::uint8_t,1024> frame{};
 display::MonoSurface surface{frame.data(),frame.size(),128,64,display::MonoLayout::HorizontalMsb};
 display::render(core.state(),surface); const auto pin_frame=frame;
 core.handle({EventKind::PairingPasskey,123456,token}); display::render(core.state(),surface);
 assert(frame!=pin_frame);
 assert(persistence::encode(core.persistent_state())==durable && clock::encode(core.clock_settings())==clock);
 assert(has_effect(core.handle({EventKind::Tick,130}),Effect::ClosePairing));
 assert(core.state().pairing.stage==PairingStage::TimedOut && core.state().pairing.passkey==0);
 core.handle({EventKind::PairingPasskey,37,token}); assert(!core.state().pairing.active());
 open(core); core.handle({EventKind::FocusButtonHeld,0});
 const auto next=core.state().pairing.token; assert(next!=token);
 core.handle({EventKind::PairingFailed,1,token}); assert(core.state().pairing.active());
 core.handle({EventKind::PairingComplete,0,next});
 assert(core.state().pairing.stage==PairingStage::Paired && core.state().pairing.passkey==0);
 open(core); core.handle({EventKind::FocusButtonHeld,0});
 assert(has_effect(core.handle({EventKind::Back,0}),Effect::ClosePairing));
 assert(core.state().pairing.passkey==0 && !core.state().pairing.active());
 core.handle({EventKind::StartTimer,1}); open(core); core.handle({EventKind::FocusButtonHeld,0});
 core.handle({EventKind::Tick,131}); assert(!core.state().pairing.active());
 core.handle({EventKind::Boot,0}); assert(!core.state().pairing.active() && core.state().pairing.passkey==0);
 std::cout << "PIN window, volatile code, stale callback, timeout, alert and cancellation checks passed\n";
}
