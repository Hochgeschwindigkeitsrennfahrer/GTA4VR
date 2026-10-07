#pragma once
#include "vrmath.hpp"
#include <cstdint>

namespace VRMelee
{
    constexpr unsigned SampleCount=24;
    using Points=std::array<VRMath::Vector,SampleCount>;
    struct Profile {uint32_t type; VRMath::Vector base,tip; float radius; unsigned samples=5;};
    // CE w_knife cutting section, observed in model space. Other weapons need
    // their own authored geometry and damage tuning, not a larger fist sphere.
    constexpr Profile Knife{3,{.065169f,.012082f,.06f,1},{.065169f,.012082f,.247477f,1},.035f};
    constexpr bool Supported(uint32_t type) {return type>=1 && type<=3;}
    // The stock clubs are elongated along model Z. Cover their real loaded
    // drawable bounds, excluding the first 10cm of handle around the grip.
    // Reject unknown/non-club shapes rather than applying an arbitrary reach.
    inline bool ClubProfile(uint32_t type,const VRMath::Vector& low,const VRMath::Vector& high,Profile& out)
    {
        if(type!=1 && type!=2) return false;
        for(unsigned axis=0;axis<3;++axis)
            if(!std::isfinite(low[axis]+high[axis]) || high[axis]<=low[axis]) return false;
        const float length=high[2]-low[2];
        const float width=std::max(high[0]-low[0],high[1]-low[1]);
        if(length<.3f || length>2.f || width>.25f || length<width*2.f) return false;
        const float end=std::abs(high[2])>=std::abs(low[2]) ? high[2]:low[2];
        const float start=end>0 ? .10f:-.10f;
        const float radius=type==1 ? .055f:.035f;
        const unsigned count=unsigned(std::ceil(std::abs(end-start)/(radius*1.75f)))+1;
        if(count<2 || count>SampleCount) return false;
        out={type,{(low[0]+high[0])*.5f,(low[1]+high[1])*.5f,start,1},
            {(low[0]+high[0])*.5f,(low[1]+high[1])*.5f,end,1},radius,count};
        return true;
    }
    inline Points SampleBlade(const Profile& profile,const VRMath::Matrix& frame)
    {
        Points result{};
        for(unsigned i=0;i<profile.samples;++i)
        {
            auto point=profile.base;
            for(unsigned axis=0;axis<3;++axis)
                point[axis]+=(profile.tip[axis]-profile.base[axis])*float(i)/float(profile.samples-1);
            result[i]=VRMath::Transform(point,frame);
        }
        return result;
    }
    struct Strike
    {
        enum class Gate { Duplicate, Discontinuity, Slow, Rearm, Cooldown, Ready };
        uint64_t at=0,quietAt=0,nextHit=0;
        Points tracked{},world{},begin{},motion{};
        VRMath::Vector contactPoint{},contactDirection{};
        unsigned contactSample=0;
        unsigned samples=5;
        float speed=0;
        bool armed=false;
        Gate gate=Gate::Discontinuity;
        bool Sample(const Points& positions,const Points& contacts,uint64_t time,float threshold,unsigned count=5)
        {
            if(count<2 || count>SampleCount) {gate=Gate::Discontinuity;armed=false;return false;}
            if(time<=at) {gate=Gate::Duplicate;return false;}
            const auto elapsed=time-at;
            begin=world;speed=0;
            bool continuous=at && elapsed<=100 && samples==count;
            samples=count;
            for(unsigned i=0;i<samples;++i)
            {
                motion[i]=VRMath::Difference3(positions[i],tracked[i]);
                const float velocity=VRMath::Length3(motion[i])*1000.f/float(elapsed);
                const float travel=VRMath::Length3(VRMath::Difference3(contacts[i],world[i]));
                // Valid tracked motion has no upper speed or travel limit.
                // Pose ownership/tracking validity are checked upstream. Never
                // infer tracking loss from a fast swing. The minimum attack
                // speed remains a separate gate.
                continuous &= std::isfinite(velocity+travel);
                speed=std::max(speed,velocity);
            }
            at=time;tracked=positions;world=contacts;
            if(!continuous) {armed=false;quietAt=0;nextHit=0;speed=0;gate=Gate::Discontinuity;return false;}
            if(speed<.7f)
            {
                if(!quietAt) quietAt=time;
                if(time-quietAt>=20) armed=true;
            }
            else quietAt=0;
            // A new slash/stab can start without parking the whole blade. Use
            // tracking-space withdrawal along the impacting sample's motion,
            // so locomotion and continued penetration cannot rearm a stroke.
            if(!armed && nextHit && VRMath::Dot3(VRMath::Difference3(tracked[contactSample],contactPoint),contactDirection)<=-.06f)
                armed=true;
            gate=speed<threshold ? Gate::Slow:(!armed ? Gate::Rearm:(time<nextHit ? Gate::Cooldown:Gate::Ready));
            return gate==Gate::Ready;
        }
        void Contact()
        {
            contactSample=0;
            for(unsigned i=1;i<samples;++i)
                if(VRMath::Length3(motion[i])>VRMath::Length3(motion[contactSample])) contactSample=i;
            contactPoint=tracked[contactSample];contactDirection=motion[contactSample];
            const float length=VRMath::Length3(contactDirection);
            for(unsigned axis=0;axis<3;++axis) contactDirection[axis]=length>0 ? contactDirection[axis]/length:0;
            armed=false;quietAt=0;nextHit=at+180;
        }
    };
}
