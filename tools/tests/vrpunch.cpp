#include "../../source/includes/vrpunch.hpp"
#include <cstdio>
#include <cstdlib>

static void Check(bool condition)
{if(!condition) {std::fprintf(stderr,"punch gate failed\n");std::exit(1);}}

int main()
{
    Check(VRPunch::FreeHand(0,0,false,false));
    Check(VRPunch::FreeHand(1,0,false,false));
    Check(VRPunch::FreeHand(0,7,true,false));
    Check(!VRPunch::FreeHand(1,7,true,false));
    Check(!VRPunch::FreeHand(0,7,true,true));
    Check(!VRPunch::FreeHand(0,0,true,false));
    Check(!VRPunch::FreeHand(0,9,true,false));
    Check(!VRPunch::FreeHand(2,0,false,false));
    VRPunch::Strike punch;
    Check(!punch.Sample({0,0,0,1},{0,0,0,1},100,false,1.6f));
    Check(!punch.Sample({0,0,0,1},{.1f,0,0,1},110,false,1.6f));
    // Moving body with a stationary tracked fist, or an open fast hand.
    Check(!punch.Sample({0,0,0,1},{.2f,0,0,1},120,true,1.6f));
    Check(!punch.Sample({.04f,0,0,1},{.24f,0,0,1},130,false,1.6f));
    Check(punch.Sample({.08f,0,0,1},{.28f,0,0,1},140,true,1.6f));
    punch.Contact();
    Check(!punch.Sample({.12f,0,0,1},{.32f,0,0,1},150,true,1.6f));
    Check(!punch.Sample({.12f,0,0,1},{.32f,0,0,1},150,true,1.6f)); // duplicate
    for(uint64_t time=160;time<=440;time+=10)
        Check(!punch.Sample({.12f,0,0,1},{.32f,0,0,1},time,true,1.6f));
    Check(punch.Sample({.16f,0,0,1},{.36f,0,0,1},450,true,1.6f));
    punch.Contact();
    Check(!punch.Sample({2,0,0,1},{2.2f,0,0,1},460,true,1.6f)); // consumed strike/cooldown
    Check(!punch.Sample({2.04f,0,0,1},{2.24f,0,0,1},470,true,1.6f)); // stays disarmed
    Check(!punch.Sample({2.04f,0,0,1},{2.24f,0,0,1},800,true,1.6f)); // gap
    Check(!punch.Sample({2.04f,0,0,1},{2.24f,0,0,1},810,false,1.6f));
    Check(punch.Sample({2.08f,0,0,1},{2.28f,0,0,1},820,true,1.6f));
    VRPunch::Strike fast;
    Check(!fast.Sample({0,0,0,1},{0,0,0,1},100,false,1.6f));
    Check(!fast.Sample({0,0,0,1},{0,0,0,1},110,false,1.6f));
    Check(fast.Sample({.3f,0,0,1},{.3f,0,0,1},120,true,1.6f)); // no upper velocity cap
    const float invalid=std::numeric_limits<float>::quiet_NaN();
    Check(!fast.Sample({invalid,0,0,1},{invalid,0,0,1},130,true,1.6f));
    std::puts("punch gating passed");
}
