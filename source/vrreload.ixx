module;
#include <common.hxx>
#include "includes/vrmath.hpp"
#include "includes/vrgrip.hpp"
#include "includes/vrreload.hpp"
#include "includes/vrammocounter.hpp"
#include <atomic>
#include <cstring>
#include <string>
export module vrreload;
import common;
import vrdiagnostics;
import comvars;
import natives;
import openxrvr;
import vrconfig;
import vrhud;

export namespace VRReloadRuntime
{
    void Install();
    void Publish(void* owner,const VRMath::Matrix& transform,const VRMath::Matrix (&tracked)[2],const VRMath::Matrix (&rendered)[2]);
    bool CanFire(void* owner,void* weapon);
    void EmptyTrigger(void* owner,void* weapon);
    void Fired(void* owner,void* weapon);
    bool UsesLeftHand();
    bool PumpHeld();
    bool HoldsMagazine();
    bool CounterGrabPriority(void* ped,void* gun,const VRMath::Matrix& hand);
    bool SlideGrip(void* ped,void* gun,VRMath::Matrix& relative);
    bool ReloadConfig();
    bool TestFireAnimation();
    std::array<float,4> RecoilSettings(unsigned type);
    std::array<float,3> SupportRotation(unsigned type);
    std::array<float,3> SupportOffset(unsigned type);
    unsigned ConfigRevision();
    bool GetRenderedSniperPose(VRMath::Matrix& world);
    bool GetAmmoCounter(const void* entity,VRAmmoCounter::Snapshot& snapshot);
    const char* ScopeProfileFile();
    bool IsScopeGun(const void* entity);
    bool IsScopeOccluder(const void* entity);
    bool HideNativeRocket(const void* entity);
    void PrepareObjectRender(void* entity);
    std::string Status();
}
namespace
{
    bool enabled=false;
    std::atomic<bool> vehicleMode=false;
    SafetyHookInline poseHook{},worldPoseHook{},reloadHook{},completionHook{},decisionHook{},taskHook{};
    std::atomic<void*> nativeRocketVisual=nullptr;
    std::atomic<unsigned> nativeRocketSkips=0,rocketRenderUpdates=0;
    using AllocatePose=void (__thiscall*)(void*);
    AllocatePose allocatePose=nullptr;
    using FinishReloadTask=void (__thiscall*)(void*,void*);
    FinishReloadTask finishReloadTask=nullptr;
    SRWLOCK poseLock=SRWLOCK_INIT;
    struct Sample {void* owner=nullptr;VRMath::Matrix transform{},wrist[2]{},hand[2]{};uint64_t at=0;} sample;
    struct SlideHand {void* owner=nullptr;void* gun=nullptr;VRMath::Matrix local{},gripInverse{};uint64_t at=0;} slideHand;
    void* owner=nullptr;void* currentWeapon=nullptr;void* gunEntity=nullptr;
    std::atomic<unsigned> currentType=7;
    struct SavedAmmo {VRReload::Pistol ammo{};bool valid=false;} savedAmmo[11];
    unsigned AmmoIndex(unsigned type) {return type==17 ? 10u:type==14 ? 9u:type==10 ? 8u:(type==12 ? 7u:(type==11 ? 6u:(type==18 ? 5u:(type==16 ? 4u:(type==15 ? 1u:(type==9 ? 2u:(type==13 ? 3u:0u)))))));}
    const char* ProfileFile(unsigned type) {return type==17 ? "VRWeapons/sniper.ini":type==14 ? "VRWeapons/ak47.ini":type==10 ? "VRWeapons/pump-shotgun.ini":(type==12 ? "VRWeapons/micro-smg.ini":(type==11 ? "VRWeapons/combat-shotgun.ini":(type==18 ? "VRWeapons/rpg.ini":(type==16 ? "VRWeapons/psg1.ini":(type==15 ? "VRWeapons/m4.ini":(type==9 ? "VRWeapons/deagle.ini":(type==13 ? "VRWeapons/smg.ini":"VRWeapons/pistol.ini")))))));}
    std::atomic<void*> guardedWeapon=nullptr,visualGun=nullptr,visualMagazine=nullptr;
    std::atomic<void*> scopeGun=nullptr;
    std::atomic<bool> chamberReady=true,inserted=true,leftBusy=false;
    std::atomic<bool> pumpOpen=false;
    std::atomic<float> slideDistance=0;
    std::atomic<unsigned> poseUpdates=0,magazineUpdates=0,blockedReloads=0,blockedTaskReloads=0,ejections=0,insertions=0,racks=0;
    std::atomic<float> insertionDistance=-1;
    std::atomic<bool> insertionAligned=false;
    VRMath::Vector rocketTail{0,0,0,0};
    std::atomic<float> insertionProgress=0;
    std::atomic<uint32_t> magazineRoom=0;
    std::atomic<bool> configPending=false;
    std::atomic<unsigned> configRevision=0;
    std::atomic<bool> configValid=true;
    std::atomic<unsigned> emptyClicks=0,slidePullSounds=0,slideReleaseSounds=0;
    std::atomic<unsigned> fireAnimations=0,fireAnimationMisses=0;
    std::atomic<unsigned> fireAnimationError=0;
    std::atomic<bool> fireAnimationTest=false;
    std::atomic<float> recoilKick=6.f,recoilSupportScale=.15f,recoilRecovery=.18f,recoilMaximum=75.f;
    struct Profile
    {
        VRAmmoCounter::Settings counter{};
        float handRotation[3]{100,180,180},handOffset[3]{.075f,.05f,.04f};
        float insertionRotation[3]{};
        VRMath::Vector axis{.258819f,0,.965926f,0};
        VRMath::Vector tip{-.05f,-.002341f,.024f,1};
        float travel=.085f,snap=.045f,alignment=.5f;
        float rackTravel=.045f,rackThreshold=.035f,rackRadius=.15f;
        float rackReturnThreshold=.015f;
        VRMath::Vector rackGripOffset{0,0,0,0};
        float recoilKick=0,recoilSupportScale=.15f,recoilRecovery=.18f,recoilMaximum=75.f;
        float supportRotation[3]{},supportOffset[3]{};
        VRMath::Vector shellPort{.132867f,-.00079973f,.0330923f,1};
        VRMath::Vector rocketEntrance{.318807f,.015501f,.062882f,1};
        float rocketLoadedDepth=.03f;
    } profile;
    std::atomic<bool> counterEditing=false,counterSavePending=false,counterSaveOk=true;
    struct CounterEdit
    {
        VRMath::Matrix hand{},panel{};
        float width=0,height=0,distance=0;
        bool resizing=false;
        unsigned type=0;
        uint64_t changedAt=0;
        VRAmmoCounter::Settings pending{};
    } counterEdit;
    void SaveCounter(bool force=false)
    {
        if(!counterSavePending || (!force && GetTickCount64()-counterEdit.changedAt<400)) return;
        CIniReader path(ProfileFile(counterEdit.type));
        mINI::INIFile file(path.GetIniPath());mINI::INIStructure values;
        bool success=file.read(values);
        if(success)
        {
            const char* offsets[]{"OffsetX","OffsetY","OffsetZ"},*rotations[]{"RotationX","RotationY","RotationZ"};
            for(unsigned a=0;a<3;++a)
            {
                values["AmmoCounter"][offsets[a]]=std::to_string(counterEdit.pending.offset[a]);
                values["AmmoCounter"][rotations[a]]=std::to_string(counterEdit.pending.rotation[a]);
            }
            values["AmmoCounter"]["Width"]=std::to_string(counterEdit.pending.width);
            values["AmmoCounter"]["Height"]=std::to_string(counterEdit.pending.height);
            success=file.write(values);
        }
        counterSaveOk=success;
        if(success) counterSavePending=false;else counterEdit.changedAt=GetTickCount64();
    }
    Profile Defaults(unsigned type)
    {
        Profile value;
        value.counter.stacked=type==12 || type==13 || type==14 || type==15;
        if(value.counter.stacked) {value.counter.width=.045f;value.counter.height=.032f;value.counter.offset[0]=-.20f;value.counter.offset[2]=.10f;}
        if(type==16 || type==17 || type==18) {value.counter.offset[0]=-.25f;value.counter.offset[2]=.13f;}
        if(type==18)
        {
            value.axis={-1,0,0,0};value.tip={.54f,0,.055f,1};value.travel=.15f;
            value.handRotation[0]=0;value.handRotation[1]=90;value.handRotation[2]=0;
            value.handOffset[0]=.08f;value.handOffset[1]=.05f;value.handOffset[2]=-.12f;
            value.snap=.06f;
            value.supportRotation[0]=180;value.supportRotation[2]=180;
            value.rocketLoadedDepth=.09f;
            value.supportOffset[0]=-.05f;
        }
        if(type==11)
        {
            // Shell/gate are children of gun_stuff (+.344,+.007,-.013),
            // so insertion points must include that parent transform.
            value.axis={.894427191f,0,.4472135955f,0};value.travel=.04f;value.tip={.271074f,-.00027224f,.0408131f,1};
            value.handOffset[1]=.02f;value.handOffset[2]=0;
            value.insertionRotation[1]=-60.f;value.snap=.08f;
            value.rackTravel=.065f;value.rackThreshold=.045f;value.rackRadius=.12f;
            value.rackGripOffset={0,-.05f,.02f,0};
            value.recoilKick=10.f;value.recoilMaximum=45.f;
        }
        if(type==17)
        {
            value.axis={0,0,1,0};value.tip={-.136978f,0,-.01641f,1};
            value.travel=.05f;value.rackTravel=.09f;value.rackThreshold=.065f;value.rackRadius=.12f;
            value.rackGripOffset={0,.04f,0,0};value.recoilKick=10.f;value.recoilMaximum=45.f;
        }
        if(type==16)
        {
            value.axis={0,0,1,0};value.travel=.05f;
            value.handRotation[2]=140;
            value.tip={-.0139846f,.00878203f,-.0202853f,1};
            value.rackTravel=.065f;value.rackThreshold=.05f;value.rackRadius=.10f;
            value.rackGripOffset={0,.07f,.025f,0};
            value.recoilKick=8.f;value.recoilMaximum=40.f;
        }
        if(type==9) value.recoilKick=8.f;
        if(type==10)
        {
            value.axis={1,0,0,0};value.tip={-.06798325f,-.00303943f,.02658558f,1};
            value.shellPort={-.20107155f,-.00063685f,.01339588f,1};
            value.travel=.04f;value.snap=.08f;value.insertionRotation[1]=-20.f;
            value.handOffset[1]=.02f;value.handOffset[2]=0;
            value.rackTravel=.13f;value.rackThreshold=.09f;value.rackRadius=.16f;
            value.recoilKick=10.f;value.recoilMaximum=45.f;
        }
        if(type==12)
        {
            value.handRotation[2]=145.f;value.snap=.06f;
            value.axis={0,0,1,0};value.tip={-.03807348f,-.00078788f,-.14679066f,1};
            value.travel=.06f;value.rackTravel=.045f;value.rackThreshold=.03f;value.rackRadius=.12f;
            value.rackGripOffset={0,0,.025f,0};
            value.recoilKick=3.f;value.recoilMaximum=35.f;
        }
        if(type==13)
        {
            value.axis={0,0,1,0};value.travel=.07f;
            value.rackTravel=.055f;value.rackThreshold=.04f;value.rackRadius=.10f;
            value.recoilKick=3.f;value.recoilMaximum=35.f;
        }
        if(type==14)
        {
            // AK_stuff parent transform included; live rig readback follows deployment.
            value.axis={0,0,1,0};value.tip={-.0711513f,-.0113021f,.0185908f,1};
            value.travel=.02f;value.rackTravel=.065f;value.rackThreshold=.05f;value.rackRadius=.12f;
            value.rackGripOffset={0,.06f,.01f,0};value.recoilKick=7.f;
            value.handRotation[0]=180;value.handRotation[1]=120;value.handOffset[1]=.02f;
            value.supportRotation[1]=45;value.supportOffset[1]=-.04f;
        }
        if(type==15)
        {
            value.axis={0,0,1,0};value.tip={-.0421465f,-.023525f,.0169907f,1};
            value.travel=.05f;value.rackTravel=.065f;value.rackThreshold=.05f;value.rackRadius=.10f;
            value.rackGripOffset={-.06f,0,0,0};
            value.recoilKick=6.f;
        }
        return value;
    }
    bool refreshHeldPose=false;
    VRReload::Pistol ammo{};
    bool initialized=false,rightGrip=false,leftGrip=false,pulled=false;
    std::atomic<bool> racking=false;
    VRMath::Vector rackStart{};
    VRMath::Matrix rackHandLocal{};
    VRReload::InsertionGuide guide{};
    VRMath::Vector guideStart{};
    Object magazineObject=0;
    void* magazineEntity=nullptr;
    VRMath::Matrix magazineOffset=VRMath::Identity();
    bool dropped=false;
    VRMath::Matrix droppedRoot=VRMath::Identity();
    float dropVelocity=0;uint64_t dropAt=0;
    unsigned magazineBone=~0u,shellSecondBone=~0u,slideBone=~0u,rackChildBone=~0u,primaryGripBone=~0u,boneCount=0;
    void* definition=nullptr;
    VRMath::Matrix rest[32]{};
    unsigned previousNativeTotal=0;
    std::atomic<unsigned> reportedClip=0,reportedReserve=0,reportedHeld=0,reportedDiscarded=0;

