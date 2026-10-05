#pragma once
#include <array>
#include <cstring>

namespace VRConstantProbe
{
    // Observation only. State-block Apply and unhooked/internal device writes
    // can invalidate this shadow; equality is a candidate, never a skip gate.
    struct Observer
    {
        std::array<std::array<float,4>,256> values{};
        std::array<bool,256> known{};
        void Reset() {known.fill(false);}
        void Invalidate(unsigned index) {if(index<known.size()) known[index]=false;}
        bool Observe(unsigned start,const float* data,unsigned count)
        {
            if(!data || !count || start>=known.size() || count>known.size()-start) return false;
            bool same=true;
            for(unsigned i=0;i<count;++i)
            {
                same &= known[start+i] && std::memcmp(values[start+i].data(),data+i*4,16)==0;
                std::memcpy(values[start+i].data(),data+i*4,16);
                known[start+i]=true;
            }
            return same;
        }
    };
}
