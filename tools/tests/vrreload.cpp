#include "../../source/includes/vrreload.hpp"
#include <cstdio>
#include <cstdlib>
static void Check(bool condition) {if(!condition) std::abort();}
int main()
{
    auto hand=VRMath::Identity();hand[3]={-.04f,.10f,.15f,1};
    const VRMath::Vector rear{-.07953f,-.00101f,.04869f,1};
    const auto rack=VRReload::SlideHandPose(hand,rear);
    Check(std::abs(rack[3][0]-hand[3][0])<.00001f); // nearby X retained
    Check(std::abs(rack[3][1]-(rear[1]+.025f))<.00001f);
    Check(std::abs(rack[3][2]-(rear[2]+.02f))<.00001f);
    hand[3][0]=.25f;
    Check(std::abs(VRReload::SlideHandPose(hand,rear)[3][0]+.05f-(rear[0]+.16f))<.00001f);
    VRReload::InsertionGuide guide;
    guide.Begin();Check(!guide.Update(0,0)); // initial snap cannot seat
    Check(!guide.Update(.04f,.02f) && guide.active);
    Check(guide.Update(.09f,.01f));
    guide.Begin();Check(!guide.Update(-.13f,0) && !guide.active);
    guide.Begin();Check(!guide.Update(.02f,.19f) && !guide.active);
    guide.Begin();Check(!guide.Update(NAN,0) && !guide.active);
    guide.Begin();Check(!guide.Update(.09f,0,.12f) && guide.active);
    Check(guide.Update(.12f,0,.12f)); // configured travel, not the default
    guide.Begin();Check(!guide.Update(.04f,0,INFINITY) && !guide.active);
    VRReload::Pistol gun;
    gun.Begin(17,51,17);
    Check(gun.Fired() && gun.Clip()==16 && gun.NativeTotal()==50);
    Check(gun.Eject()==15 && gun.Clip()==1 && gun.NativeTotal()==35);
    Check(gun.Eject()==0 && gun.discarded==15); // no repeated loss
    Check(gun.TakeMagazine() && gun.held==16 && gun.reserve==18);
    Check(!gun.TakeMagazine() && gun.Owned()==35);
    Check(gun.Insert() && !gun.Insert() && gun.Clip()==17);
    Check(gun.Ready()); // tactical reload retains the chamber
    gun.Begin(1,35,17);Check(gun.Fired() && !gun.Ready());
    gun.Eject();Check(gun.TakeMagazine() && gun.held==17);
    Check(gun.Insert() && !gun.Ready()); // insertion cannot chamber itself
    gun.Rack();Check(gun.Ready() && gun.Clip()==17 && gun.Owned()==34);
    gun.Rack();Check(gun.Owned()==33 && gun.discarded==1);
    gun.Eject();Check(gun.TakeMagazine());const auto owned=gun.Owned();
    gun.ReturnHeld();Check(gun.Owned()==owned && !gun.holding);
    Check(gun.TakeMagazine());const auto dropped=gun.held;
    gun.DropHeld();Check(gun.Owned()==owned-dropped);
    gun.Begin(0,0,17);Check(!gun.TakeMagazine() && !gun.Fired());
    // Exhaustive short sequences check conservation and invalid repetition.
    for(unsigned sequence=0;sequence<4096;++sequence)
    {
        gun.Begin(9,29,17);unsigned fired=0;
        for(unsigned n=sequence,step=0;step<6;++step,n>>=2)
        {
            switch(n&3) {case 0:gun.Eject();break;case 1:gun.TakeMagazine();gun.Insert();break;
                case 2:gun.Rack();break;case 3:if(gun.Fired()) ++fired;break;}
            Check(gun.Owned()+gun.discarded+fired==29 && gun.Clip()<=17);
        }
    }
    std::puts("pistol reload accounting passed");
}
