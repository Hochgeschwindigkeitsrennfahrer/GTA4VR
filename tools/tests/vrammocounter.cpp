#include "../../source/includes/vrammocounter.hpp"
#include <cassert>

int main()
{
    VRAmmoCounter::Snapshot sample{};sample.gun=VRMath::Identity();
    sample.capacity=17;sample.valid=true;
    auto empty=VRAmmoCounter::Geometry(sample);
    assert(empty.size()==(1+17)*6+14*12);
    assert(VRAmmoCounter::DigitMasks(218)==std::vector<unsigned>({0x5b,0x06,0x7f}));
    assert(VRAmmoCounter::DigitMasks(1000)==std::vector<unsigned>({0x06,0x3f,0x3f,0x3f}));
    VRAmmoCounter::Settings oriented{};oriented.rotation[0]=27;oriented.rotation[1]=-32;oriented.rotation[2]=41;
    const auto initial=VRAmmoCounter::Frame(oriented);
    VRAmmoCounter::SetFrame(oriented,initial);
    const auto restored=VRAmmoCounter::Frame(oriented);
    for(unsigned r=0;r<4;++r) for(unsigned c=0;c<4;++c) assert(std::abs(initial[r][c]-restored[r][c])<1.e-5f);
    auto stacked=sample;stacked.capacity=50;stacked.settings.stacked=true;
    const auto grid=VRAmmoCounter::Geometry(stacked);
    assert(grid.size()==51*6+14*12);
    assert(std::abs(grid[6].z-grid[6+25*6].z)>.001f); // both rows represented
    sample.rounds=17;
    auto full=VRAmmoCounter::Geometry(sample);
    unsigned emptyLit=0,fullLit=0;
    for(const auto& v:empty) emptyLit+=v.color==0xffffcf62;
    for(const auto& v:full) fullLit+=v.color==0xffffcf62;
    assert(fullLit>emptyLit);
    // The panel faces backward, screen right -Y, up +Z.
    assert(full[1].y<full[0].y && full[2].z>full[0].z);
    assert(std::abs(full[0].x-sample.settings.offset[0])<1.e-6f);
    sample.gun[3]={12,23,34,1};
    auto moved=VRAmmoCounter::Geometry(sample);
    for(unsigned i=0;i<full.size();++i)
    {
        assert(std::abs(moved[i].x-full[i].x-12)<1.e-5f);
        assert(std::abs(moved[i].y-full[i].y-23)<1.e-5f);
        assert(std::abs(moved[i].z-full[i].z-34)<1.e-5f);
    }
}
