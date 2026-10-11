#include "../../source/includes/vrresolution.hpp"
#include <cassert>
#include <limits>
int main()
{
    using namespace VRResolution;
    auto eye=Recommended({1512,1680},{8192,8192},1);
    assert(eye.width==1512 && eye.height==1680);
    eye=Recommended({1512,1680},{8192,8192},.75f);
    assert(eye.width==1134 && eye.height==1260);
    eye=Recommended({1512,1680},{2000,2000},10);
    assert(eye.height==2000 && eye.width==1800);
    assert(Relative(1280,2560,1512)==756);
    assert(Relative(720,1440,1680)==840);
    assert(Relative(1,2560,1)==1);
    assert(PixelCenter(-.5f,2560,1512)==-.5f);
    assert(PixelCenter(2559.5f,2560,1512)==1511.5f);
    assert(MipLevels({1,1},12)==1);
    assert(MipLevels({1424,1584},12)==11);
    float output[4]{},full[]{2560,1440,1.f/2560,1.f/1440},origin[]{0,0,1.f/2560,1.f/1440};
    assert(ScreenSize(full,{2560,1440},{2848,3168},output));
    assert(output[0]==2848 && output[1]==3168 && output[3]==1.f/3168);
    assert(ScreenSize(origin,{2560,1440},{2848,3168},output));
    assert(output[0]==0 && output[1]==0 && output[2]==1.f/2848);
    float other[]{1,0,1.f/2560,1.f/1440};
    assert(!ScreenSize(other,{2560,1440},{2848,3168},output));
    for(float scale:{0.f,-1.f,std::numeric_limits<float>::infinity()})
    {bool rejected=false;try{Recommended({1,1},{2,2},scale);}catch(...){rejected=true;}assert(rejected);}
}
