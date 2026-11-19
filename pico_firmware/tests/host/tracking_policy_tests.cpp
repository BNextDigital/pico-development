#include <pico/tracking_policy.hpp>
#include <cassert>
int main()
{
    using namespace pico::tracking;
    assert(companion_identity(0));
    assert(!companion_identity(1));
    assert(companion_advertising(0,0,4,2));
    assert(!companion_advertising(1,1,4,2));
    assert(!companion_advertising(0,4,4,2));
    assert(!companion_advertising(0,2,4,2));
    // Recycled slot permits advertising again; no second companion.
    assert(companion_advertising(0,1,4,2));
    assert(alert(true,true,true,true) == Alert::UnwantedTracking);
    assert(alert(true,true,true,false) == Alert::Timer);
    assert(alert(true,true,false,false) == Alert::OwnerRing);
    assert(alert(true,false,false,false) == Alert::LocalFind);
    assert(alert(false,false,false,false) == Alert::None);
    assert(!capture_microphone(true, Alert::UnwantedTracking));
    assert(!capture_microphone(true, Alert::OwnerRing));
    assert(capture_microphone(true, Alert::None));
    assert(!capture_microphone(false, Alert::None));
    assert(motion_mask(0,1) == 1); // User disable cannot disable safety demand.
    assert(motion_mask(2,1) == 3);
}
