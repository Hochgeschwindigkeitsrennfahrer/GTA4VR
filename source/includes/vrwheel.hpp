#pragma once
#include "vrmath.hpp"
#include <vector>
#include <cstdint>

namespace VRWheel
{
    struct Fit {VRMath::Matrix frame=VRMath::Identity();float radius=0;unsigned support=0;};
    // Recognize a circular rim near the authored driver's seat, rather than
    // assuming the seated animation's hand midpoint is the wheel centre.
    inline Fit FindRim(const std::vector<VRMath::Vector>& vertices,const VRMath::Vector& seat)
    {
        Fit best;
        for(int degrees=0;degrees<=40;degrees+=5)
        {
            const float t=degrees*.017453293f,s=std::sin(t),c=std::cos(t);
            std::vector<VRMath::Vector> points;
            for(const auto& p:vertices)
                if(std::abs(p[0]-seat[0])<.32f && p[1]-seat[1]>.1f && p[1]-seat[1]<.65f &&
                    p[2]-seat[2]>.2f && p[2]-seat[2]<.65f)
                    points.push_back({p[0],p[1]*s+p[2]*c,-p[1]*c+p[2]*s,1});
            if(points.size()<40) continue;
            uint32_t seed=1;
            auto sample=[&]() {seed=seed*1664525u+1013904223u;return points[seed%points.size()];};
            for(unsigned trial=0;trial<512;++trial)
            {
                const auto a=sample(),b=sample(),d=sample();
                const float determinant=2*(a[0]*(b[1]-d[1])+b[0]*(d[1]-a[1])+d[0]*(a[1]-b[1]));
                if(std::abs(determinant)<1.e-6f) continue;
                const float aa=a[0]*a[0]+a[1]*a[1],bb=b[0]*b[0]+b[1]*b[1],dd=d[0]*d[0]+d[1]*d[1];
                const float x=(aa*(b[1]-d[1])+bb*(d[1]-a[1])+dd*(a[1]-b[1]))/determinant;
                const float y=(aa*(d[0]-b[0])+bb*(a[0]-d[0])+dd*(b[0]-a[0]))/determinant;
                const float z=(a[2]+b[2]+d[2])/3,r=std::hypot(a[0]-x,a[1]-y);
                if(r<.12f || r>.24f || std::abs(x-seat[0])>.1f) continue;
                unsigned hits=0,bins=0;
                for(const auto& p:points)
                    if(std::abs(std::hypot(p[0]-x,p[1]-y)-r)<.015f && std::abs(p[2]-z)<.022f)
                    {++hits; bins|=1u<<(unsigned((std::atan2(p[1]-y,p[0]-x)+3.141592654f)*8/6.283185307f)%8);}
                unsigned coverage=0;for(unsigned b=0;b<8;++b) coverage+=(bins>>b)&1;
                if(coverage<7 || hits<40 || hits<=best.support) continue;
                best.frame=VRMath::Identity();
                best.frame[1]={0,s,c,0};best.frame[2]={0,-c,s,0};
                best.frame[3]={x,y*s-z*c,y*c+z*s,1};
                best.radius=r;best.support=hits;
            }
        }
        return best;
    }
    inline bool OnWheel(const VRMath::Vector& position,const Fit& fit)
    {
        const auto p=VRMath::Transform(position,VRMath::InverseRigid(fit.frame));
        return std::abs(p[2])<.055f && std::hypot(p[0],p[1])<fit.radius+.03f;
    }
}