    void PlayPistolSound(const char* event)
    {
        if(!gunEntity) return;
        auto* frame=*reinterpret_cast<VRMath::Matrix**>(static_cast<char*>(gunEntity)+0x20);
        if(!frame) return;
        // Audio only; never start the native reload animation/task.
        Natives::PlaySoundFromPosition(-1,const_cast<char*>(event),(*frame)[3][0],(*frame)[3][1],(*frame)[3][2]);
    }

    void LoadProfile()
    {
        CIniReader ini(ProfileFile(currentType.load()));Profile next=Defaults(currentType.load());
        std::error_code error;
        if(!std::filesystem::is_regular_file(ini.GetIniPath(),error)) {configValid=false;return;}
        bool valid=true;
        auto read=[&](const char* key,float fallback,const char* section="Reload")
        {
            const auto value=ini.ReadString(section,key,std::to_string(fallback));
            try
            {
                size_t end=0;const float parsed=std::stof(value,&end);
                if(value.find_first_not_of(" \t\r\n",end)!=std::string::npos ||
                    !std::isfinite(parsed)) {valid=false;return fallback;}
                return parsed;
            }
            catch(...) {valid=false;return fallback;}
        };
        const char* rotations[]{"MagazineHandRotationX","MagazineHandRotationY","MagazineHandRotationZ"};
        const char* insertionRotations[]{"InsertionRotationX","InsertionRotationY","InsertionRotationZ"};
        const char* offsets[]{"MagazineHandOffsetX","MagazineHandOffsetY","MagazineHandOffsetZ"};
        const char* axes[]{"InsertionAxisX","InsertionAxisY","InsertionAxisZ"};
        const char* tips[]{"MagazineTipX","MagazineTipY","MagazineTipZ"};
        for(unsigned a=0;a<3;++a)
        {
            next.handRotation[a]=read(rotations[a],next.handRotation[a]);
            next.insertionRotation[a]=read(insertionRotations[a],next.insertionRotation[a]);
            next.handOffset[a]=read(offsets[a],next.handOffset[a]);
            next.axis[a]=read(axes[a],next.axis[a]);
            next.tip[a]=read(tips[a],next.tip[a]);
        }
        const char* supportRotations[]{"SupportHandRotationX","SupportHandRotationY","SupportHandRotationZ"};
        for(unsigned a=0;a<3;++a) next.supportRotation[a]=read(supportRotations[a],next.supportRotation[a]);
        const char* supportOffsets[]{"SupportHandOffsetX","SupportHandOffsetY","SupportHandOffsetZ"};
        for(unsigned a=0;a<3;++a) next.supportOffset[a]=read(supportOffsets[a],next.supportOffset[a]);
        const char* shellPorts[]{"ShellPortX","ShellPortY","ShellPortZ"};
        for(unsigned a=0;a<3;++a) next.shellPort[a]=read(shellPorts[a],next.shellPort[a]);
        next.rocketLoadedDepth=read("RocketLoadedDepth",next.rocketLoadedDepth);
        const char* entranceKeys[]{"RocketEntranceX","RocketEntranceY","RocketEntranceZ"};
        for(unsigned a=0;a<3;++a) next.rocketEntrance[a]=read(entranceKeys[a],next.rocketEntrance[a]);
        const float length=VRMath::Length3(next.axis);
        if(!std::isfinite(length) || length<1.e-6f) valid=false;
        else for(unsigned a=0;a<3;++a) next.axis[a]/=length;
        next.travel=read("InsertionTravel",next.travel);
        next.snap=read("SnapRadius",next.snap);
        next.alignment=read("AlignmentCosine",next.alignment);
        next.rackTravel=read("RackTravel",next.rackTravel);
        next.rackThreshold=read("RackThreshold",next.rackThreshold);
        next.rackReturnThreshold=read("RackReturnThreshold",next.rackReturnThreshold);
        next.rackRadius=read("RackGrabRadius",next.rackRadius);
        const char* rackOffsets[]{"RackGripOffsetX","RackGripOffsetY","RackGripOffsetZ"};
        for(unsigned a=0;a<3;++a)
            next.rackGripOffset[a]=read(rackOffsets[a],next.rackGripOffset[a]);
        next.recoilKick=read("RecoilKickDegrees",next.recoilKick);
        next.recoilSupportScale=read("RecoilSupportScale",next.recoilSupportScale);
        next.recoilRecovery=read("RecoilRecoverySeconds",next.recoilRecovery);
        next.recoilMaximum=read("RecoilMaxDegrees",next.recoilMaximum);
        const float counterEnabled=read("Enabled",1,"AmmoCounter");
        if(counterEnabled!=0 && counterEnabled!=1) valid=false;
        next.counter.enabled=counterEnabled!=0;
        const float counterStacked=read("Stacked",next.counter.stacked ? 1.f:0.f,"AmmoCounter");
        const float counterEditEnabled=read("EditWithOffHand",0,"AmmoCounter");
        if((counterStacked!=0 && counterStacked!=1) || (counterEditEnabled!=0 && counterEditEnabled!=1)) valid=false;
        next.counter.stacked=counterStacked!=0;next.counter.editWithOffHand=counterEditEnabled!=0;
        next.counter.grabRadius=read("GrabRadius",next.counter.grabRadius,"AmmoCounter");
        if(next.counter.grabRadius<=0) valid=false;
        const char* counterOffsets[]{"OffsetX","OffsetY","OffsetZ"};
        const char* counterRotations[]{"RotationX","RotationY","RotationZ"};
        for(unsigned a=0;a<3;++a)
        {
            next.counter.offset[a]=read(counterOffsets[a],next.counter.offset[a],"AmmoCounter");
            next.counter.rotation[a]=read(counterRotations[a],next.counter.rotation[a],"AmmoCounter");
        }
        next.counter.width=read("Width",next.counter.width,"AmmoCounter");
        next.counter.height=read("Height",next.counter.height,"AmmoCounter");
        if(next.counter.width<=0 || next.counter.height<=0) valid=false;
        configValid=valid;
        if(valid) {counterEditing=false;profile=next;guide={};insertionProgress=0;refreshHeldPose=true;
            recoilKick=next.recoilKick;recoilSupportScale=next.recoilSupportScale;
            recoilRecovery=next.recoilRecovery;recoilMaximum=next.recoilMaximum;
            racking=false;slideDistance=0;++configRevision;}
    }
    VRMath::Vector RocketSeat(float extraDepth=0)
    {
        auto point=profile.rocketEntrance;
        for(unsigned a=0;a<3;++a) point[a]+=profile.axis[a]*(profile.travel+extraDepth)-rocketTail[a];
        return point;
    }
    VRMath::Vector InsertionTip()
    {
        if(currentType!=18) return profile.tip;
        auto point=RocketSeat();
        for(unsigned a=0;a<3;++a) point[a]+=rocketTail[a];
        return point;
    }
    VRMath::Matrix RotationXYZ(const float (&degrees)[3])
    {
        auto rotation=VRMath::Identity();
        for(unsigned a=0;a<3;++a)
        {
            const float half=degrees[a]*.00872664626f;
            float position[3]{},q[4]{0,0,0,std::cos(half)};q[a]=std::sin(half);
            rotation=VRMath::Multiply(rotation,VRMath::Pose(position,q));
        }
        return rotation;
    }
    VRMath::Matrix HeldMagazine(const VRMath::Matrix& gun,const Sample& view)
    {
        const auto rotation=RotationXYZ(profile.handRotation);
        // Use the authored weapon attachment, not a live gun transform sampled
        // before its attachment update divided by this frame's right wrist.
        // That mixed two pose times when acquiring ammo while running.
        const auto gunInWrist=primaryGripBone<boneCount ? VRMath::InverseRigid(rest[primaryGripBone]):
            VRMath::Multiply(gun,VRMath::InverseRigid(view.wrist[1]));
        auto root=VRMath::Multiply(rotation,VRMath::Multiply(gunInWrist,view.hand[0]));
        const auto center=VRMath::Transform(currentType==18 ? RocketSeat():rest[magazineBone][3],root);
        for(unsigned a=0;a<3;++a)
        {
            float offset=0;for(unsigned row=0;row<3;++row) offset+=view.hand[0][row][a]*profile.handOffset[row];
            root[3][a]+=view.hand[0][3][a]+offset-center[a];
        }
        return root;
    }

