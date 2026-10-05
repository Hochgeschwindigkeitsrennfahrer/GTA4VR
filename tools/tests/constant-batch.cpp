#include "../../source/includes/vrconstantbatch.hpp"
#include <array>
#include <cassert>
#include <cstdio>
#include <vector>

int main()
{
    VRConstantBatch::Buffer batch;std::array<std::array<float,4>,256> expected{},actual{};
    struct Range{unsigned first,count;};std::vector<Range> emitted;
    const auto write=[&](unsigned first,unsigned count,float value)
    {
        std::vector<float> data(count*4,value);
        assert(batch.Write(first,data.data(),count));
        for(unsigned i=first;i<first+count;++i)expected[i].fill(value);
    };
    const auto flush=[&]()
    {batch.Flush([&](unsigned first,const float* data,unsigned count)
        {emitted.push_back({first,count});std::memcpy(actual[first].data(),data,count*16);});};
    // Overlaps keep the last write, adjacency spans mask words, holes stay untouched.
    write(0,4,1);write(2,4,2);write(62,4,3);write(64,4,4);write(255,1,5);
    assert(!batch.Write(255,expected[0].data(),2));
    flush();assert(actual==expected);assert(emitted.size()==3);
    assert(batch.Validate(actual[0].data()));actual[0][0]=9;
    assert(!batch.Validate(actual[0].data()));actual=expected;
    assert(emitted[0].first==0 && emitted[0].count==6);
    assert(emitted[1].first==62 && emitted[1].count==6);
    assert(emitted[2].first==255 && emitted[2].count==1);
    // A boundary flush exposes the earlier value before a later overwrite.
    emitted.clear();write(8,1,6);flush();assert(actual[8][0]==6);
    write(8,1,7);flush();assert(actual[8][0]==7 && emitted.size()==2);
    batch.Reset();emitted.clear();flush();assert(emitted.empty());
    std::puts("PASS: overlapping/adjacent/sparse/boundary writes match sequential register state");
}
