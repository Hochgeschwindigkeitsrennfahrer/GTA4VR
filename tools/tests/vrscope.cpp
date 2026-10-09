#include "../../source/includes/vrscope.hpp"
#include <cassert>
#include <limits>

int main()
{
    const VRScope::Settings settings;
    const auto gun=VRMath::Identity();
    const auto camera=VRScope::Camera(gun,settings);
    assert(camera[0][1]==-1 && camera[1][2]==1 && camera[2][0]==-1);
    auto eye=settings.eyepiece;eye[0]-=.10f;
    assert(VRScope::InEyeBox(camera,eye,settings));
    eye[0]+=.20f;assert(!VRScope::InEyeBox(camera,eye,settings));
    eye=settings.eyepiece;eye[0]-=.10f;eye[1]+=.20f;
    assert(!VRScope::InEyeBox(camera,eye,settings));
    const auto depth=VRScope::DepthParameters(.05f,1500.f);
    assert(std::abs(depth[0]-20.f)<1.e-5f);
    assert(std::abs(depth[1]-.0672373995f)<1.e-7f);
    assert(VRScope::EncodeDepth(.05f,depth)==0.f);
    assert(std::abs(VRScope::EncodeDepth(1500.f,depth)-1.f)<1.e-6f);
    assert(std::abs(VRScope::EncodeDepth(std::sqrt(.05f*1500.f),depth)-.5f)<1.e-6f);
    // Recorded disappearing-lens view: logarithmic scene depth is BEHIND
    // the lens, although its value is less than perspective NDC 0.03307738.
    assert(VRScope::EncodeDepth(.0516662598f,depth)<.0251852889f);
    assert(VRScope::EncodeDepth(.0516662598f,depth)<.0253715534f);
    assert(VRScope::EncodeDepth(.25f,depth)<VRScope::EncodeDepth(.5f,depth));
    // The same eligibility must survive arbitrary weapon/world rotation.
    const float p[]{17,29,3},q[]{0,0,.70710678f,.70710678f};
    const auto moved=VRMath::Pose(p,q);
    eye=settings.eyepiece;eye[0]-=.10f;
    assert(VRScope::InEyeBox(VRScope::Camera(moved,settings),VRMath::Transform(eye,moved),settings));
    eye[0]=std::numeric_limits<float>::quiet_NaN();
    assert(!VRScope::InEyeBox(camera,eye,settings));
}
