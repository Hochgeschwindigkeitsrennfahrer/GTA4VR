#pragma once
#include "vrmath.hpp"

namespace VRScope
{
    struct Settings
    {
        VRMath::Vector eyepiece{-.16f,0,.125f,1};
        float radius=.022f,eyeRelief=.30f,eyeBox=.10f,verticalFov=8.f;
        float surfaceOffset=.001f;
        bool enabled=true;
        bool depthTest=true,debugLens=false;
    };

    // Same encoding as shaders.ixx PS c209 and FusionShaders LogDepth Write.
    // Perspective NDC is still used for clipping; fragment depth uses this.
    inline VRMath::Vector DepthParameters(float nearClip,float farClip)
    {
        if(!(nearClip>0 && farClip>nearClip) || !std::isfinite(nearClip+farClip))
            throw std::invalid_argument("Invalid scope depth clip planes");
        return {1.f/nearClip,1.f/std::log2(farClip/nearClip),farClip/nearClip,nearClip};
    }

    inline float EncodeDepth(float viewZ,const VRMath::Vector& parameters)
    {
        return std::clamp(std::log2(viewZ*parameters[0])*parameters[1],0.f,1.f);
    }

    // Weapon +X is muzzle-forward, +Z is up. A right-handed camera uses
    // +Z backward, therefore its right axis is weapon -Y.
    inline VRMath::Matrix Camera(const VRMath::Matrix& gun,const Settings& settings)
    {
        auto camera=VRMath::Identity();
        for(unsigned a=0;a<3;++a)
        {
            camera[0][a]=-gun[1][a];camera[1][a]=gun[2][a];camera[2][a]=-gun[0][a];
        }
        camera[3]=VRMath::Transform(settings.eyepiece,gun);
        return camera;
    }

    // Render along the actual firing ray, while the physical lens stays at
    // Camera(gun, settings). Preserve its longitudinal station, eliminating
    // transverse sight/bore parallax without tilting or redirecting bullets.
    inline VRMath::Matrix SightCamera(const VRMath::Matrix& lens,const VRMath::Vector& muzzle)
    {
        auto camera=lens;
        float axial=0;
        for(unsigned a=0;a<3;++a) axial+=(lens[3][a]-muzzle[a])*lens[2][a];
        for(unsigned a=0;a<3;++a) camera[3][a]=muzzle[a]+lens[2][a]*axial;
        return camera;
    }

    inline bool InEyeBox(const VRMath::Matrix& camera,const VRMath::Vector& eye,const Settings& settings)
    {
        const auto local=VRMath::Transform(eye,VRMath::InverseRigid(camera));
        return std::isfinite(local[0]+local[1]+local[2]) && local[2]>.015f &&
            local[2]<settings.eyeRelief && std::hypot(local[0],local[1])<settings.eyeBox;
    }
}
