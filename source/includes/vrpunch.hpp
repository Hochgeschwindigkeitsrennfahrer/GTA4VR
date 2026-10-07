#pragma once
#include "vrmath.hpp"
#include <cstdint>

namespace VRPunch
{
    // Weapon ownership remains right-handed. Only the free off-hand may punch
    // with the supported standard pistol; an attached support hand is occupied.
    inline bool FreeHand(unsigned hand,uint32_t weapon,bool heldWeapon,bool supporting)
    {
        return hand<2 && !supporting && ((weapon==0 && !heldWeapon) || (weapon==7 && hand==0));
    }

    // Speed comes from the tracked controller in metres, not the game's moving
    // body/camera. Running with a stationary fist must not count as a punch.
    struct Strike
    {
        uint64_t at=0,quietAt=0,nextHit=0;
        VRMath::Vector tracked{},world{},begin{};
        float speed=0;
        bool armed=true;

        bool Sample(const VRMath::Vector& position,const VRMath::Vector& contact,
            uint64_t time,bool fist,float threshold)
        {
            if(time<=at) return false;
            const auto elapsed=time-at;
            begin=world;
            const float distance=VRMath::Length3(VRMath::Difference3(position,tracked));
            const float travel=VRMath::Length3(VRMath::Difference3(contact,world));
            speed=at && elapsed ? distance*1000.f/float(elapsed):0;
            const bool continuous=at && elapsed<=100 && std::isfinite(speed+travel);
            at=time;tracked=position;world=contact;
            if(!continuous) {armed=false;quietAt=0;speed=0;return false;}
            if(!fist) {armed=true;quietAt=0;return false;}
            if(speed<.7f)
            {
                if(!quietAt) quietAt=time;
                if(time-quietAt>=60) armed=true;
            }
            else quietAt=0;
            return armed && time>=nextHit && speed>=threshold;
        }

        void Contact() {armed=false;quietAt=0;nextHit=at+300;}
    };
}
