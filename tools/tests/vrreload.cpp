#include "../../source/includes/vrreload.hpp"
#include "../../source/includes/vrrecoil.hpp"
#include <cstdio>
#include <cstdlib>
static void Check(bool condition) {if(!condition) std::abort();}
int main()
{
    VRRecoil::Kick kick; kick.Shot(1000,6,.18f,18);
    Check(std::abs(kick.degrees-6)<.00001f);
    const auto kickPose=VRRecoil::Pose(VRMath::Identity(),VRMath::Identity(),kick.degrees);
    Check(kickPose[0][2]>.1f && kickPose[3][2]>.03f); // muzzle and entire wrist rise
    Check(kickPose[3][0]<0 && std::abs(kickPose[3][1])<.00001f);
    auto frequent=kick;frequent.Sample(1050,.18f);frequent.Sample(1100,.18f);
    Check(std::abs(frequent.degrees-kick.Sample(1100,.18f))<.00001f);
    VRRecoil::Kick supported;supported.Shot(1000,6*.15f,.18f,18);
    Check(std::abs(supported.degrees-.9f)<.00001f);
    for(unsigned i=0;i<20;++i) kick.Shot(1100,6,.18f,18);
    Check(kick.degrees==18 && kick.Sample(3100,.18f)<.001f);
    for(unsigned i=0;i<20;++i) kick.Shot(3100,6,.18f,75);
    Check(kick.degrees==75 && std::isfinite(kick.Sample(3200,.18f)));
    Check(VRRecoil::Pose(VRMath::Identity(),VRMath::Identity(),0)==VRMath::Identity());
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
    // Independent rifle/pistol ownership survives a switch with the magazine
    // absent. A held reserve magazine is returned only to its own weapon.
    VRReload::Pistol rifle;rifle.Begin(30,90,30);
    Check(rifle.Eject()==29 && rifle.Clip()==1 && !rifle.inserted);
    Check(rifle.TakeMagazine() && rifle.held==29 && rifle.reserve==31);
    rifle.ReturnHeld();const auto savedRifle=rifle;
    gun.Begin(17,51,17);Check(gun.Fired() && gun.Clip()==16);
    rifle=savedRifle;Check(!rifle.inserted && rifle.Clip()==1 && rifle.reserve==60);
    // Vanilla may report a newly refilled30-round clip on reselect. Resume
    // uses total61, restoring the saved single chambered round and no mag.
    Check(rifle.Resume(61,30) && !rifle.inserted && rifle.Clip()==1 && rifle.reserve==60);
    Check(rifle.Resume(71,30) && rifle.Clip()==1 && rifle.reserve==70); // pickup
    Check(rifle.Resume(61,30) && rifle.reserve==60); // reserve removal
    Check(!rifle.Resume(0,30) && !rifle.Resume(61,17));
    Check(rifle.Fired() && !rifle.Ready());
    Check(rifle.TakeMagazine() && rifle.held==30 && rifle.Insert() && !rifle.Ready());
    rifle.Rack();Check(rifle.Ready() && rifle.Clip()==30 && rifle.reserve==30);
    Check(rifle.Fired() && rifle.Fired() && rifle.Clip()==28);
    Check(rifle.Resume(58,30) && rifle.Clip()==28 && rifle.reserve==30);
    rifle.Begin(0,60,30);Check(rifle.Resume(60,30) && !rifle.Ready() && rifle.Clip()==0);
    std::puts("pistol/rifle reload accounting passed");
}
