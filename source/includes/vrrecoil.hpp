#pragma once
#include "vrmath.hpp"
#include <cstdint>

namespace VRRecoil
{
    struct Kick
    {
        float degrees=0;
        uint64_t at=0;
        float Sample(uint64_t now,float recovery)
        {
            if(!at || now<at || !std::isfinite(recovery) || recovery<=0) {degrees=0;at=now;return 0;}
            degrees*=std::exp(-static_cast<float>(now-at)*.001f/recovery);
            at=now;return degrees;
        }
        void Shot(uint64_t now,float amount,float recovery,float maximum)
        {
            Sample(now,recovery);
            if(std::isfinite(amount+maximum) && amount>=0 && maximum>=0)
                degrees=std::clamp(degrees+amount,0.f,maximum);
        }
    };
    // Raise the tracked wrist and its orientation as one rigid motion about
    // a virtual forearm pivot behind it. Native IK moves the rest of the arm;
    // the attached weapon and actual muzzle inherit the wrist rotation.
    inline VRMath::Matrix Pose(const VRMath::Matrix& wrist,const VRMath::Matrix& gun,float degrees)
    {
        if(!std::isfinite(degrees) || degrees<=0) return wrist;
        const float length=VRMath::Length3(gun[1]);
        if(!std::isfinite(length) || length<.5f || length>1.5f) return wrist;
        const float half=degrees*.00872664626f;
        float position[3]{},q[4]{};
        for(unsigned a=0;a<3;++a) q[a]=-gun[1][a]*std::sin(half)/length;
        q[3]=std::cos(half);
        const auto rotation=VRMath::Pose(position,q);
        auto local=wrist;VRMath::Vector pivot=wrist[3];
        for(unsigned a=0;a<3;++a) {pivot[a]-=gun[0][a]*.35f;local[3][a]-=pivot[a];}
        auto result=VRMath::Multiply(local,rotation);
        for(unsigned a=0;a<3;++a) result[3][a]+=pivot[a];
        return result;
    }
}
