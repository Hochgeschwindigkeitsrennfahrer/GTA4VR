#pragma once
#include <algorithm>
#include <cstdint>
#include <cmath>
#include "vrmath.hpp"

// Ammo ownership is independent of rendering/native animation. The native
// clip combines chamber + magazine; reserve is total minus that clip.
namespace VRReload
{
    // Rotate around the configured shell tip, keeping it seated at the port.
    // Orientation and insertion travel are independently configurable.
    inline VRMath::Matrix ShellInsertionPose(const VRMath::Matrix& gun,
        const VRMath::Matrix& rotation,const VRMath::Vector& tip,
        const VRMath::Vector& port,const VRMath::Vector& axis,float progress)
    {
        auto root=VRMath::Multiply(rotation,gun);
        const auto worldTip=VRMath::Transform(tip,root);
        const auto worldPort=VRMath::Transform(port,gun);
        const auto worldAxis=VRMath::Transform(axis,gun);
        for(unsigned a=0;a<3;++a)
            root[3][a]+=worldPort[a]+worldAxis[a]*progress-worldTip[a];
        return root;
    }
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
            if(!std::isfinite(axial+lateral+travel) || travel<=0.f || axial<-.12f || lateral>.18f)
            {*this={};return false;}
            progress=std::clamp(axial,0.f,travel);
            return progress>=travel;
        }
    };
    struct Pistol
    {
        unsigned capacity=0,magazine=0,reserve=0,held=0,discarded=0;
        bool inserted=true,chamber=false,holding=false,tube=false,pump=false;
        void Begin(unsigned clip,unsigned total,unsigned clipCapacity)
        {
            *this={};capacity=clipCapacity;
            clip=std::min({clip,total,capacity});
            chamber=clip>0;magazine=clip-(chamber ? 1u:0u);reserve=total-clip;
            inserted=capacity==1 ? chamber:true;
        }
        unsigned Clip() const {return magazine+(chamber ? 1u:0u);}
        unsigned NativeTotal() const {return Clip()+reserve;}
        unsigned Owned() const {return NativeTotal()+held;}
        bool Resume(unsigned total,unsigned clipCapacity)
        {
            // Selection can recreate/refill the native clip without spending
            // total ammo. Our saved chamber/magazine remain authoritative.
            if(capacity!=clipCapacity || total<Clip() || holding) return false;
            reserve=total-Clip();return true;
        }
        bool Ready() const {return chamber;}
        unsigned Eject()
        {
            if(tube || !inserted) return 0;
            if(capacity==1)
            {const unsigned lost=chamber ? 1u:0u;discarded+=lost;chamber=false;inserted=false;return lost;}
            const auto lost=magazine;discarded+=lost;magazine=0;inserted=false;
            return lost;
        }
        bool TakeMagazine()
        {
            if(holding || !capacity || !reserve || (capacity==1 && chamber) || (tube && Clip()>=capacity)) return false;
            // Native clip capacity includes the chamber; retain that limit.
            held=tube ? 1u:std::min(reserve,capacity-(chamber ? 1u:0u));
            reserve-=held;holding=true;return true;
        }
        bool Insert()
        {
            if(!holding) return false;
            if(tube)
            {if(Clip()>=capacity || held!=1) return false; ++magazine;held=0;holding=false;inserted=true;return true;}
            if(inserted) return false;
            if(capacity==1)
            {chamber=held==1;held=0;holding=false;inserted=chamber;return chamber;}
            magazine=held;held=0;holding=false;inserted=true;return true;
        }
        void ReturnHeld()
        {reserve+=held;held=0;holding=false;}
        void DropHeld()
        {discarded+=held;held=0;holding=false;}
        void Rack()
        {
            if(capacity==1) return; // A loaded rocket has no separate chamber operation.
            if(chamber) ++discarded;
            chamber=inserted && magazine>0;
            if(chamber) --magazine;
        }
        bool Fired()
        {
            if(!Ready()) return false;
            if(capacity==1) {chamber=false;inserted=false;return true;}
            if(pump) {chamber=false;return true;}
            chamber=inserted && magazine>0;
            if(chamber) --magazine;
            return true;
        }
    };
}
