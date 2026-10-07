#include "../../source/includes/vrmelee.hpp"
#include <cstdio>
#include <cstdlib>
static void Check(bool condition) {if(!condition) {std::fputs("blade gate failed\n",stderr);std::exit(1);}}
int main()
{
    auto frame=VRMath::Identity();
    const auto points=VRMelee::SampleBlade(VRMelee::Knife,frame);
    Check(std::abs(points.front()[2]-.06f)<.00001f && std::abs(points[VRMelee::Knife.samples-1][2]-.247477f)<.00001f);
    VRMelee::Strike strike;
    Check(!strike.Sample(points,points,100,1.2f));
    for(uint64_t time=110;time<=180;time+=10) Check(!strike.Sample(points,points,time,1.2f));
    auto world=points;
    for(auto& p:world) p[0]+= .1f;
    Check(!strike.Sample(points,world,190,1.2f)); // locomotion alone
    auto moved=points;
    for(auto& p:moved) p[0]+=.03f;
    Check(strike.Sample(moved,moved,200,1.2f)); // stab translation
    strike.Contact();
    Check(!strike.Sample(moved,moved,200,1.2f)); // duplicate timestamp
    for(uint64_t time=210;time<=550;time+=10) Check(!strike.Sample(moved,moved,time,1.2f));
    // A wrist-only rotation qualifies through blade tip speed even though the
    // hand/root remains stationary. One contact consumes the whole blade.
    auto rotated=moved;rotated[VRMelee::Knife.samples-1][0]+=.03f;
    Check(strike.Sample(rotated,rotated,560,1.2f));
    strike.Contact();
    rotated[VRMelee::Knife.samples-1][0]+=.03f;
    Check(!strike.Sample(rotated,rotated,570,1.2f));
    Check(!strike.Sample(rotated,rotated,800,1.2f)); // tracking gap
    Check(!strike.Sample(rotated,rotated,810,1.2f)); // still disarmed after tracking gap
    rotated.front()[0]=std::numeric_limits<float>::quiet_NaN();
    Check(!strike.Sample(rotated,rotated,820,1.2f));

    // Repeated slashes do not require a deliberate slow pause. Withdrawal
    // rearms the stroke, but the shorter contact cooldown still bounds damage.
    VRMelee::Strike repeat;
    Check(!repeat.Sample(points,points,100,1.2f));
    for(uint64_t time=110;time<=140;time+=10) Check(!repeat.Sample(points,points,time,1.2f));
    auto slash=points;
    for(auto& p:slash) p[0]+=.03f;
    Check(repeat.Sample(slash,slash,150,1.2f));repeat.Contact();
    for(uint64_t time=160;time<=320;time+=10)
    {
        for(auto& p:slash) p[0]-=.035f;
        Check(!repeat.Sample(slash,slash,time,1.2f));
        if(time>=170) Check(repeat.gate==VRMelee::Strike::Gate::Cooldown);
    }
    for(auto& p:slash) p[0]-=.035f;
    Check(repeat.Sample(slash,slash,330,1.2f));repeat.Contact();
    // Continued motion in the same direction is not a new strike, including
    // after cooldown expires. No repeated damage from pushing deeper in.
    for(uint64_t time=340;time<=570;time+=10)
    {
        for(auto& p:slash) p[0]-=.02f;
        Check(!repeat.Sample(slash,slash,time,1.2f));
        Check(repeat.gate==VRMelee::Strike::Gate::Rearm);
    }
    // Holding a blade in place never qualifies, even when the player moves.
    auto translated=slash;
    for(uint64_t time=580;time<=620;time+=10)
    {
        for(auto& p:translated) p[1]+=.02f;
        Check(!repeat.Sample(slash,translated,time,1.2f));
    }
    Check(repeat.armed);
    Check(!repeat.Sample(slash,translated,620,1.2f));
    Check(repeat.gate==VRMelee::Strike::Gate::Duplicate);
    VRMelee::Profile bat{},cue{};
    Check(VRMelee::ClubProfile(1,{.026922f,-.061135f,-.086978f,1},{.141087f,.064811f,.71821f,1},bat));
    Check(bat.samples>5 && bat.type==1 && bat.radius==.055f);
    Check(VRMath::Length3(VRMath::Difference3(bat.tip,bat.base))>.6f);
    Check(VRMelee::ClubProfile(2,{.047686f,-.152924f,-.278012f,1},{.208190f,.065249f,1.20778f,1},cue));
    Check(cue.samples==20);
    Check(cue.samples>bat.samples && cue.samples<=VRMelee::SampleCount);
    for(const auto& profile:{bat,cue})
    {
        const auto samples=VRMelee::SampleBlade(profile,frame);
        Check(VRMath::Length3(VRMath::Difference3(samples.front(),profile.base))<.000001f &&
            VRMath::Length3(VRMath::Difference3(samples[profile.samples-1],profile.tip))<.000001f);
        for(unsigned i=1;i<profile.samples;++i)
            Check(VRMath::Length3(VRMath::Difference3(samples[i],samples[i-1]))<=2*profile.radius);
        VRMelee::Strike club;
        Check(!club.Sample(samples,samples,100,1.2f,profile.samples));
        for(uint64_t time=110;time<=140;time+=10) Check(!club.Sample(samples,samples,time,1.2f,profile.samples));
        auto swung=samples;swung[profile.samples-1][0]+=.03f;
        Check(club.Sample(swung,swung,150,1.2f,profile.samples)); // distal contact drives stroke
        club.Contact();
        swung[profile.samples-1][0]+=.03f;
        Check(!club.Sample(swung,swung,160,1.2f,profile.samples));
        VRMelee::Strike fastClub;
        Check(!fastClub.Sample(samples,samples,100,1.2f,profile.samples));
        for(uint64_t time=110;time<=140;time+=10) Check(!fastClub.Sample(samples,samples,time,1.2f,profile.samples));
        const float turn[4]={0,std::sin(.125f),0,std::cos(.125f)};
        auto fast=VRMelee::SampleBlade(profile,VRMath::Pose({0,0,0},turn)); // rigid wrist rotation
        Check(fastClub.Sample(fast,fast,150,1.2f,profile.samples));
        Check(fastClub.speed>12.f); // knife's old distal bound rejected this
        // Valid whole-hand translation must not be capped either.
        for(auto& point:fast) point[1]+=.20f;
        Check(fastClub.Sample(fast,fast,160,1.2f,profile.samples));
    }
    Check(!VRMelee::ClubProfile(7,{-.02f,-.02f,-.1f,1},{.02f,.02f,1.4f,1},cue));
    Check(!VRMelee::ClubProfile(1,{-1,-1,-1,1},{1,1,1,1},bat));
    Check(!VRMelee::ClubProfile(2,{0,0,0,1},{.02f,.02f,3.f,1},cue));
    VRMelee::Strike fastKnife;
    Check(!fastKnife.Sample(points,points,100,1.2f));
    for(uint64_t time=110;time<=140;time+=10) Check(!fastKnife.Sample(points,points,time,1.2f));
    auto fastBlade=points;
    for(auto& point:fastBlade) point[0]+=1.f;
    Check(fastKnife.Sample(fastBlade,fastBlade,150,1.2f)); // no upper speed/displacement cap
    std::puts("blade geometry and gating passed");
}