    void* ObjectEntity(Object handle)
    {
        auto* pool=CObject::GetObjectsPool();const int index=handle>>8;
        if(!handle || !pool || index<0 || index>=pool->GetSize() || !pool->m_aStorage || !pool->m_aFlags) return nullptr;
        auto* storage=pool->GetAt(handle);
        return storage && pool->m_nStorageSize==4 ? *reinterpret_cast<void**>(storage):storage;
    }
    Object WeaponObjectHandle(void* entity)
    {
        auto* pool=CObject::GetObjectsPool();
        if(!entity || !pool || !pool->m_aStorage || !pool->m_aFlags ||
            pool->GetSize()<=0 || pool->GetSize()>65536 ||
            pool->m_nStorageSize<4 || pool->m_nStorageSize>16384) return 0;
        for(int slot=0;slot<pool->GetSize();++slot)
        {
            auto* storage=pool->GetSlot(slot);
            if(!storage) continue;
            auto* candidate=pool->m_nStorageSize==4 ? *reinterpret_cast<void**>(storage):storage;
            if(candidate==entity) return pool->GetIndex(storage);
        }
        return 0;
    }
    void PlayFireAnimation(void* entity=nullptr,unsigned weaponType=0)
    {
        const auto object=WeaponObjectHandle(entity ? entity:gunEntity);
        const unsigned type=weaponType ? weaponType:currentType.load();
        const char* dictionary=type==10 ? "gun@shotgun":(type==11 ? "gun@baretta":((type==16 || type==17) ? "gun@rifle":((type==14 || type==15) ? "gun@ak47":(type==9 ? "gun@deagle":(type==12 ? "gun@uzi":(type==13 ? "gun@mp5k":"gun@handgun"))))));
        // Play only the weapon object's authored clip, never a ped fire task.
        // Missing animation assets must not prevent an otherwise valid shot.
        if(!object) {fireAnimationError=1;VRDiagnostics::Increment(fireAnimationMisses);return;}
        if(!Natives::HaveAnimsLoaded(const_cast<char*>(dictionary)))
        {Natives::RequestAnims(const_cast<char*>(dictionary));fireAnimationError=2;VRDiagnostics::Increment(fireAnimationMisses);return;}
        if(Natives::PlayObjectAnim(object,const_cast<char*>("weapon_fire"),const_cast<char*>(dictionary),1000.f,false,false))
            {fireAnimationError=0;VRDiagnostics::Increment(fireAnimations);}
        else {fireAnimationError=3;VRDiagnostics::Increment(fireAnimationMisses);}
    }
    void* Equipped(void* ped,void*& entity)
    {
        auto* manager=static_cast<char*>(ped)+0x2B0;
        const int slot=*reinterpret_cast<int*>(manager);
        entity=*reinterpret_cast<void**>(manager+0x14);
        if(!entity || slot<0 || slot>10) return nullptr;
        auto* weapon=*reinterpret_cast<void**>(static_cast<char*>(entity)+0x25C);
        if(!weapon) return nullptr;
        const auto type=*reinterpret_cast<unsigned*>(static_cast<char*>(weapon)+0x18);
        return (type==7 || type==9 || type==10 || type==11 || type==12 || type==13 || type==14 || type==15 || type==16 || type==17 || type==18) && type==*reinterpret_cast<unsigned*>(manager+12*(slot+3)) ? weapon:nullptr;
    }
    bool ResolveRig(void* entity)
    {
        auto* draw=*reinterpret_cast<void**>(static_cast<char*>(entity)+0x34);
        auto* drawable=draw ? *reinterpret_cast<void**>(draw):nullptr;
        auto* def=drawable ? *reinterpret_cast<void**>(static_cast<char*>(drawable)+0xC):nullptr;
        if(!def) return false;
        if(def==definition) return magazineBone<boneCount && (currentType==18 || slideBone<boneCount);
        definition=def;magazineBone=shellSecondBone=slideBone=rackChildBone=primaryGripBone=~0u;
        boneCount=*reinterpret_cast<unsigned short*>(static_cast<char*>(def)+0x14);
        if(!boneCount || boneCount>32) return false;
        auto* bones=*reinterpret_cast<char**>(def);
        auto* locals=*reinterpret_cast<VRMath::Matrix**>(static_cast<char*>(def)+0x10);
        if(!bones || !locals) return false;
        bool resolved[32]{};
        for(unsigned pass=0;pass<boneCount;++pass) for(unsigned b=0;b<boneCount;++b)
        {
            if(resolved[b]) continue;
            auto* parent=*reinterpret_cast<char**>(bones+b*0xE0+0x10);
            if(parent)
            {
                const auto offset=parent-bones;
                if(offset<0 || offset%0xE0 || static_cast<unsigned>(offset/0xE0)>=boneCount) return false;
                const unsigned p=static_cast<unsigned>(offset/0xE0);
                if(!resolved[p]) continue;
                rest[b]=VRMath::Multiply(VRMath::AffineXYZ(locals[b]),rest[p]);
            }
            else rest[b]=VRMath::AffineXYZ(locals[b]);
            resolved[b]=true;
            auto* name=*reinterpret_cast<const char**>(bones+b*0xE0);
            if(name && !std::strcmp(name,(currentType==10 || currentType==11) ? "Gun_Shell1":(currentType==18 ? "gun_rocket":(currentType==16 ? "PSG1_Mag":(currentType==15 ? "M4_Mag":((currentType==12 || currentType==13) ? "mag":"Mag")))))) magazineBone=b;
            // Animated combat pistol uses Cock1 for the hammer; Breach is
            // the slide. The Glock and M4 retain their accepted rack bones.
            const char* rackBone=currentType==10 ? "gun_grip2":(currentType==11 ? "Gun_Breach":(currentType==16 ? "PSG1_Gun_Cock1":(currentType==15 ? "M4_Gun_Cock1":((currentType==9 || currentType==14) ? "Gun_Breach":"Gun_Cock1"))));
            if(name && !std::strcmp(name,rackBone)) slideBone=b;
            if(name && ((currentType==12 && !std::strcmp(name,"Gun_Breach")) || (currentType==17 && !std::strcmp(name,"Breach")))) rackChildBone=b;
            if(name && !std::strcmp(name,"gun_grip")) primaryGripBone=b;
            if((currentType==10 || currentType==11) && name && !std::strcmp(name,"Gun_Shell2")) shellSecondBone=b;
        }
        for(unsigned b=0;b<boneCount;++b) if(!resolved[b]) return false;
        if(currentType==18 && magazineBone<boneCount) rest[magazineBone][3]=profile.tip;
        return magazineBone<boneCount && (currentType==18 || slideBone<boneCount);
    }
    void __fastcall UpdatePose(void* entity,void*)
    {
        poseHook.thiscall<void>(entity);
        if(currentType==18)
        {
            return;
        }
        const bool gun=entity==visualGun.load();
        const bool mag=entity==visualMagazine.load();
        if((!gun && !mag) || !definition || boneCount>32) return;
        // Verified CE CObject instance: +100 skeleton, +14 world matrices.
        // Never write shared drawable/rest matrices or an NPC weapon pose.
        auto* skeleton=*reinterpret_cast<void**>(static_cast<char*>(entity)+0x100);
        auto* root=*reinterpret_cast<VRMath::Matrix**>(static_cast<char*>(entity)+0x20);
        if(!skeleton || !root || *reinterpret_cast<void**>(static_cast<char*>(skeleton)+4)!=definition ||
            *reinterpret_cast<unsigned short*>(static_cast<char*>(skeleton)+0x10)!=boneCount) return;
        auto* matrices=*reinterpret_cast<VRMath::Matrix**>(static_cast<char*>(skeleton)+0x14);
        if(!matrices) return;
        for(unsigned b=0;b<boneCount;++b)
        {
            const bool rackPart=b==slideBone || ((currentType==12 || currentType==17) && b==rackChildBone);
            if(gun && b!=magazineBone && !rackPart && !((currentType==10 || currentType==11) && b==shellSecondBone)) continue;
            // Native weapon_fire owns the animated slide/bolt between shots.
            // Manual rack takes ownership only during the physical interaction.
            if(gun && rackPart && !racking) continue;
            auto desired=VRMath::Multiply(rest[b],VRMath::AffineXYZ(*root));
            if((mag && b!=magazineBone) || (gun && b==magazineBone && (!inserted || currentType==10 || currentType==11)) || (gun && (currentType==10 || currentType==11) && b==shellSecondBone))
            {
                // Collapse unwanted geometry in this instance's skin palette.
                for(unsigned row=0;row<3;++row) for(unsigned a=0;a<3;++a) desired[row][a]=0;
                if(mag) desired[3]=VRMath::Transform(rest[magazineBone][3],VRMath::AffineXYZ(*root));
            }
            if(gun && rackPart)
                for(unsigned a=0;a<3;++a) desired[3][a]-=(*root)[0][a]*slideDistance.load();
            for(unsigned row=0;row<4;++row) for(unsigned a=0;a<3;++a) matrices[b][row][a]=desired[row][a];
        }
        VRDiagnostics::Increment(poseUpdates);
    }
    void ApplyMagazinePose(void* skeleton)
    {
        if(currentType==18) return;
        auto* entity=visualMagazine.load();
        if(!entity || *reinterpret_cast<void**>(static_cast<char*>(entity)+0x100)!=skeleton || !definition ||
            *reinterpret_cast<void**>(static_cast<char*>(skeleton)+4)!=definition ||
            *reinterpret_cast<unsigned short*>(static_cast<char*>(skeleton)+0x10)!=boneCount) return;
        auto* root=*reinterpret_cast<VRMath::Matrix**>(static_cast<char*>(skeleton)+8);
        auto* matrices=*reinterpret_cast<VRMath::Matrix**>(static_cast<char*>(skeleton)+0x14);
        if(!root || !matrices || magazineBone>=boneCount || boneCount>32) return;
        const auto magazine=VRMath::Multiply(rest[magazineBone],VRMath::AffineXYZ(*root));
        for(unsigned b=0;b<boneCount;++b)
            for(unsigned row=0;row<4;++row) for(unsigned a=0;a<3;++a)
                matrices[b][row][a]=(b!=magazineBone && row<3) ? 0.f:magazine[row][a];
        *reinterpret_cast<unsigned char*>(static_cast<char*>(entity)+0xF4)|=0x28;
        VRDiagnostics::Increment(magazineUpdates);
    }
    void __fastcall CalculateWorldPose(void* skeleton,void*)
    {
        worldPoseHook.thiscall<void>(skeleton);
        ApplyMagazinePose(skeleton);
    }
    bool __fastcall CanUseGunAction(void* task,void*,void* ped,int action,void* a,void* b,void* c)
    {
        void* entity=nullptr;
        // CFFDD0 action7 asks to play a timed reload even when weapon state4
        // is set. Blocking only completion leaves that animation looping.
        if(ped==owner && action==7 && guardedWeapon && Equipped(ped,entity)==guardedWeapon.load())
        {VRDiagnostics::Increment(blockedReloads);return false;}
        return decisionHook.thiscall<bool>(task,ped,action,a,b,c);
    }
    bool __fastcall ProcessReloadTask(void* task,void*,void* ped)
    {
        void* entity=nullptr;
        if(ped==owner && guardedWeapon && Equipped(ped,entity)==guardedWeapon.load())
        {
            // TaskSimpleReloadGun starts its animation independently of the
            // generic gun-action decision. Finish only this player's task.
            if(*reinterpret_cast<void**>(static_cast<char*>(task)+0x48)) finishReloadTask(task,ped);
            VRDiagnostics::Increment(blockedTaskReloads);return true;
        }
        return taskHook.thiscall<bool>(task,ped);
    }
    void __fastcall CompleteReload(void* weapon,void*,void* ped)
    {
        if(weapon==guardedWeapon.load()) return;
        completionHook.thiscall<void>(weapon,ped);
    }
    void __fastcall UpdateWeapon(void* weapon,void*,void* ped)
    {
        if(weapon==guardedWeapon.load())
        {
            auto& state=*reinterpret_cast<unsigned*>(static_cast<char*>(weapon)+0x1C);
            // Readiness belongs to our chamber guard. Native empty/reload
            // states ask the gun task to reload and play empty/slide sounds.
            // Preserve state1 only while its shot cadence is still meaningful.
            if(state!=1 || !*reinterpret_cast<unsigned*>(static_cast<char*>(weapon)+0x5C))
            {state=0;return;}
        }
        reloadHook.thiscall<void>(weapon,ped);
    }
    void DeleteMagazine()
    {
        visualMagazine=nullptr;magazineEntity=nullptr;
        if(magazineObject && Natives::DoesObjectExist(magazineObject)) Natives::DeleteObject(&magazineObject);
        magazineObject=0;dropped=false;magazineRoom=0;guide={};insertionProgress=0;
    }
    void MoveMagazine(const VRMath::Matrix& root)
    {
        if(!magazineEntity || !magazineObject) return;
        Ped ped=0;uint32_t room=0;
        Natives::GetPlayerChar(Natives::GetPlayerId(),&ped);
        if(ped) Natives::GetKeyForCharInRoom(ped,&room);
        if(room!=magazineRoom.load())
        {
            if(room) Natives::AddObjectToInteriorRoomByKey(magazineObject,room);
            else Natives::ClearRoomForObject(magazineObject);
            magazineRoom=room;
        }
        Natives::SetObjectCoordinates(magazineObject,root[3][0],root[3][1],root[3][2]);
        auto* frame=*reinterpret_cast<VRMath::Matrix**>(static_cast<char*>(magazineEntity)+0x20);
        if(!frame) return;
        if(currentType==18)
        {
            // Native ROCKET asset is rotated -90 degrees about Z in weaponInfo.
            const float position[3]{},q[4]{0,0,-.70710678f,.70710678f};
            auto rocket=VRMath::Multiply(VRMath::Pose(position,q),root);
            rocket[3]=VRMath::Transform(RocketSeat(ammo.Ready() && !ammo.holding ? profile.rocketLoadedDepth:0.f),root);*frame=rocket;
        }
        else *frame=root;
        auto* skeleton=*reinterpret_cast<void**>(static_cast<char*>(magazineEntity)+0x100);
        if(skeleton) CalculateWorldPose(skeleton,nullptr);
    }
    bool MakeMagazine(const VRMath::Matrix& root)
    {
        DeleteMagazine();uint32_t model=0;Natives::GetWeapontypeModel(currentType==18 ? 6u:currentType.load(),&model);
        if(!model) return false;
        if(!Natives::HasModelLoaded(model)) {Natives::RequestModel(model);return false;}
        if(currentType==18)
        {
            Vector3 minimum{},maximum{};Natives::GetModelDimensions(model,&minimum,&maximum);
            // Native projectile +Y becomes launcher +X after its -90deg Z
            // rotation. Its rear end, not its origin or nose, enters the tube.
            rocketTail={minimum.fY,-(minimum.fX+maximum.fX)*.5f,(minimum.fZ+maximum.fZ)*.5f,0};
            if(!std::isfinite(rocketTail[0]+rocketTail[1]+rocketTail[2]) ||
                VRMath::Length3(rocketTail)>2.f) return false;
        }
        Natives::CreateObjectNoOffset(model,root[3][0],root[3][1],root[3][2],&magazineObject,true);
        magazineEntity=ObjectEntity(magazineObject);
        if(!magazineEntity) {DeleteMagazine();return false;}
        Natives::SetObjectCollision(magazineObject,false);
        Natives::SetObjectDynamic(magazineObject,false);
        Natives::FreezeObjectPosition(magazineObject,true);
        if(currentType!=18 && !*reinterpret_cast<void**>(static_cast<char*>(magazineEntity)+0x100)) allocatePose(magazineEntity);
        auto* frame=*reinterpret_cast<VRMath::Matrix**>(static_cast<char*>(magazineEntity)+0x20);
        if(!frame || (currentType!=18 && !*reinterpret_cast<void**>(static_cast<char*>(magazineEntity)+0x100))) {DeleteMagazine();return false;}
        visualMagazine=magazineEntity;MoveMagazine(root);
        return true;
    }
    void WriteAmmo(Ped ped)
    {
        if(reportedClip.load()!=ammo.Clip() || reportedReserve.load()!=ammo.reserve)VRHud::AmmoChanged();
        Natives::SetAmmoInClip(ped,currentType.load(),ammo.Clip());
        // CE SET_AMMO_IN_CLIP also adds (newClip-oldClip) to total ammo.
        // Write the authoritative total last, or a rack/ejection/insert gets
        // reconciled as an extra reserve change on the following update.
        // Native inventory owns held ammunition too, even though the storage
        // display and clip both exclude it. Otherwise its last-magazine
        // transfer appears as total exhaustion to vanilla weapon selection.
        Natives::SetCharAmmo(ped,currentType.load(),ammo.Owned());
        previousNativeTotal=ammo.Owned();
        chamberReady=ammo.Ready();inserted=ammo.inserted;
        reportedClip=ammo.Clip();reportedReserve=ammo.reserve;reportedHeld=ammo.held;reportedDiscarded=ammo.discarded;
        if(currentWeapon)
        {
            auto& state=*reinterpret_cast<unsigned*>(static_cast<char*>(currentWeapon)+0x1C);
            if(state!=1) state=0;
        }
    }
    void Process()
    {
        SaveCounter();
        if(configPending.exchange(false)) LoadProfile();
        if(enabled && OpenXRVR::IsSessionRunning())
        {
            Ped local=0;Natives::GetPlayerChar(Natives::GetPlayerId(),&local);
            vehicleMode=local && Natives::DoesCharExist(local) && !Natives::IsCharDead(local) && Natives::IsCharInAnyCar(local);
        }
        else vehicleMode=false;
        if(enabled && vehicleMode.load() && OpenXRVR::IsSessionRunning())
        {
            // First drive-by pass uses the game's clip/reload controller.
            // Transfer the actual partial clip, never create ammunition.
            Ped ped=0;Natives::GetPlayerChar(Natives::GetPlayerId(),&ped);
            if(initialized && ped && Natives::DoesCharExist(ped) && !Natives::IsCharDead(ped))
            {
                ammo.ReturnHeld();currentWeapon=nullptr;
                if(Natives::HasCharGotWeapon(ped,currentType.load())) WriteAmmo(ped);
            }
            counterEditing=false;initialized=false;guardedWeapon=nullptr;nativeRocketVisual=nullptr;visualGun=nullptr;currentWeapon=nullptr;
            racking=false;slideDistance=0;leftBusy=false;
            if(magazineObject) DeleteMagazine();
            void* entity=nullptr;void* local=FindPlayerPed ? FindPlayerPed(0):nullptr;
            void* weapon=local ? Equipped(local,entity):nullptr;
            if(weapon) savedAmmo[AmmoIndex(*reinterpret_cast<unsigned*>(static_cast<char*>(weapon)+0x18))]={};
            return;
        }
        if(enabled && initialized && !OpenXRVR::IsSessionRunning())
        {
            // Overlay focus loss is not session loss. Only a stopped session
            // hands ammunition and reloading back to the ordinary game.
            Ped ped=0;Natives::GetPlayerChar(Natives::GetPlayerId(),&ped);
            currentWeapon=nullptr; // Inventory may have changed during session loss.
            if(ped && Natives::DoesCharExist(ped) && !Natives::IsCharDead(ped))
            {ammo.ReturnHeld();WriteAmmo(ped);}
            guardedWeapon=nullptr;nativeRocketVisual=nullptr;visualGun=nullptr;currentWeapon=nullptr;
            initialized=false;racking=false;slideDistance=0;leftBusy=false;DeleteMagazine();
        }
        Sample view;AcquireSRWLockShared(&poseLock);view=sample;ReleaseSRWLockShared(&poseLock);
        if(!enabled || !OpenXRVR::IsSessionRunning() || !view.owner || GetTickCount64()-view.at>250) return;
        Ped ped=0;Natives::GetPlayerChar(Natives::GetPlayerId(),&ped);
        if(!ped || !Natives::DoesCharExist(ped) || Natives::IsCharDead(ped))
        {guardedWeapon=nullptr;nativeRocketVisual=nullptr;visualGun=nullptr;currentWeapon=nullptr;leftBusy=false;initialized=false;
            for(auto& saved:savedAmmo) saved={};DeleteMagazine();return;}
        if(owner!=view.owner)
        {DeleteMagazine();initialized=false;owner=view.owner;guardedWeapon=nullptr;for(auto& saved:savedAmmo) saved={};}
        void* entity=nullptr;void* weapon=Equipped(view.owner,entity);
        const unsigned type=weapon ? *reinterpret_cast<unsigned*>(static_cast<char*>(weapon)+0x18):0;
        if(initialized && (!weapon || type!=currentType))
        {
            // Return only the held reserve magazine to its original weapon.
            // Preserve absent magazines/chambers across inventory switches.
            // Do not dereference the previous CWeapon: selection can destroy it.
            guardedWeapon=nullptr;nativeRocketVisual=nullptr;visualGun=nullptr;currentWeapon=nullptr;
            ammo.ReturnHeld();
            if(Natives::HasCharGotWeapon(ped,currentType.load()))
            {
                Natives::SetAmmoInClip(ped,currentType.load(),ammo.Clip());
                Natives::SetCharAmmo(ped,currentType.load(),ammo.Owned());
                savedAmmo[AmmoIndex(currentType)]={ammo,true};
            }
            initialized=false;racking=false;slideDistance=0;leftBusy=false;DeleteMagazine();
        }
        if(weapon && type!=currentType)
        {SaveCounter(true);counterEditing=false;currentType=type;definition=nullptr;profile=Defaults(type);LoadProfile();}
        if(!weapon || !ResolveRig(entity))
        {
            counterEditing=false;guardedWeapon=nullptr;nativeRocketVisual=nullptr;visualGun=nullptr;currentWeapon=nullptr;racking=false;leftBusy=false;guide={};
            return;
        }
        currentWeapon=weapon;gunEntity=entity;
        unsigned total=0,clip=0,capacity=0;Natives::GetAmmoInCharWeapon(ped,type,&total);
        if(!initialized)
        {
            Natives::GetAmmoInClip(ped,type,&clip);Natives::GetMaxAmmoInClip(ped,type,&capacity);
            if(capacity<(type==18 ? 1u:2u) || capacity>64 || total>100000) return;
            auto& saved=savedAmmo[AmmoIndex(type)];
            if(saved.valid)
            {
                ammo=saved.ammo;
                if(!ammo.Resume(total,capacity)) ammo.Begin(clip,total,capacity);
            }
            else ammo.Begin(clip,total,capacity);
            ammo.tube=type==10 || type==11;ammo.pump=type==10 || type==17;
            initialized=true;previousNativeTotal=total;rightGrip=leftGrip=true;
        }
        else if(total>previousNativeTotal) ammo.reserve+=total-previousNativeTotal;
        else if(total<previousNativeTotal) ammo.reserve-=std::min(ammo.reserve,previousNativeTotal-total);
        guardedWeapon=weapon;visualGun=entity;WriteAmmo(ped);
        nativeRocketVisual=nullptr;
        if(type==18)
        {
            // CE weapon manager+1C owns a separate native held projectile.
            // Live type18 readback: this object is model type6, parent is the
            // player ped and +25C has no CWeapon. Keep it alive for native
            // lifecycle; the physical reload adapter owns its replacement.
            auto* rocket=*reinterpret_cast<void**>(static_cast<char*>(view.owner)+0x2B0+0x1C);
            if(rocket && rocket!=entity &&
                *reinterpret_cast<void**>(static_cast<char*>(rocket)+0x1BC)==view.owner &&
                !*reinterpret_cast<void**>(static_cast<char*>(rocket)+0x25C)) nativeRocketVisual=rocket;
        }
        OpenXRVR::ControllerInput input{};
        if(!OpenXRVR::HasFocusedViews() || !OpenXRVR::GetControllerInput(input) || input.phoneActive ||
            Natives::IsPauseMenuActive() || !input.gripPoseValid[0] || !input.gripPoseValid[1])
        {counterEditing=false;rightGrip=leftGrip=true;racking=false;slideDistance=0;leftBusy=false;guide={};return;}
        if(fireAnimationTest.exchange(false) && !racking) PlayFireAnimation();
        auto* frame=*reinterpret_cast<VRMath::Matrix**>(static_cast<char*>(entity)+0x20);
        if(!frame) return;
        const auto gun=VRMath::AffineXYZ(*frame);
        if(type==18)
        {
            // Load the projectile asset before a shoulder press, and show the
            // loaded round without relying on the suppressed native reload task.
            uint32_t model=0;Natives::GetWeapontypeModel(6,&model);
            if(model && !Natives::HasModelLoaded(model)) Natives::RequestModel(model);
            if(ammo.Ready()) {if(!magazineEntity) MakeMagazine(gun);if(magazineEntity) MoveMagazine(gun);}
            else if(magazineEntity && !ammo.holding && !dropped) DeleteMagazine();
        }
        const bool right=input.grabActive[1] && input.grab[1];
        const bool left=input.grabActive[0] && input.grab[0];
        const bool showCounter=VRConfig::Read().Get(VRConfig::Flag::AmmoCounters);
        if(!left || !profile.counter.editWithOffHand || !showCounter) counterEditing=false;
        if(left && showCounter && profile.counter.enabled && profile.counter.editWithOffHand && !ammo.holding &&
            (counterEditing || VRAmmoCounter::Near(profile.counter,gun,view.wrist[0][3])))
        {
            const auto hand=VRMath::Multiply(view.wrist[0],VRMath::InverseRigid(gun));
            const bool resize=input.triggerActive[0] && input.trigger[0]>.5f;
            if(!counterEditing || resize!=counterEdit.resizing)
            {
                if(!counterEditing) SaveCounter(true);
                counterEdit.hand=hand;counterEdit.panel=VRAmmoCounter::Frame(profile.counter);
                counterEdit.width=profile.counter.width;counterEdit.height=profile.counter.height;
                counterEdit.distance=VRMath::Length3(hand[3]);counterEdit.resizing=resize;
                counterEditing=true;racking=false;slideDistance=0;guide={};
            }
            auto next=profile.counter;
            if(resize)
            {
                if(counterEdit.distance>1.e-5f)
                {
                    const float scale=VRMath::Length3(hand[3])/counterEdit.distance;
                    if(scale>0 && std::isfinite(scale)) {next.width=counterEdit.width*scale;next.height=counterEdit.height*scale;}
                }
            }
            else VRAmmoCounter::SetFrame(next,VRMath::Multiply(VRMath::Multiply(counterEdit.panel,VRMath::InverseRigid(counterEdit.hand)),hand));
            bool changed=std::abs(next.width-profile.counter.width)>1.e-5f || std::abs(next.height-profile.counter.height)>1.e-5f;
            for(unsigned a=0;a<3;++a) changed|=std::abs(next.offset[a]-profile.counter.offset[a])>1.e-5f ||
                std::abs(next.rotation[a]-profile.counter.rotation[a])>.01f;
            if(changed && std::isfinite(next.width+next.height) && next.width>0 && next.height>0)
            {
                profile.counter=next;counterEdit.pending=next;counterEdit.type=type;
                counterEdit.changedAt=GetTickCount64();counterSavePending=true;
            }
            leftBusy=true;leftGrip=left;rightGrip=right;WriteAmmo(ped);return;
        }
        if(type!=18 && type!=10 && type!=11 && right && !rightGrip && ammo.inserted)
        {
            ammo.Eject();VRDiagnostics::Increment(ejections);
            if(!ammo.holding && MakeMagazine(gun)) {dropped=true;droppedRoot=gun;dropVelocity=0;dropAt=GetTickCount64();}
        }
        // Shoulder is defined relative to the live HMD, not startup height.
        OpenXRVR::EyeView eyes[2]{};long long display=0;
        if(left && !leftGrip && !ammo.holding && !racking && !(type==18 && ammo.Ready()) && OpenXRVR::GetEyeViews(eyes,display))
        {
            const VRMath::Vector head{(eyes[0].position[0]+eyes[1].position[0])*.5f,
                (eyes[0].position[1]+eyes[1].position[1])*.5f,(eyes[0].position[2]+eyes[1].position[2])*.5f,1};
            const auto leftTracking=VRMath::Transform(view.wrist[0][3],VRMath::InverseRigid(view.transform));
            auto delta=VRMath::Difference3(leftTracking,head);
            const auto headPose=VRMath::Pose(eyes[0].position,eyes[0].orientation);
            const float behind=VRMath::Dot3(delta,headPose[2]);
            if(VRMath::Length3(delta)<.60f && behind>.10f && leftTracking[1]>head[1]-.35f && ammo.reserve)
            {
                const auto root=HeldMagazine(gun,view);
                if(MakeMagazine(root) && ammo.TakeMagazine()) magazineOffset=VRGrip::Capture(root,view.hand[0]);
            }
        }
        if(ammo.holding && magazineEntity)
        {
            if(refreshHeldPose) magazineOffset=VRGrip::Capture(HeldMagazine(gun,view),view.hand[0]);
            auto root=VRGrip::Target(magazineOffset,view.hand[0]);
            // The leading end meets the bottom opening; do not snap the
            // magazine's center to a sphere around the pistol's whole grip.
            const auto insertionTip=InsertionTip();
            const auto tip=VRMath::Transform(insertionTip,root);
            auto well=VRMath::Transform((type==10 || type==11) ? profile.shellPort:insertionTip,gun);
            const auto axis=VRMath::Transform(profile.axis,gun);
            if(type!=10 && type!=11) for(unsigned a=0;a<3;++a) well[a]-=axis[a]*profile.travel;
            const float distance=VRMath::Length3(VRMath::Difference3(tip,well));
            insertionDistance=distance;
            const auto local=VRMath::Transform(view.wrist[0][3],VRMath::InverseRigid(gun));
            const auto heldAxis=VRMath::Transform(profile.axis,root);
            const auto insertionRotation=RotationXYZ(profile.insertionRotation);
            const auto desiredAxis=(type==10 || type==11) ? VRMath::Transform(profile.axis,VRMath::Multiply(insertionRotation,gun)):axis;
            const bool aligned=VRMath::Dot3(heldAxis,desiredAxis)>profile.alignment &&
                (type==18 || type==10 || type==11 || VRMath::Dot3(root[1],gun[1])>0.f);
            insertionAligned=aligned;
            if((!ammo.inserted || ((type==10 || type==11) && ammo.Clip()<ammo.capacity)) && !guide.active && left && distance<profile.snap && aligned)
            {guide.Begin();guideStart=local;}
            if(guide.active)
            {
                const auto movement=VRMath::Difference3(local,guideStart);
                const float axial=VRMath::Dot3(movement,profile.axis);
                const float lateral=std::sqrt(std::max(0.f,VRMath::Dot3(movement,movement)-axial*axial));
                const bool seated=guide.Update(axial,lateral,profile.travel);
                if(guide.active)
                {
                    root=gun;
                    if(type==10 || type==11)
                    {
                        root=VRReload::ShellInsertionPose(gun,insertionRotation,insertionTip,
                            profile.shellPort,profile.axis,guide.progress);
                    }
                    else for(unsigned a=0;a<3;++a) root[3][a]-=axis[a]*(profile.travel-guide.progress);
                }
                insertionProgress=guide.progress;
                if(seated && left) {ammo.Insert();VRDiagnostics::Increment(insertions);DeleteMagazine();}
            }
            if(ammo.holding)
            {
                MoveMagazine(root);
                if(!left) {ammo.DropHeld();guide={};dropped=true;droppedRoot=root;dropVelocity=0;dropAt=GetTickCount64();}
            }
        }
        if(type!=18 && !ammo.holding && !racking && left && !leftGrip)
        {
            auto gripPoint=rest[slideBone][3];
            if(type==10 || type==11 || type==12 || type==13 || type==14 || type==15 || type==16 || type==17) for(unsigned a=0;a<3;++a) gripPoint[a]+=profile.rackGripOffset[a];
            const auto slide=VRMath::Transform(gripPoint,gun);
            if(VRMath::Length3(VRMath::Difference3(view.wrist[0][3],slide))<profile.rackRadius)
            {
                racking=true;pulled=false;pumpOpen=false;rackStart=VRMath::Transform(view.wrist[0][3],VRMath::InverseRigid(gun));
                rackHandLocal=VRReload::SlideHandPose(VRMath::Multiply(view.wrist[0],VRMath::InverseRigid(gun)),rest[slideBone][3]);
                if(type==10 || type==11 || type==12 || type==13 || type==14 || type==15 || type==16 || type==17)
                {
                    // Charging handle, rather than a palm over a pistol slide.
                    for(unsigned a=0;a<3;++a)
                        rackHandLocal[3][a]=gripPoint[a]-rackHandLocal[0][a]*.04f;
                }
            }
        }
        if(racking)
        {
            const auto local=VRMath::Transform(view.wrist[0][3],VRMath::InverseRigid(gun));
            slideDistance=std::clamp(rackStart[0]-local[0],0.f,profile.rackTravel);
            if(slideDistance>=profile.rackThreshold && !pulled)
            {pulled=true;PlayPistolSound("HANDGUN_RELOAD_COCK_PULL");VRDiagnostics::Increment(slidePullSounds);}
            if(type==10 || type==17)
            {
                pumpOpen=slideDistance>profile.rackReturnThreshold || pulled;
                if(left && pulled && slideDistance<=profile.rackReturnThreshold)
                {
                    ammo.Rack();VRDiagnostics::Increment(racks);pulled=false;pumpOpen=false;
                    PlayPistolSound("HANDGUN_RELOAD_COCK_RELEASE");VRDiagnostics::Increment(slideReleaseSounds);
                }
            }
            if(!left)
            {
                if(pulled && type!=10 && type!=17)
                {ammo.Rack();VRDiagnostics::Increment(racks);PlayPistolSound("HANDGUN_RELOAD_COCK_RELEASE");VRDiagnostics::Increment(slideReleaseSounds);}
                racking=false;slideDistance=0;
            }
        }
        if(dropped && magazineEntity)
        {
            float dt=0;Natives::GetFrameTime(&dt);dt=std::clamp(dt,0.f,.05f);
            dropVelocity+=9.81f*dt;droppedRoot[3][2]-=dropVelocity*dt;
            MoveMagazine(droppedRoot);
            if(GetTickCount64()-dropAt>1500) DeleteMagazine();
        }
        AcquireSRWLockExclusive(&poseLock);
        if(racking && primaryGripBone<boneCount)
            slideHand={owner,gunEntity,rackHandLocal,VRMath::InverseRigid(rest[primaryGripBone]),GetTickCount64()};
        else slideHand={};
        ReleaseSRWLockExclusive(&poseLock);
        refreshHeldPose=false;leftBusy=ammo.holding || racking;rightGrip=right;leftGrip=left;WriteAmmo(ped);
    }
}
void VRReloadRuntime::Publish(void* ped,const VRMath::Matrix& transform,const VRMath::Matrix (&tracked)[2],const VRMath::Matrix (&rendered)[2])
{
    if(!enabled) return;
    AcquireSRWLockExclusive(&poseLock);sample.owner=ped;sample.transform=transform;
    // Gestures need the physical controller trajectory even when the visible
    // hand is snapped to a slide/pump. Loose ammo follows the rendered hand.
    for(unsigned hand=0;hand<2;++hand) {sample.wrist[hand]=tracked[hand];sample.hand[hand]=rendered[hand];}
    sample.at=GetTickCount64();ReleaseSRWLockExclusive(&poseLock);
}
bool VRReloadRuntime::CanFire(void*,void* weapon)
{return weapon!=guardedWeapon.load() || (chamberReady && (!racking || ((currentType==10 || currentType==17) && !pumpOpen)));}
void VRReloadRuntime::EmptyTrigger(void* ped,void* weapon)
{
    if(ped!=owner || weapon!=guardedWeapon.load() || chamberReady || (racking && currentType!=10 && currentType!=17) || !gunEntity) return;
    // Native weapon-bank click identified in sounds.dat15. This is audio
    // only: never invoke Fire with an empty clip or start a reload task.
    PlayPistolSound("GENERAL_GUNS_AK47_DRY_CLICK");
    VRDiagnostics::Increment(emptyClicks);
}
void VRReloadRuntime::Fired(void* ped,void* weapon)
{
    if(ped && FindPlayerPed && ped==FindPlayerPed(0))VRHud::AmmoChanged();
    if(weapon!=guardedWeapon.load())
    {
        if(vehicleMode.load() && ped && FindPlayerPed && ped==FindPlayerPed(0))
        {
            void* entity=nullptr;
            if(Equipped(ped,entity)==weapon) PlayFireAnimation(entity,*reinterpret_cast<unsigned*>(static_cast<char*>(weapon)+0x18));
        }
        return;
    }
    ammo.Fired();previousNativeTotal=ammo.Owned();chamberReady=ammo.Ready();
    if(currentType!=18) PlayFireAnimation();
}
bool VRReloadRuntime::UsesLeftHand() {return leftBusy || counterEditing;}
bool VRReloadRuntime::CounterGrabPriority(void* ped,void* entity,const VRMath::Matrix& hand)
{
    if(!profile.counter.enabled || !profile.counter.editWithOffHand || ped!=owner || entity!=visualGun.load() || !entity ||
        !VRConfig::Read().Get(VRConfig::Flag::AmmoCounters)) return false;
    auto* root=*reinterpret_cast<VRMath::Matrix**>(static_cast<char*>(entity)+0x20);
    return counterEditing || (root && VRAmmoCounter::Near(profile.counter,VRMath::AffineXYZ(*root),hand[3]));
}
bool VRReloadRuntime::PumpHeld() {return guardedWeapon && currentType==10 && racking;}
bool VRReloadRuntime::HoldsMagazine() {return guardedWeapon && reportedHeld.load()>0;}
bool VRReloadRuntime::SlideGrip(void* ped,void* gun,VRMath::Matrix& relative)
{
    SlideHand pose;AcquireSRWLockShared(&poseLock);pose=slideHand;ReleaseSRWLockShared(&poseLock);
    if(!guardedWeapon || !racking || pose.owner!=ped || pose.gun!=gun || !pose.at || GetTickCount64()-pose.at>150) return false;
    pose.local[3][0]-=slideDistance.load();
    relative=VRMath::Multiply(pose.local,pose.gripInverse);
    return true;
}
bool VRReloadRuntime::ReloadConfig() {if(!enabled) return false;configPending=true;return true;}
bool VRReloadRuntime::TestFireAnimation() {if(!enabled) return false;fireAnimationTest=true;return true;}
unsigned VRReloadRuntime::ConfigRevision() {return configRevision.load();}

