#pragma once
#include <cstdint>
#include <cstring>

namespace VRVertex
{
    // CE immediate records contain nine raw words, including packed color.
    // Copy bytes so NaNs, signed zero and integer fields retain their bits.
    inline void Emit(std::uint32_t enabled,std::uint32_t*& output,
        std::uint32_t& count,const void* arguments)
    {
        ++count;
        if(!enabled || !output) return;
        auto* destination=output;
        output+=9;
        std::memcpy(destination,arguments,36);
    }
}
