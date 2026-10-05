#pragma once
#include <array>
#include <bit>
#include <cstring>

namespace VRConstantBatch
{
    // Register writes remain confined to one verified native parameter binder.
    // Adjacent writes are combined; gaps are never filled from stale state.
    class Buffer
    {
        std::array<std::array<float,4>,256> values{};
        std::array<unsigned long long,4> dirty{};
        std::array<unsigned long long,4> written{};
    public:
        unsigned inputCalls=0,outputCalls=0;
        bool Write(unsigned first,const float* data,unsigned count)
        {
            if(!data || !count || first>=256 || count>256-first)return false;
            std::memcpy(values[first].data(),data,count*16);
            for(unsigned i=first;i<first+count;++i)
            {dirty[i/64]|=1ull<<(i%64);written[i/64]|=1ull<<(i%64);}
            ++inputCalls;return true;
        }
        template<class Send>void Flush(Send&& send)
        {
            for(unsigned word=0;word<4;++word)
                while(dirty[word])
                {
                    const unsigned first=word*64+std::countr_zero(dirty[word]);
                    unsigned end=first;
                    while(end<256 && (dirty[end/64]&(1ull<<(end%64))))
                    {dirty[end/64]&=~(1ull<<(end%64));++end;}
                    send(first,values[first].data(),end-first);++outputCalls;
                }
        }
        bool Validate(const float* registers) const
        {
            for(unsigned word=0;word<4;++word)
            {
                auto bits=written[word];
                while(bits)
                {
                    const unsigned reg=word*64+std::countr_zero(bits);bits&=bits-1;
                    if(std::memcmp(values[reg].data(),registers+reg*4,16))return false;
                }
            }
            return true;
        }
        void Reset(){dirty={};written={};inputCalls=outputCalls=0;}
    };
}