bool VRReloadRuntime::GetAmmoCounter(const void* entity,VRAmmoCounter::Snapshot& result)
{
    result={};
    if(!enabled || !guardedWeapon.load() || !entity ||
        entity!=visualGun.load() || !profile.counter.enabled || !OpenXRVR::HasFocusedViews() ||
        !VRConfig::Read().Get(VRConfig::Flag::AmmoCounters)) return false;
    auto* root=*reinterpret_cast<VRMath::Matrix* const*>(static_cast<const char*>(entity)+0x20);
    if(!root) return false;
    result.gun=VRMath::AffineXYZ(*root);
    if(!VRGrip::Rigid(result.gun)) return false;
    // Keep configured offsets in resting weapon coordinates, then apply the
    // actual animated slide delta (both manual rack and native shot animation).
    if((currentType==7 || currentType==9) && slideBone<boneCount)
    {
        auto* skeleton=*reinterpret_cast<void* const*>(static_cast<const char*>(entity)+0x100);
        if(skeleton && *reinterpret_cast<void**>(static_cast<char*>(skeleton)+4)==definition &&
            *reinterpret_cast<unsigned short*>(static_cast<char*>(skeleton)+0x10)==boneCount)
        {
            auto* matrices=*reinterpret_cast<VRMath::Matrix**>(static_cast<char*>(skeleton)+0x14);
            if(matrices)
            {
                auto slide=VRMath::AffineXYZ(matrices[slideBone]);
                auto restingSlide=VRMath::Multiply(rest[slideBone],result.gun);
                if(VRGrip::Rigid(slide) && VRGrip::Rigid(restingSlide))
                    result.gun=VRMath::Multiply(VRMath::Multiply(result.gun,VRMath::InverseRigid(restingSlide)),slide);
            }
        }
    }
    result.settings=profile.counter;result.rounds=ammo.Clip();result.capacity=ammo.capacity;
    result.total=ammo.reserve;
    result.valid=result.capacity>0;return result.valid;
}

