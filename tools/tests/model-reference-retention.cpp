#include "../../source/includes/vrmodelrefs.hpp"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <vector>

static void Check(bool value,const char* message)
{
    if(!value) {std::fprintf(stderr,"Model retention failed: %s\n",message); std::exit(1);}
}

struct Queue
{
    VRModelRefs::Batch tracking;
    std::array<uint16_t,65535> ids{};
    unsigned count=0;
    std::array<int,65536>* refs;

    void NativeHold(unsigned model)
    {Check(count<ids.size(),"queue overflow"); ++(*refs)[model]; ids[count++]=uint16_t(model);}
    void StereoHold(unsigned model)
    {
        if(tracking.NeedsHold(this,ids.data(),count,model)) NativeHold(model);
        tracking.Recorded(model,count);
    }
    void Release()
    {
        for(unsigned i=0;i<count;++i) Check(--(*refs)[ids[i]]>=0,"release without a hold");
        count=0; // native release consumer resets the list for reuse
    }
};

int main()
{
    auto refs=std::vector<std::array<int,65536>>(1);
    auto queues=std::vector<Queue>(4);
    for(auto& queue:queues) queue.refs=&refs[0];
    for(unsigned cycle=0;cycle<12;++cycle)
    {
        auto& queue=queues[cycle%4];
        // Stereo/shadow passes can make tens of thousands of repeated calls.
        for(unsigned draw=0;draw<24000;++draw) queue.StereoHold(1900+draw%48);
        Check(queue.count==48,"repeated draws multiply queued holds");
        for(unsigned model=1900;model<1948;++model) Check(refs[0][model]==1,"model must remain alive until queue release");
        queue.Release();
        for(auto ref:refs[0]) Check(ref==0,"a recycled queue leaks references");
    }
    auto& queue=queues[0];
    queue.NativeHold(21033); queue.NativeHold(21033); queue.NativeHold(1910);
    queue.StereoHold(21033); queue.StereoHold(1910); queue.StereoHold(1911);
    Check(queue.count==4 && refs[0][21033]==2,"pre-stereo multiplicity changed");
    queue.Release();
    for(auto ref:refs[0]) Check(ref==0,"pre-stereo release balance");
    // Fallback temporarily bypasses tracking, then stereo seeds those holds.
    queue.tracking.owner=nullptr;
    queue.NativeHold(21100); queue.StereoHold(21100); queue.StereoHold(1910);
    Check(queue.count==2,"fallback holds were not observed on resume");
    queue.Release();
    for(auto ref:refs[0]) Check(ref==0,"fallback release balance");
    std::puts("Model reference retention checks passed");
}
