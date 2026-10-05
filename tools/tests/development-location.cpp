#include "../../source/includes/vrdevlocation.hpp"
#include <cassert>
#include <limits>
int main()
{
    VRDevLocation::Position value{1,2,3,4};
    assert(VRDevLocation::Parse("-100.25 250 12.5 90",value));
    assert(value.x==-100.25f && value.y==250 && value.z==12.5f && value.heading==90);
    for(const auto text:{"","1 2 3","1 2 3 4 extra","1  2 3 4","nan 2 3 4",
        "1 inf 3 4","9000 2 3 4","1 2 -1025 4","1 2 4097 4","1 2 3 361"})
    {
        const auto before=value;
        assert(!VRDevLocation::Parse(text,value));
        assert(value.x==before.x && value.y==before.y && value.z==before.z && value.heading==before.heading);
    }
    assert(VRDevLocation::Parse("-8192 8192 -1024 -360",value));
    assert(VRDevLocation::Parse("0 0 4096 360",value));
    value.z=std::numeric_limits<float>::infinity();
    assert(!VRDevLocation::Valid(value));
}
