#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace VRResolution
{
    struct Size {unsigned width=0,height=0;};

    inline Size Recommended(Size recommendation,Size maximum,float scale)
    {
        if(!recommendation.width || !recommendation.height || !maximum.width || !maximum.height ||
            !std::isfinite(scale) || scale<=0) throw std::invalid_argument("invalid eye resolution");
        // Runtime/device limits are allocation requirements, not tuning limits.
        const double effective=std::min({double(scale),double(maximum.width)/recommendation.width,
            double(maximum.height)/recommendation.height});
        return {std::max(1u,unsigned(std::floor(recommendation.width*effective))),
            std::max(1u,unsigned(std::floor(recommendation.height*effective)))};
    }

    inline unsigned Relative(unsigned value,unsigned native,unsigned eye)
    {
        if(!native || !eye) throw std::invalid_argument("invalid render target dimensions");
        return std::max(1u,unsigned((uint64_t(value)*eye+native/2)/native));
    }

    inline float Coordinate(float value,unsigned native,unsigned eye)
    {return value*float(eye)/native;}

    // D3D9 pretransformed vertices address pixel centers at integer - 0.5.
    inline float PixelCenter(float value,unsigned native,unsigned eye)
    {return Coordinate(value+.5f,native,eye)-.5f;}

    // Scissors address pixel boundaries, not centers. Round outwards so a
    // projected object cannot lose an edge when its native bounds are resized.
    inline int Boundary(int value,unsigned from,unsigned to,bool upper)
    {
        if(!from || !to) throw std::invalid_argument("invalid render target dimensions");
        const double scaled=double(value)*to/from;
        return int(upper ? std::ceil(scaled):std::floor(scaled));
    }

    inline unsigned MipLevels(Size size,unsigned requested)
    {
        unsigned maximum=1;
        for(unsigned extent=std::max(size.width,size.height);extent>1;extent>>=1) ++maximum;
        return std::min(requested,maximum);
    }

    inline bool ScreenSize(const float* input,Size from,Size to,float* output)
    {
        if(!from.width || !from.height || !to.width || !to.height) return false;
        // Native post-processing texel offsets put inverse dimensions in xy.
        // Preserve this encoding; the deferred-lighting encoding uses zw.
        if(input[2]==0.f && input[3]==0.f && std::isfinite(input[0]) && std::isfinite(input[1]) &&
            std::abs(input[0]-1.f/from.width)<=1e-7f && std::abs(input[1]-1.f/from.height)<=1e-7f)
        {output[0]=1.f/to.width;output[1]=1.f/to.height;output[2]=output[3]=0.f;return true;}
        const bool dimensions=input[0]==float(from.width) && input[1]==float(from.height);
        const bool origin=input[0]==0.f && input[1]==0.f;
        if((!dimensions && !origin) || !from.width || !from.height || !to.width || !to.height ||
            std::abs(input[2]-1.f/from.width)>1e-7f || std::abs(input[3]-1.f/from.height)>1e-7f ||
            !std::isfinite(input[2]) || !std::isfinite(input[3])) return false;
        output[0]=dimensions ? float(to.width):0.f;output[1]=dimensions ? float(to.height):0.f;
        output[2]=1.f/to.width;output[3]=1.f/to.height;
        return true;
    }
}
