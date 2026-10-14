#include "../../source/includes/vrvehiclepanels.hpp"
#include <cassert>
int main()
{
    using namespace VRVehiclePanels;
    auto panel=Default(.45f,.32f,.55f,12,.36f,.09f);
    assert(Valid(panel));
    assert(Distance(panel,panel.frame[3])<1.e-5f);
    auto first=VRMath::Identity();first[3]=panel.frame[3];
    const float position[]{1,2,3},quaternion[]{0,0,.70710678f,.70710678f};
    auto hand=VRMath::Pose(position,quaternion);
    auto moved=Drag(panel.frame,first,hand);
    auto restored=Drag(moved,hand,first);
    for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c)assert(std::abs(restored[r][c]-panel.frame[r][c])<1.e-5f);
    auto point=first[3];point[0]-=panel.width/3;
    assert(std::abs(StationsDragged(panel,first[3],point)-1)<1.e-5f);
    assert(StationsDragged(panel,first[3],first[3])==0);
    // Same local interaction after the vehicle translates and turns.
    auto inverse=VRMath::InverseRigid(hand);
    auto world=VRMath::Multiply(first,hand);
    auto local=VRMath::Multiply(world,inverse);
    assert(std::abs(StationsDragged(panel,first[3],local[3]))<1.e-5f);
    // Recording the legacy mount preserves the layout, then entry-side head
    // offsets cannot affect either rendering or grip coordinates.
    auto legacy=hand;legacy[3]={4,5,6,1};
    const VRMath::Vector origin{1,2,3,1};
    const auto offset=MountOffset(legacy,origin);
    auto fixed=FixedMount(legacy,origin,offset);
    for(unsigned axis=0;axis<3;++axis)assert(std::abs(fixed[3][axis]-legacy[3][axis])<1.e-5f);
    auto oppositeEntry=legacy;oppositeEntry[3]={-9,8,7,1};
    auto same=FixedMount(oppositeEntry,origin,offset);
    for(unsigned row=0;row<4;++row)for(unsigned axis=0;axis<4;++axis)assert(std::abs(same[row][axis]-fixed[row][axis])<1.e-5f);
    auto translatedOrigin=origin;translatedOrigin[0]+=20;
    auto travelling=FixedMount(oppositeEntry,translatedOrigin,offset);
    assert(std::abs(travelling[3][0]-fixed[3][0]-20)<1.e-5f);
    panel.frame[3][0]=10000;assert(Valid(panel)); // No guessed tuning limits.
    panel.width=0;assert(!Valid(panel));
}
