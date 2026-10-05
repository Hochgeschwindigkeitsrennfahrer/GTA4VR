#pragma once
#include <charconv>
#include <cmath>
#include <initializer_list>
#include <string_view>

namespace VRDevLocation
{
    struct Position {float x=0,y=0,z=0,heading=0;};
    inline bool Valid(const Position& value)
    {
        return std::isfinite(value.x) && std::isfinite(value.y) &&
            std::isfinite(value.z) && std::isfinite(value.heading) &&
            std::abs(value.x)<=8192 && std::abs(value.y)<=8192 &&
            value.z>=-1024 && value.z<=4096 && std::abs(value.heading)<=360;
    }
    inline bool Parse(std::string_view text,Position& result)
    {
        Position value;
        for(float* component:{&value.x,&value.y,&value.z,&value.heading})
        {
            if(text.empty() || text.front()==' ') return false;
            const auto parsed=std::from_chars(text.data(),text.data()+text.size(),*component);
            if(parsed.ec!=std::errc{}) return false;
            text.remove_prefix(parsed.ptr-text.data());
            if(component!=&value.heading)
            {
                if(text.empty() || text.front()!=' ') return false;
                text.remove_prefix(1);
            }
        }
        if(!text.empty() || !Valid(value)) return false;
        result=value; return true;
    }
}
