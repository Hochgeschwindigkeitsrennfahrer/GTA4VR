#pragma once
#include <bitset>
#include <cstdint>

namespace VRModelRefs
{
    // Tracks native holds already recorded in one queued lifetime list.
    // The caller owns synchronization and the actual retain/release calls.
    struct Batch
    {
        void* owner=nullptr;
        uint16_t* data=nullptr;
        unsigned count=0;
        std::bitset<65536> retained;

        bool NeedsHold(void* currentOwner,uint16_t* currentData,unsigned currentCount,unsigned model)
        {
            if(owner!=currentOwner || data!=currentData || !currentCount || currentCount<count)
            {owner=currentOwner; data=currentData; count=0; retained.reset();}
            // Preexisting native holds keep their original multiplicity.
            if(data) for(unsigned i=count;i<currentCount;++i) retained.set(data[i]);
            return !retained.test(model);
        }

        void Recorded(unsigned model,unsigned currentCount)
        {retained.set(model); count=currentCount;}
    };
}
