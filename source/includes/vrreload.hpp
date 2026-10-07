#pragma once
#include <algorithm>
#include <cstdint>
#include <cmath>
#include "vrmath.hpp"

// Ammo ownership is independent of rendering/native animation. The native
// clip combines chamber + magazine; reserve is total minus that clip.
namespace VRReload
{
    // Seat the palm on this pistol slide while retaining the initial grip's
    // orientation and nearby longitudinal point. Wrist is behind the palm.
    inline VRMath::Matrix SlideHandPose(const VRMath::Matrix& hand,const VRMath::Vector& rear)
    {
        auto pose=hand;
        VRMath::Vector palm=hand[3];
        for(unsigned a=0;a<3;++a) palm[a]+=hand[0][a]*.05f;
        palm[0]=std::clamp(palm[0],rear[0]-.015f,rear[0]+.16f);
        palm[1]=std::clamp(palm[1],rear[1]-.025f,rear[1]+.025f);
        palm[2]=rear[2]+.02f;
        for(unsigned a=0;a<3;++a) pose[3][a]=palm[a]-hand[0][a]*.05f;
        return pose;
    }
    // Capture at the well entrance, then require a separate axial push. The
    // snap itself never seats a magazine or changes ammunition ownership.
    struct InsertionGuide
    {
        static constexpr float Travel=.085f;
        bool active=false;
        float progress=0;
        void Begin() {active=true;progress=0;}
        bool Update(float axial,float lateral,float travel=Travel)
        {
            if(!active) return false;
            if(!std::isfinite(axial+lateral+travel) || travel<.02f || travel>.25f || axial<-.12f || lateral>.18f)
            {*this={};return false;}
            progress=std::clamp(axial,0.f,travel);
            return progress>=travel;
        }
    };
    struct Pistol
    {
        unsigned capacity=0,magazine=0,reserve=0,held=0,discarded=0;
        bool inserted=true,chamber=false,holding=false;
        void Begin(unsigned clip,unsigned total,unsigned clipCapacity)
        {
            *this={};capacity=clipCapacity;
            clip=std::min({clip,total,capacity});
            chamber=clip>0;magazine=clip-(chamber ? 1u:0u);reserve=total-clip;
            inserted=true;
        }
        unsigned Clip() const {return magazine+(chamber ? 1u:0u);}
        unsigned NativeTotal() const {return Clip()+reserve;}
        unsigned Owned() const {return NativeTotal()+held;}
        bool Ready() const {return chamber;}
        unsigned Eject()
        {
            if(!inserted) return 0;
            const auto lost=magazine;discarded+=lost;magazine=0;inserted=false;
            return lost;
        }
        bool TakeMagazine()
        {
            if(holding || !capacity || !reserve) return false;
            // Native clip capacity includes the chamber; retain that limit.
            held=std::min(reserve,capacity-(chamber ? 1u:0u));
            reserve-=held;holding=true;return true;
        }
        bool Insert()
        {
            if(inserted || !holding) return false;
            magazine=held;held=0;holding=false;inserted=true;return true;
        }
        void ReturnHeld()
        {reserve+=held;held=0;holding=false;}
        void DropHeld()
        {discarded+=held;held=0;holding=false;}
        void Rack()
        {
            if(chamber) ++discarded;
            chamber=inserted && magazine>0;
            if(chamber) --magazine;
        }
        bool Fired()
        {
            if(!Ready()) return false;
            chamber=inserted && magazine>0;
            if(chamber) --magazine;
            return true;
        }
    };
}
