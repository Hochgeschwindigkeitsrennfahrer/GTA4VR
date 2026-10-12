#pragma once
#include "vrmath.hpp"
#include <vector>
#include <string>

namespace VRAmmoCounter
{
    struct Settings
    {
        bool enabled=true;
        bool stacked=false,editWithOffHand=false;
        float grabRadius=.10f;
        float offset[3]{-.115f,0,.080f},rotation[3]{};
        float width=.026f,height=.024f;
    };
    struct Snapshot
    {
        VRMath::Matrix gun{};
        Settings settings{};
        unsigned rounds=0,capacity=0,total=0;
        bool valid=false;
    };
    struct Vertex {float x,y,z;unsigned color;};
    inline VRMath::Matrix Frame(const Settings& s)
    {
        auto frame=VRMath::Identity();
        for(unsigned a=0;a<3;++a)
        {
            float p[3]{},q[4]{};const float half=s.rotation[a]*.00872664626f;
            q[a]=std::sin(half);q[3]=std::cos(half);
            frame=VRMath::Multiply(frame,VRMath::Pose(p,q));
        }
        frame[3]={s.offset[0],s.offset[1],s.offset[2],1};return frame;
    }
    inline void SetFrame(Settings& s,const VRMath::Matrix& m)
    {
        for(unsigned a=0;a<3;++a) s.offset[a]=m[3][a];
        const float y=std::asin(std::clamp(-m[0][2],-1.f,1.f));
        float x=0,z=0;
        if(std::abs(std::cos(y))>1.e-5f) {x=std::atan2(m[1][2],m[2][2]);z=std::atan2(m[0][1],m[0][0]);}
        else x=std::atan2(-m[2][1],m[1][1]);
        s.rotation[0]=x*57.2957795f;s.rotation[1]=y*57.2957795f;s.rotation[2]=z*57.2957795f;
    }
    inline bool Near(const Settings& s,const VRMath::Matrix& gun,const VRMath::Vector& hand)
    {
        const auto p=VRMath::Transform(hand,VRMath::InverseRigid(VRMath::Multiply(Frame(s),gun)));
        const float y=std::max(0.f,std::abs(p[1])-s.width*.5f),z=std::max(0.f,std::abs(p[2])-s.height*.5f);
        return p[0]*p[0]+y*y+z*z<s.grabRadius*s.grabRadius;
    }
    inline std::vector<unsigned> DigitMasks(unsigned total)
    {
        constexpr unsigned masks[]{0x3f,0x06,0x5b,0x4f,0x66,0x6d,0x7d,0x07,0x7f,0x6f};
        const auto text=std::to_string(total);
        std::vector<unsigned> result;
        if(text.size()==1) result.push_back(0);
        for(char digit:text) result.push_back(masks[digit-'0']);
        return result;
    }
    inline std::vector<Vertex> Geometry(const Snapshot& snapshot)
    {
        const auto& s=snapshot.settings;
        const auto frame=VRMath::Multiply(Frame(s),snapshot.gun);
        std::vector<Vertex> vertices;vertices.reserve(600);
        auto point=[&](float x,float y,float depth,unsigned color)
        {
            const auto p=VRMath::Transform({-depth,-(x-.5f)*s.width,(y-.5f)*s.height,1},frame);
            vertices.push_back({p[0],p[1],p[2],color});
        };
        auto rect=[&](float x,float y,float w,float h,float depth,unsigned color)
        {
            // Looking from the rear: screen right is weapon -Y, up is +Z.
            const float corners[6][2]{{x,y},{x+w,y},{x,y+h},{x,y+h},{x+w,y},{x+w,y+h}};
            for(const auto& c:corners)
            {
                point(c[0],c[1],depth,color);
            }
        };
        rect(0,0,1,1,0,0xff101216); // recessed backing
        const unsigned pins=std::min(snapshot.capacity,64u),columns=(pins+1)/2;
        for(unsigned i=0;i<pins;++i)
            rect(s.stacked ? .07f+.86f*(i%columns)/columns:.08f,
                s.stacked ? .10f+.14f*(i/columns):.10f+.80f*i/pins,
                s.stacked ? .65f/columns:.18f,s.stacked ? .085f:.62f/pins,.0004f,
                i<snapshot.rounds ? 0xffffcf62:0xff382d1b);
        const auto digits=DigitMasks(snapshot.total);
        for(unsigned digit=0;digit<digits.size();++digit)
        {
            const unsigned mask=digits[digit];
            const float cell=(s.stacked ? .80f:.60f)/static_cast<float>(digits.size());
            const float x=(s.stacked ? .10f:.37f)+cell*digit,w=cell*.77f,t=std::min(w*.16f,.035f);
            const float y=s.stacked ? .47f:.14f,h=s.stacked ? .43f:.72f;
            const float segments[7][4]{{x+t,y+h-t,w-2*t,t},{x+w-t,y+h/2,t,h/2-t},
                {x+w-t,y+t,t,h/2-t},{x+t,y,w-2*t,t},{x,y+t,t,h/2-t},
                {x,y+h/2,t,h/2-t},{x+t,y+h/2-t/2,w-2*t,t}};
            for(unsigned segment=0;segment<7;++segment)
            {
                const float sx=segments[segment][0],sy=segments[segment][1];
                const float sw=segments[segment][2],sh=segments[segment][3];
                // Six-point bevel: each end tapers to a centered tip.
                const bool horizontal=segment==0 || segment==3 || segment==6;
                const float polygon[6][2]{
                    {sx+(horizontal ? 0:sw/2),sy+(horizontal ? sh/2:0)},
                    {sx+(horizontal ? sh/2:sw),sy+(horizontal ? 0:sw/2)},
                    {sx+(horizontal ? sw-sh/2:sw),sy+(horizontal ? 0:sh-sw/2)},
                    {sx+(horizontal ? sw:sw/2),sy+(horizontal ? sh/2:sh)},
                    {sx+(horizontal ? sw-sh/2:0),sy+(horizontal ? sh:sh-sw/2)},
                    {sx+(horizontal ? sh/2:0),sy+(horizontal ? sh:sw/2)}};
                const unsigned color=(mask&(1u<<segment)) ? 0xffffcf62:0xff30291d;
                for(unsigned i=1;i<5;++i)
                {
                    point(polygon[0][0],polygon[0][1],.0004f,color);
                    point(polygon[i][0],polygon[i][1],.0004f,color);
                    point(polygon[i+1][0],polygon[i+1][1],.0004f,color);
                }
            }
        }
        return vertices;
    }
}
