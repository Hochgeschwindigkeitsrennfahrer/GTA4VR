#include "../../source/includes/vrphoneinput.hpp"
#include <cassert>
#include <cstdio>

int main()
{
    VRPhoneInput::Edges edges;
    std::array<bool,6> held{};
    held[1]=true;edges.Sample(held);held[1]=false;edges.Sample(held);
    VRPhoneInput::Pulse pulse;
    // A whole tap occurred between simulation updates; it must still arrive.
    assert(pulse.Update(edges.presses[1],false,true,100));
    assert(pulse.Update(edges.presses[1],false,true,219));
    assert(!pulse.Update(edges.presses[1],false,true,220));
    assert(!pulse.Update(edges.presses[1],false,true,1000));
    // Two quick taps remain separate, with a native release between them.
    assert(pulse.Update(3,false,true,1001));
    assert(!pulse.Update(3,false,true,1121));
    assert(!pulse.Update(3,false,true,1170));
    assert(pulse.Update(3,false,true,1171));
    assert(!pulse.Update(3,false,true,1291));
    // Holding keeps native held input, without manufacturing repeat edges.
    assert(pulse.Update(4,true,true,1400));
    assert(pulse.Update(4,true,true,2000));
    assert(!pulse.Update(4,false,true,2001));
    // Phone close/focus loss cancels queued presses; reopening does not replay.
    assert(pulse.Update(6,false,true,2100));
    assert(!pulse.Update(6,false,false,2110));
    assert(!pulse.Update(0,false,false,2120));
    assert(!pulse.Update(6,false,true,2200));
    assert(pulse.Update(7,false,true,2210));
    std::puts("Phone input edge/pulse checks passed");
}
