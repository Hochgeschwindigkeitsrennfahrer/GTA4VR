#pragma once
#include "vrmath.hpp"

namespace VRVehiclePanels
{
    struct Panel {VRMath::Matrix frame=VRMath::Identity();float width=.16f,height=.16f;};
    inline VRMath::Vector MountOffset(const VRMath::Matrix& anchor,const VRMath::Vector& vehicleOrigin)
    {
        auto root=anchor;root[3]=vehicleOrigin;
        return VRMath::Transform(anchor[3],VRMath::InverseRigid(root));
    }
    inline VRMath::Matrix FixedMount(const VRMath::Matrix& basis,const VRMath::Vector& vehicleOrigin,const VRMath::Vector& offset)
    {
        auto root=basis;root[3]=vehicleOrigin;
        auto mount=root;mount[3]=VRMath::Transform(offset,root);return mount;
    }
    inline Panel Default(float right,float down,float forward,float tilt,float width,float height)
    {
        Panel p;p.width=width;p.height=height;
        const float a=tilt*.01745329252f;
        p.frame[1]={0,std::cos(a),-std::sin(a),0};
        p.frame[2]={0,std::sin(a),std::cos(a),0};
        p.frame[3]={right,-down,-forward,1};return p;
    }
    inline bool Valid(const Panel& p)
    {
        if(!std::isfinite(p.width) || !std::isfinite(p.height) || p.width<=0 || p.height<=0)return false;
        for(const auto& row:p.frame)for(float value:row)if(!std::isfinite(value))return false;
        return VRMath::Length3(p.frame[0])>0 && VRMath::Length3(p.frame[1])>0 && VRMath::Length3(p.frame[2])>0;
    }
    inline float Distance(const Panel& p,const VRMath::Vector& hand)
    {
        const auto point=VRMath::Transform(hand,VRMath::InverseRigid(p.frame));
        const float x=std::max(0.f,std::abs(point[0])-p.width*.5f),y=std::max(0.f,std::abs(point[1])-p.height*.5f);
        return std::sqrt(x*x+y*y+point[2]*point[2]);
    }
    inline VRMath::Matrix Drag(const VRMath::Matrix& panel,const VRMath::Matrix& acquiredHand,const VRMath::Matrix& hand)
    {return VRMath::Multiply(VRMath::Multiply(panel,VRMath::InverseRigid(acquiredHand)),hand);}
    // Absolute displacement from acquisition, never integrated frame deltas.
    inline float StationsDragged(const Panel& acquired,const VRMath::Vector& first,const VRMath::Vector& hand)
    {return -VRMath::Dot3(VRMath::Difference3(hand,first),acquired.frame[0])/(acquired.width/3.f);}
}
