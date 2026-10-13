#include "../../source/includes/vrsettingsmodel.hpp"
#include <cassert>
#include <limits>
int main()
{
    using namespace VRSettingsModel;
    const float nan=std::numeric_limits<float>::quiet_NaN();
    const float inf=std::numeric_limits<float>::infinity();
    assert(FloorHeight(1.70f,1.72f,true,true,true).value()>1.70f);
    assert(!FloorHeight(1.7f,1.7f,false,true,true)); // LOCAL has no known floor.
    assert(!FloorHeight(1.7f,1.7f,true,false,true));
    assert(!FloorHeight(1.7f,1.7f,true,true,false));
    assert(!FloorHeight(nan,1.7f,true,true,true));
    assert(!FloorHeight(inf,1.7f,true,true,true));
    assert(!FloorHeight(-1,-1,true,true,true));
    assert(!FloorHeight(0,0,true,true,true));
    // UI ranges never restrict values loaded from a valid INI.
    assert(Valid(Number::PlayerHeight,3.5f));
    assert(Valid(Number::DashboardRight,-4));
    assert(Valid(Number::HudWidth,2));
    assert(!Valid(Number::PlayerHeight,0));
    for(unsigned i=0;i<unsigned(Number::Count);++i)
    {
        assert(Valid(Number(i),numbers[i].fallback));
        assert(!Valid(Number(i),nan) && !Valid(Number(i),inf));
    }
    assert(!Valid(Number::Count,1));
}
