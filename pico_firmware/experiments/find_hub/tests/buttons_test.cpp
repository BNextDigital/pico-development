#include "../src/buttons.hpp"
#include <cassert>
#include <cstdint>
#include <iostream>
using pico::fhn::Action;
using pico::fhn::Buttons;
int main()
{
    Buttons b;
    assert(!b.pending());
    assert(b.update(1,100,false)==Action::None);
    b.update(0,110,false); b.update(1,120,false);
    assert(b.update(1,149,false)==Action::None);
    b.update(1,150,false); b.update(0,180,false);
    assert(b.update(0,210,false)==Action::Pairing);
    assert(!b.pending());
    b.update(2,300,false); b.update(2,330,false); b.update(0,3330,false);
    assert(b.update(0,3360,false)==Action::Recovery);
    b.update(1,4000,true); assert(b.update(1,4030,true)==Action::StopRinging);
    assert(b.update(1,4100,false)==Action::None);
    b.update(0,4200,false); assert(b.update(0,4230,false)==Action::None);
    b.update(3,5000,false); b.update(3,5030,false);
    assert(b.update(3,10029,false)==Action::None);
    assert(b.update(3,10030,false)==Action::Reset);
    assert(b.update(3,11000,false)==Action::None);
    b.update(1,11100,false); assert(b.update(1,11130,false)==Action::None);
    b.update(0,11200,false); assert(b.update(0,11230,false)==Action::None);
    assert(!b.pending());
    Buttons wrap;
    const std::uint32_t t=UINT32_MAX-100U;
    wrap.update(2,t,false); wrap.update(2,t+30U,false); wrap.update(0,t+3030U,false);
    assert(wrap.update(0,t+3060U,false)==Action::Recovery);
    std::cout << "FHN button debounce, holds, ringing, reset and rollover passed\n";
}