const char* VRReloadRuntime::ScopeProfileFile()
{return currentType==17 ? "VRWeapons/sniper.ini":"VRWeapons/psg1.ini";}
bool VRReloadRuntime::GetRenderedSniperPose(VRMath::Matrix& world)
{
    // Called by the native world-list builder. Snapshot the actual equipped
    // object independently of manual-reload/hand tracking ownership.
    scopeGun=nullptr;
    if(!enabled || !FindPlayerPed || !OpenXRVR::HasFocusedViews()) return false;
    void* ped=FindPlayerPed(0);void* entity=nullptr;
    void* weapon=ped ? Equipped(ped,entity):nullptr;
    if(!weapon) return false;
    const auto type=*reinterpret_cast<unsigned*>(static_cast<char*>(weapon)+0x18);
    if(type!=16 && type!=17) return false;
    auto* root=*reinterpret_cast<VRMath::Matrix**>(static_cast<char*>(entity)+0x20);
    if(!root) return false;
    world=VRMath::AffineXYZ(*root);
    if(!VRGrip::Rigid(world)) return false;
    scopeGun=entity;return true;
}
bool VRReloadRuntime::IsScopeOccluder(const void* entity)
{return entity && (entity==scopeGun.load() || entity==visualMagazine.load());}
bool VRReloadRuntime::IsScopeGun(const void* entity)
{return entity && entity==scopeGun.load();}
void VRReloadRuntime::PrepareObjectRender(void* entity)
{
    // The simulation callback owns creation/ammo/rooms. Only refresh the
    // loaded visual's frame here, immediately before its draw is queued:
    // the native weapon attachment may have moved since Process().
    if(!entity || currentType!=18 || !guardedWeapon.load() ||
        entity!=visualMagazine.load() || !chamberReady.load() || reportedHeld.load()) return;
    auto* gun=visualGun.load();
    if(!gun) return;
    auto* gunFrame=*reinterpret_cast<VRMath::Matrix**>(static_cast<char*>(gun)+0x20);
    auto* rocketFrame=*reinterpret_cast<VRMath::Matrix**>(static_cast<char*>(entity)+0x20);
    if(!gunFrame || !rocketFrame) return;
    auto root=VRMath::AffineXYZ(*gunFrame);
    if(!VRGrip::Rigid(root)) return;
    const float position[3]{},q[4]{0,0,-.70710678f,.70710678f};
    auto rocket=VRMath::Multiply(VRMath::Pose(position,q),root);
    rocket[3]=VRMath::Transform(RocketSeat(profile.rocketLoadedDepth),root);
    *rocketFrame=rocket;
    VRDiagnostics::Increment(rocketRenderUpdates);
}
bool VRReloadRuntime::HideNativeRocket(const void* entity)
{
    if(entity && currentType==18 && guardedWeapon.load() && entity==nativeRocketVisual.load())
    {VRDiagnostics::Increment(nativeRocketSkips);return true;}
    return false;
}

