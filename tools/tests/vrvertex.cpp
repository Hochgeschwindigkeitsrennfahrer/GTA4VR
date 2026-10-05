#include "../../source/includes/vrvertex.hpp"
#include <array>
#include <cassert>

int main()
{
    const std::array<std::uint32_t,9> record{0x7FC12345,0x80000000,0x7F800000,
        0xFF800000,0x00000001,0xFFFFFFFF,0x12345678,0x3F800000,0xABCDEF01};
    std::array<std::uint32_t,21> buffer{};
    buffer.fill(0xDEADBEEF);
    auto* output=buffer.data()+1; // Deliberately not aligned to sixteen bytes.
    std::uint32_t count=0;
    VRVertex::Emit(1,output,count,record.data());
    VRVertex::Emit(1,output,count,record.data()); // Consecutive36-byte strides.
    assert(count==2 && output==buffer.data()+19);
    assert(buffer[0]==0xDEADBEEF && buffer[19]==0xDEADBEEF && buffer[20]==0xDEADBEEF);
    for(unsigned i=0;i<18;++i) assert(buffer[i+1]==record[i%9]);
    auto* saved=output;
    VRVertex::Emit(0,output,count,nullptr);
    assert(count==3 && output==saved && buffer[19]==0xDEADBEEF);
    output=nullptr;
    VRVertex::Emit(1,output,count,nullptr);
    assert(count==4 && !output);
}
