#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace VRTextureRouting
{
    // Owned by one immutable eye-resource set; accessed on its render thread.
    // Cache positive and negative routes. No COM references or device state.
    template<typename Pointer,std::size_t Capacity=64> class Cache
    {
        static_assert(Capacity && (Capacity&(Capacity-1))==0);
        struct Entry {Pointer input{}; std::array<Pointer,3> routes{};};
        std::array<Entry,Capacity> entries{};
    public:
        template<typename Resolve> Pointer Map(Pointer input,int eye,Resolve&& resolve)
        {
            if(!input || eye>1) return input;
            auto& entry=entries[(reinterpret_cast<std::uintptr_t>(input)>>4)&(Capacity-1)];
            if(entry.input!=input) {entry.routes=resolve(input); entry.input=input;}
            return entry.routes[eye<0 ? 0 : eye+1];
        }
    };
}
