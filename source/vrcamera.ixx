module;
#include <common.hxx>
#include "includes/vrmath.hpp"
#include <atomic>
#include <cstring>
export module vrcamera;
import common;
import comvars;
import openxrvr;

export namespace VRCamera
{
    void Install();
    bool Generate(const rage::grcViewport& base, const OpenXRVR::EyeView (&eyes)[2],
        rage::grcViewport (&output)[2]);
}

namespace
{
    using Setter=void (__thiscall*)(rage::grcViewport*,const void*);
    Setter setCamera=nullptr, setProjection=nullptr;
    std::atomic<bool> recenter{true};
    VRMath::Matrix referenceInverse{};
}

void VRCamera::Install()
{
    CIniReader ini("");
    if (!ini.ReadInteger("DEVELOPMENT","ProbeEyeViewport",0) &&
        !ini.ReadInteger("VR","ExperimentalStereo",0)) return;
    auto camera=hook::pattern("55 8B EC 83 E4 F0 83 EC 48 56 8B 75 08 57 8B 06 8B F9 56");
    auto projection=hook::pattern("56 FF 74 24 08 8B F1 8D 8E C0 01 00 00 C6 86 F0 02 00 00 01 E8");
    // Optional VR hooks fail closed on an unsupported executable.
    if (camera.size()!=1 || projection.size()!=1) return;
    setCamera=reinterpret_cast<Setter>(camera.get(0).get<void>());
    setProjection=reinterpret_cast<Setter>(projection.get(0).get<void>());
    FusionFix::onBeforeReset() += [](){recenter=true;};
}

bool VRCamera::Generate(const rage::grcViewport& base, const OpenXRVR::EyeView (&eyes)[2],
    rage::grcViewport (&output)[2])
{
    if (!setCamera || !setProjection || !base.mIsPerspective || base.mInvertZInProjectionMatrix)
        return false;
    try
    {
        if (recenter.exchange(false))
        {
            float center[3]{};
            for(unsigned i=0;i<3;++i) center[i]=(eyes[0].position[i]+eyes[1].position[i])*.5f;
            // Initial prototype uses the first eye orientation as the center
            // orientation. Both sampled SteamVR views had the same orientation.
            referenceInverse=VRMath::InverseRigid(VRMath::Pose(center,eyes[0].orientation));
        }
        VRMath::Matrix gameCamera{};
        std::memcpy(gameCamera.data(),base.mViewInverseMatrix,sizeof(gameCamera));
        for(const auto& row:gameCamera)
            for(float value:row)
                if(!std::isfinite(value)) throw std::invalid_argument("Invalid game camera");
        for(unsigned eye=0;eye<2;++eye)
        {
            alignas(16) VRMath::Matrix pose=VRMath::Multiply(VRMath::Multiply(
                VRMath::Pose(eyes[eye].position,eyes[eye].orientation),referenceInverse),gameCamera);
            alignas(16) VRMath::Matrix projection=VRMath::Projection(eyes[eye].fov,base.mNearClip,base.mFarClip);
            // Engine matrix helpers use aligned SSE loads/stores. The mirrored
            // viewport type has only four-byte alignment, so never pass an
            // arbitrary caller-owned copy to the engine setters.
            alignas(16) rage::grcViewport generated=base;
            const float vertical=std::tan(eyes[eye].fov[2])-std::tan(eyes[eye].fov[3]);
            const float horizontal=std::tan(eyes[eye].fov[1])-std::tan(eyes[eye].fov[0]);
            generated.mFov=(eyes[eye].fov[2]-eyes[eye].fov[3])*180.f/3.14159265358979323846f;
            generated.mAspect=horizontal/vertical;
            generated.field_2C8=vertical*.5f;
            generated.field_2CC=horizontal*.5f;
            // Verified engine setters update camera/view matrices, world-VP,
            // screen transform product and all six planes on this local copy.
            setCamera(&generated,pose.data());
            setProjection(&generated,projection.data());
            std::memcpy(&output[eye],&generated,sizeof(generated));
        }
        return true;
    }
    catch(const std::exception&)
    {
        recenter=true;
        return false;
    }
}