std::array<float,4> VRReloadRuntime::RecoilSettings(unsigned type)
{
    if(type!=9 && type!=10 && type!=11 && type!=12 && type!=13 && type!=14 && type!=15 && type!=16 && type!=17) return {};
    if(type!=currentType || !enabled) {const auto defaults=Defaults(type);return {defaults.recoilKick,defaults.recoilSupportScale,defaults.recoilRecovery,defaults.recoilMaximum};}
    return {recoilKick.load(),recoilSupportScale.load(),recoilRecovery.load(),recoilMaximum.load()};
}
std::array<float,3> VRReloadRuntime::SupportRotation(unsigned type)
{
    const auto value=type==currentType && enabled ? profile:Defaults(type);
    return {value.supportRotation[0],value.supportRotation[1],value.supportRotation[2]};
}
std::array<float,3> VRReloadRuntime::SupportOffset(unsigned type)
{
    const auto value=type==currentType && enabled ? profile:Defaults(type);
    return {value.supportOffset[0],value.supportOffset[1],value.supportOffset[2]};
}
std::string VRReloadRuntime::Status()
{
    return "{\"enabled\":"+std::string(enabled ? "true":"false")+",\"active\":"+(guardedWeapon ? "true":"false")+
        ",\"assetReady\":"+(definition && magazineBone<boneCount && (currentType==18 || slideBone<boneCount) ? "true":"false")+
        ",\"chambered\":"+(chamberReady ? "true":"false")+",\"magazineInserted\":"+(inserted ? "true":"false")+
        ",\"configRevision\":"+std::to_string(configRevision.load())+",\"configValid\":"+(configValid ? "true":"false")+
        ",\"configPending\":"+(configPending ? "true":"false")+
        ",\"emptyClicks\":"+std::to_string(emptyClicks.load())+
        ",\"slidePullSounds\":"+std::to_string(slidePullSounds.load())+",\"slideReleaseSounds\":"+std::to_string(slideReleaseSounds.load())+
        ",\"weaponType\":"+std::to_string(currentType.load())+
        ",\"fireAnimations\":"+std::to_string(fireAnimations.load())+",\"fireAnimationMisses\":"+std::to_string(fireAnimationMisses.load())+
        ",\"fireAnimationError\":"+std::to_string(fireAnimationError.load())+
        ",\"configFile\":\""+ProfileFile(currentType.load())+"\",\"clip\":"+std::to_string(reportedClip.load())+",\"reserve\":"+std::to_string(reportedReserve.load())+
        ",\"heldRounds\":"+std::to_string(reportedHeld.load())+",\"discarded\":"+std::to_string(reportedDiscarded.load())+
        ",\"counterEditing\":"+(counterEditing ? "true":"false")+",\"counterSavePending\":"+(counterSavePending ? "true":"false")+
        ",\"counterSaveOk\":"+(counterSaveOk ? "true":"false")+
        ",\"poseUpdates\":"+std::to_string(poseUpdates.load())+",\"magazineUpdates\":"+std::to_string(magazineUpdates.load())+
        ",\"nativeRocketVisual\":"+(nativeRocketVisual.load() ? "true":"false")+",\"nativeRocketSkips\":"+std::to_string(nativeRocketSkips.load())+",\"rocketRenderUpdates\":"+std::to_string(rocketRenderUpdates.load())+
        ",\"blockedReloads\":"+std::to_string(blockedReloads.load())+",\"blockedTaskReloads\":"+std::to_string(blockedTaskReloads.load())+
        ",\"magazineRoom\":"+std::to_string(magazineRoom.load())+",\"insertionProgress\":"+std::to_string(insertionProgress.load())+
        ",\"insertionDistance\":"+std::to_string(insertionDistance.load())+
        ",\"insertionAligned\":"+(insertionAligned ? "true":"false")+
        ",\"ejections\":"+std::to_string(ejections.load())+
        ",\"insertions\":"+std::to_string(insertions.load())+",\"racks\":"+std::to_string(racks.load())+"}";
}
void VRReloadRuntime::Install()
{
    CIniReader ini("");if(!ini.ReadInteger("VR","ExperimentalPhysicalReload",0)) return;
    auto pose=hook::pattern("56 8B F1 8B 46 78 85 C0 74 17 F6 00 01 74 12 6A 01 05 04 1A 00 00 50 B9 ? ? ? ? E8 ? ? ? ? 8B 06 8B CE FF 90 A0 00 00 00 85 C0 74 16 8B 06 8B CE FF 90 A0 00 00 00 8B 10 8B C8 FF 92 E0 00 00 00 EB 06 8B 86 00 01 00 00 85 C0 0F 84 ? ? ? ? F6 86 F4 00 00 00 02");
    auto update=hook::pattern("56 8B F1 8B 46 1C 83 F8 05 0F 87 ? ? ? ? FF 24 85 ? ? ? ? 83 7E 60 00");
    auto complete=hook::pattern("56 8B F1 57 8B 7E 60 85 FF 75 0C 5F C7 46 1C 04 00 00 00 5E C2 04 00");
    auto alloc=hook::pattern("56 8B F1 8B 06 FF 90 A0 00 00 00 85 C0 0F 85 ? ? ? ? 8B 46 34 85 C0 0F 84 ? ? ? ? 57 8B 38 85 FF");
    auto decision=hook::pattern("55 8B EC 83 E4 F0 83 EC 58 56 57 8B 7D 08 8B F1 83 BF B0 02 00 00 07");
    auto task=hook::pattern("83 EC 08 55 8B 6C 24 10 56 57 8B F9 8D 8D B0 02 00 00 E8 ? ? ? ? 8B F0 89 74 24 10 85 F6");
    auto finish=hook::pattern("53 56 8B F1 57 8B 4E 48 85 C9 74 20 56 E8 ? ? ? ? F3 0F 10 46 44");
    if(pose.size()!=1 || update.size()!=1 || complete.size()!=1 || alloc.size()!=1 || decision.size()!=1 || task.size()!=1 || finish.size()!=1) return;
    // The world-pose routine's file bytes are packed; resolve its verified
    // CALL from B126E0 after unpacking, and validate the live entry bytes.
    auto* call=pose.get_first<unsigned char>(0x8D);
    if(*call!=0xE8) return;
    auto* world=call+5+*reinterpret_cast<int32_t*>(call+1);
    const unsigned char entry[]{0xFF,0x71,0x14,0xFF,0x71,0x08};
    if(std::memcmp(world,entry,sizeof(entry))) return;
    allocatePose=reinterpret_cast<AllocatePose>(alloc.get_first());
    finishReloadTask=reinterpret_cast<FinishReloadTask>(finish.get_first());
    poseHook=safetyhook::create_inline(pose.get_first(),UpdatePose);
    worldPoseHook=safetyhook::create_inline(world,CalculateWorldPose);
    reloadHook=safetyhook::create_inline(update.get_first(),UpdateWeapon);
    completionHook=safetyhook::create_inline(complete.get_first(),CompleteReload);
    decisionHook=safetyhook::create_inline(decision.get_first(),CanUseGunAction);
    taskHook=safetyhook::create_inline(task.get_first(),ProcessReloadTask);
    enabled=poseHook && worldPoseHook && reloadHook && completionHook && decisionHook && taskHook;
    if(enabled) {LoadProfile();FusionFix::onGameProcessEvent() += Process;}
}
