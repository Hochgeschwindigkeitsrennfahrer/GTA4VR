module;
#include <common.hxx>
#include "includes/vrmath.hpp"
#include "includes/vrgrip.hpp"
#include "includes/vrreload.hpp"
#include <atomic>
#include <cstring>
#include <string>
export module vrreload;
import common;
import comvars;
import natives;
import openxrvr;

export namespace VRReloadRuntime
{
    void Install();
    void Publish(void* owner,const VRMath::Matrix& transform,const VRMath::Matrix (&wrists)[2]);
    bool CanFire(void* owner,void* weapon);
    void EmptyTrigger(void* owner,void* weapon);
    void Fired(void* owner,void* weapon);
    bool UsesLeftHand();
    bool HoldsMagazine();
    bool SlideGrip(void* ped,void* gun,VRMath::Matrix& relative);
    bool ReloadConfig();
    bool TestFireAnimation();
    std::array<float,4> RecoilSettings(unsigned type);
    std::string Status();
}
namespace
{
    bool enabled=false;
    SafetyHookInline poseHook{},worldPoseHook{},reloadHook{},completionHook{},decisionHook{},taskHook{};
    using AllocatePose=void (__thiscall*)(void*);
    AllocatePose allocatePose=nullptr;
    using FinishReloadTask=void (__thiscall*)(void*,void*);
    FinishReloadTask finishReloadTask=nullptr;
    SRWLOCK poseLock=SRWLOCK_INIT;
    struct Sample {void* owner=nullptr;VRMath::Matrix transform{},wrist[2]{};uint64_t at=0;} sample;
    struct SlideHand {void* owner=nullptr;void* gun=nullptr;VRMath::Matrix local{},gripInverse{};uint64_t at=0;} slideHand;
    void* owner=nullptr;void* currentWeapon=nullptr;void* gunEntity=nullptr;
    std::atomic<unsigned> currentType=7;
    struct SavedAmmo {VRReload::Pistol ammo{};bool valid=false;} savedAmmo[4];
    unsigned AmmoIndex(unsigned type) {return type==15 ? 1u:(type==9 ? 2u:(type==13 ? 3u:0u));}
    const char* ProfileFile(unsigned type) {return type==15 ? "VRWeapons/m4.ini":(type==9 ? "VRWeapons/deagle.ini":(type==13 ? "VRWeapons/smg.ini":"VRWeapons/pistol.ini"));}
    std::atomic<void*> guardedWeapon=nullptr,visualGun=nullptr,visualMagazine=nullptr;
    std::atomic<bool> chamberReady=true,inserted=true,leftBusy=false;
    std::atomic<float> slideDistance=0;
    std::atomic<unsigned> poseUpdates=0,magazineUpdates=0,blockedReloads=0,blockedTaskReloads=0,ejections=0,insertions=0,racks=0;
    std::atomic<float> insertionDistance=-1;
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
        float handRotation[3]{100,180,180},handOffset[3]{.075f,.05f,.04f};
        VRMath::Vector axis{.258819f,0,.965926f,0};
        VRMath::Vector tip{-.05f,-.002341f,.024f,1};
        float travel=.085f,snap=.045f,alignment=.5f;
        float rackTravel=.045f,rackThreshold=.035f,rackRadius=.15f;
        VRMath::Vector rackGripOffset{0,0,0,0};
        float recoilKick=0,recoilSupportScale=.15f,recoilRecovery=.18f,recoilMaximum=75.f;
    } profile;
    Profile Defaults(unsigned type)
    {
        Profile value;
        if(type==9) value.recoilKick=8.f;
        if(type==13)
        {
            value.axis={0,0,1,0};value.travel=.07f;
            value.rackTravel=.055f;value.rackThreshold=.04f;value.rackRadius=.10f;
            value.recoilKick=3.f;value.recoilMaximum=35.f;
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
    unsigned magazineBone=~0u,slideBone=~0u,primaryGripBone=~0u,boneCount=0;
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
        auto read=[&](const char* key,float fallback,float minimum,float maximum)
        {
            const auto value=ini.ReadString("Reload",key,std::to_string(fallback));
            try
            {
                size_t end=0;const float parsed=std::stof(value,&end);
                if(value.find_first_not_of(" \t\r\n",end)!=std::string::npos ||
                    !std::isfinite(parsed) || parsed<minimum || parsed>maximum) {valid=false;return fallback;}
                return parsed;
            }
            catch(...) {valid=false;return fallback;}
        };
        const char* rotations[]{"MagazineHandRotationX","MagazineHandRotationY","MagazineHandRotationZ"};
        const char* offsets[]{"MagazineHandOffsetX","MagazineHandOffsetY","MagazineHandOffsetZ"};
        const char* axes[]{"InsertionAxisX","InsertionAxisY","InsertionAxisZ"};
        const char* tips[]{"MagazineTipX","MagazineTipY","MagazineTipZ"};
        for(unsigned a=0;a<3;++a)
        {
            next.handRotation[a]=read(rotations[a],next.handRotation[a],-180,180);
            next.handOffset[a]=read(offsets[a],next.handOffset[a],-.5f,.5f);
            next.axis[a]=read(axes[a],next.axis[a],-1,1);
            next.tip[a]=read(tips[a],next.tip[a],-.5f,.5f);
        }
        const float length=VRMath::Length3(next.axis);
        if(length<.1f) valid=false;
        else for(unsigned a=0;a<3;++a) next.axis[a]/=length;
        next.travel=read("InsertionTravel",next.travel,.02f,.25f);
        next.snap=read("SnapRadius",next.snap,.005f,.15f);
        next.alignment=read("AlignmentCosine",next.alignment,0,1);
        next.rackTravel=read("RackTravel",next.rackTravel,.01f,.15f);
        next.rackThreshold=read("RackThreshold",next.rackThreshold,.005f,.15f);
        next.rackRadius=read("RackGrabRadius",next.rackRadius,.02f,.20f);
        const char* rackOffsets[]{"RackGripOffsetX","RackGripOffsetY","RackGripOffsetZ"};
        for(unsigned a=0;a<3;++a)
            next.rackGripOffset[a]=read(rackOffsets[a],next.rackGripOffset[a],-.25f,.25f);
        if(next.rackThreshold>next.rackTravel) valid=false;
        next.recoilKick=read("RecoilKickDegrees",next.recoilKick,0,20);
        next.recoilSupportScale=read("RecoilSupportScale",next.recoilSupportScale,0,1);
        next.recoilRecovery=read("RecoilRecoverySeconds",next.recoilRecovery,.03f,1.f);
        next.recoilMaximum=read("RecoilMaxDegrees",next.recoilMaximum,0,90);
        configValid=valid;
        if(valid) {profile=next;guide={};insertionProgress=0;refreshHeldPose=true;
            recoilKick=next.recoilKick;recoilSupportScale=next.recoilSupportScale;
            recoilRecovery=next.recoilRecovery;recoilMaximum=next.recoilMaximum;
            racking=false;slideDistance=0;++configRevision;}
    }
    VRMath::Matrix HeldMagazine(const VRMath::Matrix& gun,const Sample& view)
    {
        auto rotation=VRMath::Identity();
        for(unsigned a=0;a<3;++a)
        {
            const float half=profile.handRotation[a]*.00872664626f;
            float position[3]{},q[4]{0,0,0,std::cos(half)};q[a]=std::sin(half);
            rotation=VRMath::Multiply(rotation,VRMath::Pose(position,q));
        }
        auto root=VRMath::Multiply(rotation,VRMath::Multiply(
            VRMath::Multiply(gun,VRMath::InverseRigid(view.wrist[1])),view.wrist[0]));
        const auto center=VRMath::Transform(rest[magazineBone][3],root);
        for(unsigned a=0;a<3;++a)
        {
            float offset=0;for(unsigned row=0;row<3;++row) offset+=view.wrist[0][row][a]*profile.handOffset[row];
            root[3][a]+=view.wrist[0][3][a]+offset-center[a];
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
    void PlayFireAnimation()
    {
        const auto object=WeaponObjectHandle(gunEntity);
        const unsigned type=currentType.load();
        const char* dictionary=type==15 ? "gun@ak47":(type==9 ? "gun@deagle":(type==13 ? "gun@mp5k":"gun@handgun"));
        // Play only the weapon object's authored clip, never a ped fire task.
        // Missing animation assets must not prevent an otherwise valid shot.
        if(!object) {fireAnimationError=1;++fireAnimationMisses;return;}
        if(!Natives::HaveAnimsLoaded(const_cast<char*>(dictionary)))
        {Natives::RequestAnims(const_cast<char*>(dictionary));fireAnimationError=2;++fireAnimationMisses;return;}
        if(Natives::PlayObjectAnim(object,const_cast<char*>("weapon_fire"),const_cast<char*>(dictionary),1000.f,false,false))
            {fireAnimationError=0;++fireAnimations;}
        else {fireAnimationError=3;++fireAnimationMisses;}
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
        return (type==7 || type==9 || type==13 || type==15) && type==*reinterpret_cast<unsigned*>(manager+12*(slot+3)) ? weapon:nullptr;
    }
    bool ResolveRig(void* entity)
    {
        auto* draw=*reinterpret_cast<void**>(static_cast<char*>(entity)+0x34);
        auto* drawable=draw ? *reinterpret_cast<void**>(draw):nullptr;
        auto* def=drawable ? *reinterpret_cast<void**>(static_cast<char*>(drawable)+0xC):nullptr;
        if(!def) return false;
        if(def==definition) return magazineBone<boneCount && slideBone<boneCount;
        definition=def;magazineBone=slideBone=primaryGripBone=~0u;
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
            if(name && !std::strcmp(name,currentType==15 ? "M4_Mag":(currentType==13 ? "mag":"Mag"))) magazineBone=b;
            // Animated combat pistol uses Cock1 for the hammer; Breach is
            // the slide. The Glock and M4 retain their accepted rack bones.
            const char* rackBone=currentType==15 ? "M4_Gun_Cock1":(currentType==9 ? "Gun_Breach":"Gun_Cock1");
            if(name && !std::strcmp(name,rackBone)) slideBone=b;
            if(name && !std::strcmp(name,"gun_grip")) primaryGripBone=b;
        }
        for(unsigned b=0;b<boneCount;++b) if(!resolved[b]) return false;
        return magazineBone<boneCount && slideBone<boneCount;
    }
    void __fastcall UpdatePose(void* entity,void*)
    {
        poseHook.thiscall<void>(entity);
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
            if(gun && b!=magazineBone && b!=slideBone) continue;
            // Native weapon_fire owns the animated slide/bolt between shots.
            // Manual rack takes ownership only during the physical interaction.
            if(gun && b==slideBone && !racking) continue;
            auto desired=VRMath::Multiply(rest[b],VRMath::AffineXYZ(*root));
            if((mag && b!=magazineBone) || (gun && b==magazineBone && !inserted))
            {
                // Collapse unwanted geometry in this instance's skin palette.
                for(unsigned row=0;row<3;++row) for(unsigned a=0;a<3;++a) desired[row][a]=0;
                if(mag) desired[3]=VRMath::Transform(rest[magazineBone][3],VRMath::AffineXYZ(*root));
            }
            if(gun && b==slideBone)
                for(unsigned a=0;a<3;++a) desired[3][a]-=(*root)[0][a]*slideDistance.load();
            for(unsigned row=0;row<4;++row) for(unsigned a=0;a<3;++a) matrices[b][row][a]=desired[row][a];
        }
        ++poseUpdates;
    }
    void ApplyMagazinePose(void* skeleton)
    {
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
        ++magazineUpdates;
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
        {++blockedReloads;return false;}
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
            ++blockedTaskReloads;return true;
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
        *frame=root;
        auto* skeleton=*reinterpret_cast<void**>(static_cast<char*>(magazineEntity)+0x100);
        if(skeleton) CalculateWorldPose(skeleton,nullptr);
    }
    bool MakeMagazine(const VRMath::Matrix& root)
    {
        DeleteMagazine();uint32_t model=0;Natives::GetWeapontypeModel(currentType.load(),&model);
        if(!model || !Natives::HasModelLoaded(model)) return false;
        Natives::CreateObjectNoOffset(model,root[3][0],root[3][1],root[3][2],&magazineObject,true);
        magazineEntity=ObjectEntity(magazineObject);
        if(!magazineEntity) {DeleteMagazine();return false;}
        Natives::SetObjectCollision(magazineObject,false);
        Natives::SetObjectDynamic(magazineObject,false);
        Natives::FreezeObjectPosition(magazineObject,true);
        if(!*reinterpret_cast<void**>(static_cast<char*>(magazineEntity)+0x100)) allocatePose(magazineEntity);
        auto* frame=*reinterpret_cast<VRMath::Matrix**>(static_cast<char*>(magazineEntity)+0x20);
        if(!frame || !*reinterpret_cast<void**>(static_cast<char*>(magazineEntity)+0x100)) {DeleteMagazine();return false;}
        visualMagazine=magazineEntity;MoveMagazine(root);
        return true;
    }
    void WriteAmmo(Ped ped)
    {
        Natives::SetCharAmmo(ped,currentType.load(),ammo.NativeTotal());
        Natives::SetAmmoInClip(ped,currentType.load(),ammo.Clip());
        previousNativeTotal=ammo.NativeTotal();
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
        if(configPending.exchange(false)) LoadProfile();
        if(enabled && initialized && !OpenXRVR::IsSessionRunning())
        {
            // Overlay focus loss is not session loss. Only a stopped session
            // hands ammunition and reloading back to the ordinary game.
            Ped ped=0;Natives::GetPlayerChar(Natives::GetPlayerId(),&ped);
            currentWeapon=nullptr; // Inventory may have changed during session loss.
            if(ped && Natives::DoesCharExist(ped) && !Natives::IsCharDead(ped))
            {ammo.ReturnHeld();WriteAmmo(ped);}
            guardedWeapon=nullptr;visualGun=nullptr;currentWeapon=nullptr;
            initialized=false;racking=false;slideDistance=0;leftBusy=false;DeleteMagazine();
        }
        Sample view;AcquireSRWLockShared(&poseLock);view=sample;ReleaseSRWLockShared(&poseLock);
        if(!enabled || !OpenXRVR::IsSessionRunning() || !view.owner || GetTickCount64()-view.at>250) return;
        Ped ped=0;Natives::GetPlayerChar(Natives::GetPlayerId(),&ped);
        if(!ped || !Natives::DoesCharExist(ped) || Natives::IsCharDead(ped))
        {guardedWeapon=nullptr;visualGun=nullptr;currentWeapon=nullptr;leftBusy=false;initialized=false;
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
            guardedWeapon=nullptr;visualGun=nullptr;currentWeapon=nullptr;
            ammo.ReturnHeld();
            if(Natives::HasCharGotWeapon(ped,currentType.load()))
            {
                Natives::SetCharAmmo(ped,currentType.load(),ammo.NativeTotal());
                Natives::SetAmmoInClip(ped,currentType.load(),ammo.Clip());
                savedAmmo[AmmoIndex(currentType)]={ammo,true};
            }
            initialized=false;racking=false;slideDistance=0;leftBusy=false;DeleteMagazine();
        }
        if(weapon && type!=currentType)
        {currentType=type;definition=nullptr;profile=Defaults(type);LoadProfile();}
        if(!weapon || !ResolveRig(entity))
        {
            guardedWeapon=nullptr;visualGun=nullptr;currentWeapon=nullptr;racking=false;leftBusy=false;guide={};
            return;
        }
        currentWeapon=weapon;gunEntity=entity;
        unsigned total=0,clip=0,capacity=0;Natives::GetAmmoInCharWeapon(ped,type,&total);
        if(!initialized)
        {
            Natives::GetAmmoInClip(ped,type,&clip);Natives::GetMaxAmmoInClip(ped,type,&capacity);
            if(capacity<2 || capacity>64 || total>100000) return;
            auto& saved=savedAmmo[AmmoIndex(type)];
            if(saved.valid)
            {
                ammo=saved.ammo;
                if(!ammo.Resume(total,capacity)) ammo.Begin(clip,total,capacity);
            }
            else ammo.Begin(clip,total,capacity);
            initialized=true;previousNativeTotal=total;rightGrip=leftGrip=true;
        }
        else if(total>previousNativeTotal) ammo.reserve+=total-previousNativeTotal;
        else if(total<previousNativeTotal) ammo.reserve-=std::min(ammo.reserve,previousNativeTotal-total);
        guardedWeapon=weapon;visualGun=entity;WriteAmmo(ped);
        OpenXRVR::ControllerInput input{};
        if(!OpenXRVR::HasFocusedViews() || !OpenXRVR::GetControllerInput(input) || input.phoneActive ||
            Natives::IsPauseMenuActive() || !input.gripPoseValid[0] || !input.gripPoseValid[1])
        {rightGrip=leftGrip=true;racking=false;slideDistance=0;guide={};return;}
        if(fireAnimationTest.exchange(false) && !racking) PlayFireAnimation();
        auto* frame=*reinterpret_cast<VRMath::Matrix**>(static_cast<char*>(entity)+0x20);
        if(!frame) return;
        const auto gun=VRMath::AffineXYZ(*frame);
        const bool right=input.grabActive[1] && input.grab[1];
        const bool left=input.grabActive[0] && input.grab[0];
        if(right && !rightGrip && ammo.inserted)
        {
            ammo.Eject();++ejections;
            if(!ammo.holding && MakeMagazine(gun)) {dropped=true;droppedRoot=gun;dropVelocity=0;dropAt=GetTickCount64();}
        }
        // Shoulder is defined relative to the live HMD, not startup height.
        OpenXRVR::EyeView eyes[2]{};long long display=0;
        if(left && !leftGrip && !ammo.holding && !racking && OpenXRVR::GetEyeViews(eyes,display))
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
                if(MakeMagazine(root) && ammo.TakeMagazine()) magazineOffset=VRGrip::Capture(root,view.wrist[0]);
            }
        }
        if(ammo.holding && magazineEntity)
        {
            if(refreshHeldPose) magazineOffset=VRGrip::Capture(HeldMagazine(gun,view),view.wrist[0]);
            auto root=VRGrip::Target(magazineOffset,view.wrist[0]);
            // The leading end meets the bottom opening; do not snap the
            // magazine's center to a sphere around the pistol's whole grip.
            const auto tip=VRMath::Transform(profile.tip,root);
            auto well=VRMath::Transform(profile.tip,gun);
            const auto axis=VRMath::Transform(profile.axis,gun);
            for(unsigned a=0;a<3;++a) well[a]-=axis[a]*profile.travel;
            const float distance=VRMath::Length3(VRMath::Difference3(tip,well));
            insertionDistance=distance;
            const auto local=VRMath::Transform(view.wrist[0][3],VRMath::InverseRigid(gun));
            const auto heldAxis=VRMath::Transform(profile.axis,root);
            const bool aligned=VRMath::Dot3(heldAxis,axis)>profile.alignment && VRMath::Dot3(root[1],gun[1])>0.f;
            if(!ammo.inserted && !guide.active && left && distance<profile.snap && aligned)
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
                    for(unsigned a=0;a<3;++a) root[3][a]-=axis[a]*(profile.travel-guide.progress);
                }
                insertionProgress=guide.progress;
                if(seated && left) {ammo.Insert();++insertions;DeleteMagazine();}
            }
            if(ammo.holding)
            {
                MoveMagazine(root);
                if(!left) {ammo.DropHeld();guide={};dropped=true;droppedRoot=root;dropVelocity=0;dropAt=GetTickCount64();}
            }
        }
        if(!ammo.holding && !racking && left && !leftGrip)
        {
            auto gripPoint=rest[slideBone][3];
            if(type==13 || type==15) for(unsigned a=0;a<3;++a) gripPoint[a]+=profile.rackGripOffset[a];
            const auto slide=VRMath::Transform(gripPoint,gun);
            if(VRMath::Length3(VRMath::Difference3(view.wrist[0][3],slide))<profile.rackRadius)
            {
                racking=true;pulled=false;rackStart=VRMath::Transform(view.wrist[0][3],VRMath::InverseRigid(gun));
                rackHandLocal=VRReload::SlideHandPose(VRMath::Multiply(view.wrist[0],VRMath::InverseRigid(gun)),rest[slideBone][3]);
                if(type==13 || type==15)
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
            {pulled=true;PlayPistolSound("HANDGUN_RELOAD_COCK_PULL");++slidePullSounds;}
            if(!left)
            {
                if(pulled)
                {ammo.Rack();++racks;PlayPistolSound("HANDGUN_RELOAD_COCK_RELEASE");++slideReleaseSounds;}
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
void VRReloadRuntime::Publish(void* ped,const VRMath::Matrix& transform,const VRMath::Matrix (&wrists)[2])
{
    if(!enabled) return;
    AcquireSRWLockExclusive(&poseLock);sample.owner=ped;sample.transform=transform;
    sample.wrist[0]=wrists[0];sample.wrist[1]=wrists[1];sample.at=GetTickCount64();ReleaseSRWLockExclusive(&poseLock);
}
bool VRReloadRuntime::CanFire(void*,void* weapon)
{return weapon!=guardedWeapon.load() || (chamberReady && !racking);}
void VRReloadRuntime::EmptyTrigger(void* ped,void* weapon)
{
    if(ped!=owner || weapon!=guardedWeapon.load() || chamberReady || racking || !gunEntity) return;
    // Native weapon-bank click identified in sounds.dat15. This is audio
    // only: never invoke Fire with an empty clip or start a reload task.
    PlayPistolSound("GENERAL_GUNS_AK47_DRY_CLICK");
    ++emptyClicks;
}
void VRReloadRuntime::Fired(void*,void* weapon)
{
    if(weapon!=guardedWeapon.load()) return;
    ammo.Fired();previousNativeTotal=ammo.NativeTotal();chamberReady=ammo.Ready();
    PlayFireAnimation();
}
bool VRReloadRuntime::UsesLeftHand() {return leftBusy;}
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
std::array<float,4> VRReloadRuntime::RecoilSettings(unsigned type)
{
    if(type!=9 && type!=13 && type!=15) return {};
    if(type!=currentType || !enabled) {const auto defaults=Defaults(type);return {defaults.recoilKick,defaults.recoilSupportScale,defaults.recoilRecovery,defaults.recoilMaximum};}
    return {recoilKick.load(),recoilSupportScale.load(),recoilRecovery.load(),recoilMaximum.load()};
}
std::string VRReloadRuntime::Status()
{
    return "{\"enabled\":"+std::string(enabled ? "true":"false")+",\"active\":"+(guardedWeapon ? "true":"false")+
        ",\"assetReady\":"+(definition && magazineBone<boneCount && slideBone<boneCount ? "true":"false")+
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
        ",\"poseUpdates\":"+std::to_string(poseUpdates.load())+",\"magazineUpdates\":"+std::to_string(magazineUpdates.load())+
        ",\"blockedReloads\":"+std::to_string(blockedReloads.load())+",\"blockedTaskReloads\":"+std::to_string(blockedTaskReloads.load())+
        ",\"magazineRoom\":"+std::to_string(magazineRoom.load())+",\"insertionProgress\":"+std::to_string(insertionProgress.load())+
        ",\"insertionDistance\":"+std::to_string(insertionDistance.load())+
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
