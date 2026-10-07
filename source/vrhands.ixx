module;
#include <common.hxx>
#include "includes/vrmath.hpp"
#include "includes/vrgrip.hpp"
#include "includes/vrmelee.hpp"
#include <atomic>
#include <cstdio>
#include <cstring>
export module vrhands;
import common;
import comvars;
import openxrvr;
import natives;
import vrhandphysics;
import vrreload;

export namespace VRHands
{
    void Install();
    void PublishTrackingTransform(const VRMath::Matrix& trackingToWorld,void* owner,bool active);
    bool StationaryBodyHeading(void* owner,float current,float& heading);
    bool Active(void* owner);
    void SetTraversalState(void* owner,bool moving,bool neutralAllowed);
    std::string Status();
    std::string PistolStatus();
    bool SetRotation(int x,int y,int z);
    bool Calibrate(unsigned hand);
    bool Trim(unsigned hand,int x,int y,int z);
    bool GetWrist(unsigned hand,VRMath::Matrix& world);
}

namespace
{
    bool enabled=false;
    struct PhysicsRig
    {
        void* owner=nullptr;
        void* held=nullptr;
        VRMath::Matrix* matrices=nullptr;
        int wrist[2]{-1,-1},elbow[2]{-1,-1};
        VRMath::Matrix requested[2]{};
        bool valid[2]{};
        bool supporting[2]{};
        VRMath::Matrix trackingTransform{};
        uint64_t sampledAt=0;
    } physicsRig;
    void PublishPhysicsPose();
    SRWLOCK targetLock=SRWLOCK_INIT;
    SRWLOCK wristLock=SRWLOCK_INIT;
    VRMath::Matrix publishedWrists[2]{};
    uint64_t publishedWristAt[2]{};
    bool fitBodyHeight=false;
    bool supportGripEnabled=false;
    std::atomic<bool> supportGripping=false,supportReady=false;
    std::atomic<float> supportDistance=-1.f;
    void* supportOwner=nullptr;
    void* supportWeapon=nullptr;
    void* supportModel=nullptr;
    VRMath::Matrix supportRelative{};
    float configuredHeight=1.75f,bodyBottom=0,bodyScale=1,bodyNativeHeight=0;
    float bodyHeadAllowance=.25f;
    float bodyHeightScale=1.f;
    float bodyVerticalOffset=.10f;
    bool trackedBody=false;
    std::atomic<float> torsoDrop=0;
    std::atomic<unsigned> legUpdates=0;
    void* bodyDefinition=nullptr;
    void* bodyDimensionsOwner=nullptr;
    VRMath::Matrix bodyRest[255]{};
    bool bodyRestValid[255]{};
    unsigned bodyRestCount=0;
    void* traversalOwner=nullptr;
    uint64_t traversalAt=0,idleSince=0;
    bool neutralIdle=false;
    std::atomic<unsigned> neutralPoseUpdates=0;
    struct ScaledPose
    {
        void* owner=nullptr;
        VRMath::Matrix* matrices=nullptr;
        unsigned count=0;
        bool pending=false;
        bool captured=false;
        VRMath::Vector pivot{};
        float scale=1;
        VRMath::Matrix before[255]{},after[255]{};
    } scaledPose;
    void* PlayerSkeleton(void* owner)
    {
        using Getter=void* (__thiscall*)(void*);
        const auto vtable=*reinterpret_cast<void***>(owner);
        void* fragment=reinterpret_cast<Getter>(vtable[0xA0/4])(owner);
        if(!fragment) return *reinterpret_cast<void**>(static_cast<char*>(owner)+0x100);
        return reinterpret_cast<Getter>((*reinterpret_cast<void***>(fragment))[0xE0/4])(fragment);
    }
    void ResolveBodyHeight(void* skeleton,void* owner)
    {
        if(!fitBodyHeight) return;
        auto* definition=*reinterpret_cast<void**>(static_cast<char*>(skeleton)+4);
        if(!definition || (definition==bodyDefinition && owner==bodyDimensionsOwner)) return;
        const unsigned count=*reinterpret_cast<unsigned short*>(static_cast<char*>(skeleton)+0x10);
        if(!count || count>255) return;
        auto* bones=*reinterpret_cast<char**>(definition);
        if(!bones) return;
        VRMath::Matrix rest[255]{};
        bool valid[255]{};
        // Resolve arbitrary parent ordering, bounded by the bone count.
        for(unsigned pass=0;pass<count;++pass)
            for(unsigned bone=0;bone<count;++bone)
            {
                if(valid[bone]) continue;
                auto* descriptor=bones+0xE0*bone;
                auto* parent=*reinterpret_cast<char**>(descriptor+0x10);
                unsigned index=0;
                if(parent)
                {
                    const auto offset=reinterpret_cast<uintptr_t>(parent)-reinterpret_cast<uintptr_t>(bones);
                    if(offset%0xE0 || offset/0xE0>=count) continue;
                    index=static_cast<unsigned>(offset/0xE0);
                    if(!valid[index]) continue;
                }
                float position[3],quaternion[4];
                std::memcpy(position,descriptor+0x20,sizeof(position));
                std::memcpy(quaternion,descriptor+0x40,sizeof(quaternion));
                bool finite=true; float norm=0;
                for(float v:position) finite &= std::isfinite(v);
                for(float v:quaternion) {finite &= std::isfinite(v); norm+=v*v;}
                if(!finite || norm<1.e-8f) continue;
                rest[bone]=VRMath::Pose(position,quaternion);
                if(parent) rest[bone]=VRMath::Multiply(rest[bone],rest[index]);
                valid[bone]=true;
            }
        float bottom=INFINITY,top=-INFINITY;
        for(unsigned bone=0;bone<count;++bone)
        {
            if(!valid[bone]) continue;
            const char* name=*reinterpret_cast<const char**>(bones+0xE0*bone);
            if(!name) continue;
            if(strcmp(name,"Char_L_Toe0")==0 || strcmp(name,"Char_R_Toe0")==0)
                bottom=std::min(bottom,rest[bone][3][2]);
            if(strcmp(name,"Char_Head")==0) top=rest[bone][3][2]+bodyHeadAllowance;
        }
        if(!std::isfinite(bottom+top) || top-bottom<1.f || top-bottom>2.5f) return;
        AcquireSRWLockExclusive(&targetLock);
        bodyDefinition=definition; bodyDimensionsOwner=owner;
        bodyBottom=bottom; bodyNativeHeight=top-bottom;
        bodyRestCount=count;
        for(unsigned bone=0;bone<count;++bone) {bodyRest[bone]=rest[bone]; bodyRestValid[bone]=valid[bone];}
        bodyScale=std::clamp(configuredHeight/bodyNativeHeight*bodyHeightScale,.65f,1.25f);
        ReleaseSRWLockExclusive(&targetLock);
    }
    void NeutralStandingPose(void* skeleton,void* owner,VRMath::Matrix* matrices,const VRMath::Matrix& root)
    {
        if(!trackedBody || !fitBodyHeight || !scaledPose.pending) return;
        AcquireSRWLockShared(&targetLock);
        const auto now=GetTickCount64();
        const bool apply=neutralIdle && owner==traversalOwner && owner==bodyDimensionsOwner &&
            traversalAt && now-traversalAt<=100 && idleSince && now-idleSince>=200;
        const float t=idleSince ? std::clamp((static_cast<float>(now-idleSince)-200.f)/450.f,0.f,1.f):0.f;
        const float blend=t*t*(3.f-2.f*t);
        ReleaseSRWLockShared(&targetLock);
        if(!apply) return;
        const unsigned count=*reinterpret_cast<unsigned short*>(static_cast<char*>(skeleton)+0x10);
        auto* definition=*reinterpret_cast<void**>(static_cast<char*>(skeleton)+4);
        if(definition!=bodyDefinition || !count || count!=bodyRestCount) return;
        auto* bones=*reinterpret_cast<char**>(definition);
        if(!bones) return;
        int clavicle[2]{-1,-1},parents[255];
        for(unsigned bone=0;bone<count;++bone)
        {
            if(!bodyRestValid[bone]) return;
            auto* descriptor=bones+0xE0*bone;
            const char* name=*reinterpret_cast<const char**>(descriptor);
            auto* parent=*reinterpret_cast<char**>(descriptor+0x10);
            const auto offset=reinterpret_cast<uintptr_t>(parent)-reinterpret_cast<uintptr_t>(bones);
            parents[bone]=parent && offset%0xE0==0 && offset/0xE0<count ? static_cast<int>(offset/0xE0):-1;
            if(name && !strcmp(name,"Char_L_Clavicle")) clavicle[0]=bone;
            if(name && !strcmp(name,"Char_R_Clavicle")) clavicle[1]=bone;
        }
        if(clavicle[0]<0 || clavicle[1]<0) return;
        VRMath::Matrix carry[2];
        for(unsigned hand=0;hand<2;++hand)
        {
            const auto desired=VRMath::Multiply(bodyRest[clavicle[hand]],VRMath::AffineXYZ(root));
            carry[hand]=VRMath::Multiply(VRMath::InverseRigid(VRMath::AffineXYZ(matrices[clavicle[hand]])),desired);
        }
        // Restore authored symmetric standing legs/spine, preserving native
        // arm/finger pose relative to each newly aligned shoulder. Tracked IK
        // replaces the arms afterward, with equipped-weapon fingers intact.
        for(unsigned bone=0;bone<count;++bone) scaledPose.before[bone]=matrices[bone];
        scaledPose.captured=true;
        for(unsigned bone=0;bone<count;++bone)
        {
            int branch=-1,current=bone;
            for(unsigned depth=0;depth<count && current>=0;++depth)
            {
                if(current==clavicle[0]) {branch=0; break;}
                if(current==clavicle[1]) {branch=1; break;}
                current=parents[current];
            }
            const auto neutral=branch>=0 ?
                VRMath::Multiply(VRMath::AffineXYZ(scaledPose.before[bone]),carry[branch]):
                VRMath::Multiply(bodyRest[bone],VRMath::AffineXYZ(root));
            const auto desired=VRMath::BlendRigidPose(scaledPose.before[bone],neutral,blend);
            for(unsigned row=0;row<4;++row) for(unsigned axis=0;axis<3;++axis)
                matrices[bone][row][axis]=desired[row][axis];
        }
        ++neutralPoseUpdates;
    }

    void LowerTrackedTorso(void* skeleton,VRMath::Matrix* matrices,const VRMath::Matrix& root,
        const VRMath::Vector& pivot,float scale)
    {
        torsoDrop=0;
        if(!trackedBody || !fitBodyHeight) return;
        OpenXRVR::EyeView eyes[2]{}; long long time=0;
        if(!OpenXRVR::GetEyeViews(eyes,time) || !eyes[0].floorRelative || !eyes[1].floorRelative) return;
        const float height=(eyes[0].position[1]+eyes[1].position[1])*.5f;
        if(!std::isfinite(height) || height<.4f || height>2.5f) return;
        const unsigned count=*reinterpret_cast<unsigned short*>(static_cast<char*>(skeleton)+0x10);
        auto* definition=*reinterpret_cast<void**>(static_cast<char*>(skeleton)+4);
        auto* bones=definition ? *reinterpret_cast<char**>(definition):nullptr;
        if(!bones || !count || count>255) return;
        int head=-1,thigh[2]{-1,-1},calf[2]{-1,-1},foot[2]{-1,-1};
        int parents[255];
        for(unsigned bone=0;bone<count;++bone)
        {
            auto* descriptor=bones+0xE0*bone;
            const char* name=*reinterpret_cast<const char**>(descriptor);
            auto* parent=*reinterpret_cast<char**>(descriptor+0x10);
            const auto offset=reinterpret_cast<uintptr_t>(parent)-reinterpret_cast<uintptr_t>(bones);
            parents[bone]=parent && offset%0xE0==0 && offset/0xE0<count ? static_cast<int>(offset/0xE0):-1;
            if(!name) continue;
            if(!strcmp(name,"Char_Head")) head=bone;
            for(unsigned side=0;side<2;++side)
            {
                if(!strcmp(name,side ? "Char_R_Thigh":"Char_L_Thigh")) thigh[side]=bone;
                if(!strcmp(name,side ? "Char_R_Calf":"Char_L_Calf")) calf[side]=bone;
                if(!strcmp(name,side ? "Char_R_Foot":"Char_L_Foot")) foot[side]=bone;
            }
        }
        if(head<0 || thigh[0]<0 || thigh[1]<0 || calf[0]<0 || calf[1]<0 || foot[0]<0 || foot[1]<0) return;
        // Subtract lowering already supplied by the native crouch animation.
        // The visible rig alone follows height; camera and collision stay native.
        const float desired=pivot[2]+bodyNativeHeight-bodyHeadAllowance-
            std::max(0.f,configuredHeight-height-bodyVerticalOffset)/scale;
        const float drop=std::clamp(matrices[head][3][2]-desired,0.f,.65f/scale);
        if(drop<.002f) return;
        VRMath::Vector knees[2],hips[2];
        for(unsigned side=0;side<2;++side)
        {
            hips[side]=matrices[thigh[side]][3]; hips[side][2]-=drop;
            const auto upper=VRMath::Difference3(matrices[calf[side]][3],matrices[thigh[side]][3]);
            const auto lower=VRMath::Difference3(matrices[foot[side]][3],matrices[calf[side]][3]);
            if(!VRMath::TwoBoneKnee(hips[side],matrices[foot[side]][3],root[1],
                VRMath::Length3(upper),VRMath::Length3(lower),knees[side])) return;
        }
        // Capture the native input, not our corrected output, for reused poses.
        VRMath::Matrix inputPose[255];
        for(unsigned bone=0;bone<count;++bone)
        {
            inputPose[bone]=matrices[bone];
            if(!scaledPose.captured) scaledPose.before[bone]=matrices[bone];
        }
        scaledPose.captured=true;
        for(unsigned bone=0;bone<count;++bone) if(parents[bone]>=0) matrices[bone][3][2]-=drop;
        auto descendant=[&](unsigned bone,int ancestor)
        {
            int current=bone;
            for(unsigned depth=0;depth<count && current>=0;++depth)
            {if(current==ancestor) return true; current=parents[current];}
            return false;
        };
        for(unsigned side=0;side<2;++side)
        {
            const auto& before=inputPose;
            const auto upper=VRMath::SegmentRotation(
                VRMath::Difference3(before[calf[side]][3],before[thigh[side]][3]),
                VRMath::Difference3(knees[side],hips[side]));
            const auto lower=VRMath::SegmentRotation(
                VRMath::Difference3(before[foot[side]][3],before[calf[side]][3]),
                VRMath::Difference3(before[foot[side]][3],knees[side]));
            for(unsigned bone=0;bone<count;++bone)
            {
                if(!descendant(bone,thigh[side])) continue;
                if(descendant(bone,foot[side])) {matrices[bone]=before[bone]; continue;}
                const bool below=descendant(bone,calf[side]);
                const auto& rotation=below ? lower:upper;
                const auto& origin=before[below ? calf[side]:thigh[side]][3];
                const auto& target=below ? knees[side]:hips[side];
                const auto rotated=VRMath::Multiply(VRMath::AffineXYZ(before[bone]),rotation);
                const auto translated=VRMath::Transform(VRMath::Difference3(before[bone][3],origin),rotation);
                for(unsigned row=0;row<3;++row) for(unsigned axis=0;axis<3;++axis)
                    matrices[bone][row][axis]=rotated[row][axis];
                for(unsigned axis=0;axis<3;++axis) matrices[bone][3][axis]=target[axis]+translated[axis];
            }
        }
        torsoDrop=drop*scale; ++legUpdates;
    }

    void RestoreBodyScale()
    {
        // Animation may regenerate some or all world bones between callbacks.
        // Restore only matrices that still exactly equal our previous output.
        for(unsigned bone=0;bone<scaledPose.count;++bone)
        {
            bool unchanged=true;
            for(unsigned row=0;row<4;++row) for(unsigned axis=0;axis<3;++axis)
                unchanged &= scaledPose.matrices[bone][row][axis]==scaledPose.after[bone][row][axis];
            if(unchanged)
                for(unsigned row=0;row<4;++row) for(unsigned axis=0;axis<3;++axis)
                    scaledPose.matrices[bone][row][axis]=scaledPose.before[bone][row][axis];
        }
        scaledPose.count=0;
    }
    void ApplyBodyScale()
    {
        if(!scaledPose.pending) return;
        scaledPose.pending=false;
        for(unsigned bone=0;bone<scaledPose.count;++bone)
        {
            if(!scaledPose.captured) scaledPose.before[bone]=scaledPose.matrices[bone];
            auto result=VRMath::ScaleBoneAbout(scaledPose.matrices[bone],scaledPose.pivot,scaledPose.scale);
            if(trackedBody) result[3][2]+=bodyVerticalOffset;
            scaledPose.matrices[bone]=scaledPose.after[bone]=result;
        }
    }
    SafetyHookInline updateHook{};
    SafetyHookInline finishPoseHook{};
    thread_local bool deferFinishPose=false;
    thread_local void* deferredPose=nullptr;
    std::atomic<unsigned> finishPoseUpdates{0};

    void __fastcall FinishPose(void* context,void*)
    {
        if(deferFinishPose) {deferredPose=context; return;}
        finishPoseHook.thiscall<void>(context);
    }

    struct FinishTrackedPose
    {
        bool publish=false;
        ~FinishTrackedPose()
        {
            deferFinishPose=false;
            auto* context=deferredPose;
            deferredPose=nullptr;
            if(context) {finishPoseHook.thiscall<void>(context); ++finishPoseUpdates;}
            ApplyBodyScale();
            if(publish) PublishPhysicsPose();
        }
    };
    using ArmSolver=bool (__thiscall*)(void*,int,const void*,const void*);
    ArmSolver solveArm=nullptr;
    VRMath::Matrix trackingToWorld{};
    void* targetOwner=nullptr;
    uint64_t targetAt=0;
    VRMath::Matrix wristCorrection[2]={VRMath::Identity(),VRMath::Identity()};
    bool calibrated[2]{};
    int correctionDegrees[3]{};
    std::atomic<unsigned> updates{0},solves[2]{};
    bool fingersEnabled=false;
    struct FingerJoint {unsigned bone=0,parent=0,finger=0,joint=0; VRMath::Matrix rest{};};
    struct FingerRig {void* definition=nullptr; unsigned count[2]{}; FingerJoint joints[2][15]{};} fingerRig;
    std::atomic<unsigned> fingerUpdates[2]{},fingerJointCounts[2]{};
    std::atomic<bool> fingerClosed[2]{};
    std::atomic<bool> weaponGrip{false};
    SafetyHookInline pistolFireHook{};
    using MuzzleLookup=VRMath::Vector* (__thiscall*)(void*,VRMath::Vector*,const VRMath::Matrix*,int);
    MuzzleLookup muzzleLookup=nullptr;
    using WeaponRange=float (__thiscall*)(void*,void*);
    WeaponRange weaponRange=nullptr;
    std::atomic<unsigned> muzzleShots{0};
    using WeaponFire=char (__thiscall*)(void*,void*,const VRMath::Matrix*,const VRMath::Vector*,const VRMath::Vector*,
        uint32_t,uint32_t,uint32_t,uint32_t,float);
    WeaponFire weaponFire=nullptr;
    SafetyHookInline weaponFireHook{};
    SafetyHookInline bulletHitHook{};
    SafetyHookMid aimedHeadshotHook{};
    std::atomic<unsigned> aimedHeadshotChecks=0,aimedHeadshotOverrides=0;
    using AimTarget=void* (__cdecl*)(void*,const VRMath::Vector*,const VRMath::Vector*,bool,bool);
    using AimEventCtor=void* (__thiscall*)(void*,void*);
    using AimEventDtor=void (__thiscall*)(void*);
    using AimAddEvent=void* (__thiscall*)(void*,void*,bool,bool);
    AimTarget aimTarget=nullptr;
    AimEventCtor aimEventCtor=nullptr;
    AimEventDtor aimEventDtor=nullptr;
    AimAddEvent aimAddEvent=nullptr;
    float aimRaiseBelowHead=.40f,aimDwell=0;
    void* aimCandidate=nullptr;
    uint64_t aimSampleAt=0;
    std::atomic<bool> aimRaised=false,aimHasTarget=false;
    std::atomic<unsigned> aimQueries=0,aimEvents=0,aimVehicleEvents=0;
    std::atomic<unsigned> bulletHits=0,bulletPart=~0u,bulletShape=~0u;
    std::atomic<float> bulletDamage=0,bulletApplied=0,bulletHealthBefore=0,bulletHealthAfter=0;
    bool triggerLatched=false;
    std::atomic<unsigned> triggerAttempts{0},triggerShots{0};
    bool weaponSwapEnabled=false,weaponClickLatched=true;
    std::atomic<unsigned> weaponSwaps{0};

    void ProcessWeaponSwap()
    {
        OpenXRVR::ControllerInput input{};
        if(!OpenXRVR::HasFocusedViews() || !OpenXRVR::GetControllerInput(input) ||
            !input.gripPoseValid[1]) {weaponClickLatched=true; return;}
        if(!input.cycle && !input.nextWeapon && !input.previousWeapon) {weaponClickLatched=false; return;}
        if(weaponClickLatched) return;
        weaponClickLatched=true;
        const float horizontal=input.previousWeapon ? -1.f:input.nextWeapon ? 1.f:input.cycleDirection;
        if(!std::isfinite(horizontal) || std::abs(horizontal)<.25f) return;
        void* owner=nullptr; uint64_t at=0;
        AcquireSRWLockShared(&targetLock); owner=targetOwner; at=targetAt; ReleaseSRWLockShared(&targetLock);
        if(!owner || !at || GetTickCount64()-at>100) return;
        using Hash=Natives::NativeHashes;
        for(auto hash:{Hash::GET_PLAYER_ID,Hash::GET_PLAYER_CHAR,Hash::DOES_CHAR_EXIST,
            Hash::IS_CHAR_DEAD,Hash::IS_CHAR_IN_ANY_CAR,Hash::IS_PAUSE_MENU_ACTIVE,
            Hash::GET_CURRENT_CHAR_WEAPON,Hash::GET_WEAPONTYPE_SLOT,Hash::GET_CHAR_WEAPON_IN_SLOT,
            Hash::HAS_CHAR_GOT_WEAPON,Hash::SET_CURRENT_CHAR_WEAPON})
            if(!NativeInvoke::GetNativeHandler(std::to_underlying(hash))) return;
        Ped ped=0; Natives::GetPlayerChar(Natives::GetPlayerId(),&ped);
        if(!ped || !Natives::DoesCharExist(ped) || Natives::IsCharDead(ped) ||
            Natives::IsCharInAnyCar(ped) || Natives::IsPauseMenuActive()) return;
        uint32_t current=0;
        if(!Natives::GetCurrentCharWeapon(ped,&current)) return;
        int32_t slot=0; Natives::GetWeapontypeSlot(static_cast<int32_t>(current),&slot);
        if(slot<0 || slot>10) return;
        const int direction=horizontal<0 ? -1:1;
        // Native inventory slots, not a hardcoded list of weapon classes.
        // Slot zero is unarmed; skip other empty slots and wrap once.
        for(int step=1;step<=10;++step)
        {
            const unsigned next=static_cast<unsigned>((slot+direction*step+11)%11);
            uint32_t candidate=0,ammo=0,clip=0;
            Natives::GetCharWeaponInSlot(ped,next,&candidate,&ammo,&clip);
            if((candidate==0 && next!=0) || candidate==current ||
                (candidate!=0 && !Natives::HasCharGotWeapon(ped,candidate))) continue;
            Natives::SetCurrentCharWeapon(ped,candidate,true);
            // Support grip belongs to the old gun. Require a fresh trigger
            // squeeze before a newly selected pistol can fire.
            supportGripping=false; supportWeapon=nullptr; triggerLatched=true;
            uint32_t selected=current;
            if(Natives::GetCurrentCharWeapon(ped,&selected) && selected==candidate) ++weaponSwaps;
            break;
        }
    }

    void* EquippedWeapon(void* ped,void*& entity)
    {
        // CPedWeaponMgr::GetCurrentWeapon (00B64E30), reached with ped+2B0.
        auto* manager=static_cast<char*>(ped)+0x2B0;
        const int slot=*reinterpret_cast<int*>(manager);
        entity=*reinterpret_cast<void**>(manager+0x14);
        if(!entity || slot<0 || slot>10) return nullptr;
        void* weapon=*reinterpret_cast<void**>(static_cast<char*>(entity)+0x25C);
        if(!weapon || *reinterpret_cast<int*>(static_cast<char*>(weapon)+0x18)!=
            *reinterpret_cast<int*>(manager+12*(slot+3))) return nullptr;
        return weapon;
    }

    char __fastcall FireWeapon(void* weapon,void* edx,void* shooter,const VRMath::Matrix* frame,
        const VRMath::Vector* origin,const VRMath::Vector* target,uint32_t force,uint32_t a,uint32_t b,uint32_t c,float damage)
    {
        if(!VRReloadRuntime::CanFire(shooter,weapon)) return 0;
        const auto fired=weaponFireHook.unsafe_fastcall<char>(weapon,edx,shooter,frame,origin,target,force,a,b,c,damage);
        if(fired) VRReloadRuntime::Fired(shooter,weapon);
        return fired;
    }

    float __fastcall BulletHit(void* weapon,void*,void* shooter,void* frame,void* hit,float damage)
    {
        void* local=nullptr;AcquireSRWLockShared(&targetLock);local=targetOwner;ReleaseSRWLockShared(&targetLock);
        const bool observed=shooter && shooter==local &&
            *reinterpret_cast<unsigned*>(static_cast<char*>(weapon)+0x18)==7;
        void* victim=nullptr;
        if(observed && hit)
        {
            auto* instance=*reinterpret_cast<unsigned char**>(hit);
            if(instance && (instance[0xA]&0x40)) victim=*reinterpret_cast<void**>(instance+0xC);
        }
        using Health=float (__thiscall*)(void*);
        Health health=victim ? reinterpret_cast<Health>((*reinterpret_cast<void***>(victim))[0xFC/4]):nullptr;
        const float before=health ? health(victim):0;
        const float applied=bulletHitHook.thiscall<float>(weapon,shooter,frame,hit,damage);
        if(observed)
        {
            bulletDamage=damage;bulletApplied=applied;bulletHealthBefore=before;
            bulletHealthAfter=health ? health(victim):0;
            bulletShape=hit ? *reinterpret_cast<unsigned short*>(static_cast<char*>(hit)+0x50):~0u;
            bulletPart=hit ? *reinterpret_cast<unsigned short*>(static_cast<char*>(hit)+0x52):~0u;
            ++bulletHits;
        }
        return applied;
    }

    void AimedPistolHeadshot(SafetyHookContext& regs)
    {
        // CE CA46E3: JNZ following the native player aiming-bit test. Reached
        // only after immunity, weapon-class, head/neck and player checks.
        // EDI is the damage event (shooter0, amount4, part8, weaponTypeC).
        // Carry VR aiming eligibility through the ordinary native branch;
        // do not write the player's aiming flags or multiply base damage.
        auto* event=reinterpret_cast<unsigned char*>(regs.edi);
        if(!event || *reinterpret_cast<unsigned*>(event+0xC)!=7 || !OpenXRVR::HasFocusedViews()) return;
        auto* shooter=*reinterpret_cast<void**>(event);
        if(!VRHands::Active(shooter)) return;
        ++aimedHeadshotChecks;
        constexpr uintptr_t ZeroFlag=0x40;
        if(regs.eflags & ZeroFlag) {regs.eflags &= ~ZeroFlag;++aimedHeadshotOverrides;}
    }

    char __fastcall FirePistol(void* weapon,void* edx,void* shooter,const VRMath::Matrix* frame,
        const VRMath::Vector* origin,const VRMath::Vector* target,uint32_t flag,uint32_t extra,float damage)
    {
        // Enter before the native spread/collision/damage path. Ammo and fire
        // cadence remain in the original outer CWeapon::Fire dispatcher.
        void* owner=nullptr; uint64_t at=0;
        AcquireSRWLockShared(&targetLock); owner=targetOwner; at=targetAt; ReleaseSRWLockShared(&targetLock);
        void* entity=nullptr;
        OpenXRVR::ControllerInput input{};
        if(shooter && shooter==owner && at && GetTickCount64()-at<=100 &&
            OpenXRVR::HasFocusedViews() && OpenXRVR::GetControllerInput(input) && input.gripPoseValid[1] &&
            EquippedWeapon(shooter,entity)==weapon &&
            *reinterpret_cast<int*>(static_cast<char*>(weapon)+0x18)==7 && origin && target && muzzleLookup && weaponRange)
        {
            auto* gun=*reinterpret_cast<VRMath::Matrix**>(static_cast<char*>(entity)+0x20);
            if(gun)
            {
                alignas(16) VRMath::Vector muzzle{0,0,0,1},end{};
                muzzleLookup(weapon,&muzzle,gun,3); // Authored gun_muzzle semantic.
                const float range=weaponRange(weapon,shooter);
                float length2=0,offset2=0;
                for(unsigned axis=0;axis<3;++axis)
                {
                    length2+=(*gun)[0][axis]*(*gun)[0][axis];
                    offset2+=(muzzle[axis]-(*gun)[3][axis])*(muzzle[axis]-(*gun)[3][axis]);
                }
                if(std::isfinite(range+length2+offset2) && range>.1f && range<2000.f &&
                    length2>.5f && length2<1.5f && offset2<4.f)
                {
                    const float scale=range/std::sqrt(length2);
                    for(unsigned axis=0;axis<3;++axis) end[axis]=muzzle[axis]+(*gun)[0][axis]*scale;
                    end[3]=1;
                    ++muzzleShots;
                    VRHandPhysics::WeaponQueryScope shotScope(shooter,entity);
                    return pistolFireHook.unsafe_fastcall<char>(weapon,edx,shooter,gun,&muzzle,&end,flag,extra,damage);
                }
            }
        }
        return pistolFireHook.unsafe_fastcall<char>(weapon,edx,shooter,frame,origin,target,flag,extra,damage);
    }

    void ProcessPistolTrigger()
    {
        // Semiautomatic: one request per squeeze, rearm on release. No pad
        // injection or aiming task; native Fire owns readiness/ammo/cadence.
        OpenXRVR::ControllerInput input{};
        if(!weaponFire || !OpenXRVR::HasFocusedViews() || !OpenXRVR::GetControllerInput(input) ||
            !input.gripPoseValid[1] || !input.fireActive) {triggerLatched=false; return;}
        if(input.fire<.3f) {triggerLatched=false; return;}
        if(input.fire<.7f || triggerLatched) return;
        triggerLatched=true;
        void* owner=nullptr; uint64_t at=0;
        AcquireSRWLockShared(&targetLock); owner=targetOwner; at=targetAt; ReleaseSRWLockShared(&targetLock);
        if(!owner || !at || GetTickCount64()-at>100) return;
        void* entity=nullptr;
        void* weapon=EquippedWeapon(owner,entity);
        if(!weapon || *reinterpret_cast<int*>(static_cast<char*>(weapon)+0x18)!=7) return;
        if(!VRReloadRuntime::CanFire(owner,weapon)) {VRReloadRuntime::EmptyTrigger(owner,weapon);return;}
        auto* gun=*reinterpret_cast<VRMath::Matrix**>(static_cast<char*>(entity)+0x20);
        if(!gun) return;
        alignas(16) VRMath::Vector muzzle{0,0,0,1},end{0,0,0,1};
        muzzleLookup(weapon,&muzzle,gun,3);
        const float range=weaponRange(weapon,owner);
        float length2=0,offset2=0;
        for(unsigned axis=0;axis<3;++axis)
        {
            length2+=(*gun)[0][axis]*(*gun)[0][axis];
            offset2+=(muzzle[axis]-(*gun)[3][axis])*(muzzle[axis]-(*gun)[3][axis]);
        }
        if(!std::isfinite(range+length2+offset2) || range<=.1f || range>=2000.f ||
            length2<.5f || length2>1.5f || offset2>=4.f) return;
        const float scale=range/std::sqrt(length2);
        for(unsigned axis=0;axis<3;++axis) end[axis]=muzzle[axis]+(*gun)[0][axis]*scale;
        ++triggerAttempts;
        VRHandPhysics::WeaponQueryScope shotScope(owner,entity);
        if(weaponFire(weapon,owner,gun,&muzzle,&end,0,0,0,0,-1.f)) ++triggerShots;
    }

    void ProcessPistolThreat()
    {
        auto reset=[] {aimCandidate=nullptr;aimDwell=0;aimSampleAt=0;aimRaised=false;aimHasTarget=false;};
        OpenXRVR::ControllerInput input{};OpenXRVR::EyeView eyes[2]{};long long display=0;
        if(!aimTarget || !aimEventCtor || !aimEventDtor || !aimAddEvent ||
            !OpenXRVR::HasFocusedViews() || !OpenXRVR::GetControllerInput(input) ||
            !input.gripPoseValid[1] || input.phoneActive || Natives::IsPauseMenuActive() ||
            !OpenXRVR::GetEyeViews(eyes,display)) {reset();return;}
        // Tracking-space height follows physical crouching and does not depend
        // on an animated shoulder or a vanilla aiming pose.
        const float head=(eyes[0].position[1]+eyes[1].position[1])*.5f;
        if(!std::isfinite(head+input.gripPosition[1][1]) ||
            input.gripPosition[1][1]<head-aimRaiseBelowHead) {reset();return;}
        void* owner=nullptr;uint64_t at=0;
        AcquireSRWLockShared(&targetLock);owner=targetOwner;at=targetAt;ReleaseSRWLockShared(&targetLock);
        if(!owner || !at || GetTickCount64()-at>100) {reset();return;}
        Ped player=0;Natives::GetPlayerChar(Natives::GetPlayerId(),&player);
        if(!player || !Natives::DoesCharExist(player) || Natives::IsCharDead(player) ||
            Natives::IsCharInAnyCar(player)) {reset();return;}
        void* entity=nullptr;void* weapon=EquippedWeapon(owner,entity);
        if(!weapon || *reinterpret_cast<unsigned*>(static_cast<char*>(weapon)+0x18)!=7) {reset();return;}
        auto* gun=*reinterpret_cast<VRMath::Matrix**>(static_cast<char*>(entity)+0x20);
        if(!gun) {reset();return;}
        alignas(16) VRMath::Vector muzzle{0,0,0,1},end{0,0,0,1};
        muzzleLookup(weapon,&muzzle,gun,3);
        const float length=VRMath::Length3((*gun)[0]);
        const float range=weaponRange(weapon,owner);
        const float offset=VRMath::Length3(VRMath::Difference3(muzzle,(*gun)[3]));
        if(!std::isfinite(length+range+offset) || length<.7f || length>1.3f ||
            range<=.1f || range>=2000.f || offset>2.f) {reset();return;}
        for(unsigned a=0;a<3;++a) end[a]=muzzle[a]+(*gun)[0][a]*(std::min(range,20.f)/length);
        void* victim=nullptr;
        {
            // Use the same native ped/occupant and obstruction query as
            // CTaskComplexPlayerGun. Exclude only our own rig and weapon.
            VRHandPhysics::WeaponQueryScope query(owner,entity);
            victim=aimTarget(owner,&muzzle,&end,false,true);
        }
        ++aimQueries;aimRaised=true;
        auto* ped=static_cast<unsigned char*>(victim);
        if(!ped || victim==owner || (*reinterpret_cast<uint32_t*>(ped+0x28)&0x3C0)!=0xC0 ||
            (ped[0x268]&1) || !*reinterpret_cast<void**>(ped+0x224))
        {aimCandidate=nullptr;aimDwell=0;aimSampleAt=0;aimHasTarget=false;return;}
        aimHasTarget=true;
        const auto now=GetTickCount64();
        // Never dereference a retained candidate: each frame obtains a fresh
        // native result. Long gaps and target changes restart the dwell.
        if(victim!=aimCandidate || !aimSampleAt || now-aimSampleAt>100)
        {aimCandidate=victim;aimDwell=0;aimSampleAt=now;return;}
        float dt=0;Natives::GetFrameTime(&dt);
        if(!std::isfinite(dt) || dt<0) {reset();return;}
        aimDwell+=std::min(dt,.05f);aimSampleAt=now;
        auto* frame=*reinterpret_cast<VRMath::Matrix**>(ped+0x20);
        if(!frame) {reset();return;}
        const float distance=VRMath::Length3(VRMath::Difference3((*frame)[3],(*gun)[3]));
        // Native player-gun dwell: .2s nearby, rising to2s at20m.
        const float delay=std::max(0.f,(distance-5.f)/15.f)*1.8f+.2f;
        if(!std::isfinite(distance) || distance>20.f || aimDwell<delay) return;
        alignas(16) unsigned char event[0x20]{};
        aimEventCtor(event,owner);
        auto* group=static_cast<char*>(*reinterpret_cast<void**>(ped+0x224))+0x84;
        if(aimAddEvent(group,event,false,true))
        {++aimEvents;if((ped[0x26C]&4) && *reinterpret_cast<void**>(ped+0xB30)) ++aimVehicleEvents;}
        aimEventDtor(event);aimDwell=0;
    }

    void ResolveFingerRig(void* skeleton)
    {
        if(!fingersEnabled) return;
        void* definition=*reinterpret_cast<void**>(static_cast<char*>(skeleton)+4);
        const unsigned count=*reinterpret_cast<unsigned short*>(static_cast<char*>(skeleton)+0x10);
        if(!definition || !count || count>256) return;
        auto* bones=*reinterpret_cast<char**>(definition);
        if(!bones) return;
        if(fingerRig.definition!=definition)
        {
            fingerRig={}; fingerRig.definition=definition;
            fingerJointCounts[0]=fingerJointCounts[1]=0;
            for(unsigned side=0;side<2;++side)
            {
                for(unsigned finger=0;finger<5;++finger)
                {
                    unsigned parent=UINT_MAX;
                    for(unsigned joint=0;joint<3;++joint)
                    {
                        char name[32];
                        if(joint==0) sprintf_s(name,"Char_%c_Finger%u",side==0 ? 'L':'R',finger);
                        else sprintf_s(name,"Char_%c_Finger%u%u",side==0 ? 'L':'R',finger,joint);
                        bool found=false;
                        for(unsigned bone=0;bone<count;++bone)
                        {
                            const auto descriptor=bones+bone*0xE0;
                            const char* actual=*reinterpret_cast<const char**>(descriptor);
                            if(!actual || strcmp(name,actual)!=0) continue;
                            auto* ancestor=*reinterpret_cast<char**>(descriptor+0x10);
                            const auto ancestorOffset=reinterpret_cast<uintptr_t>(ancestor)-reinterpret_cast<uintptr_t>(bones);
                            if(!ancestor || ancestorOffset%0xE0 || ancestorOffset/0xE0>=count) break;
                            const unsigned ancestorIndex=static_cast<unsigned>(ancestorOffset/0xE0);
                            if(joint && ancestorIndex!=parent) break;
                            float position[3],quaternion[4];
                            std::memcpy(position,descriptor+0x20,sizeof(position));
                            std::memcpy(quaternion,descriptor+0x40,sizeof(quaternion));
                            bool finite=true;
                            for(float v:position) finite &= std::isfinite(v);
                            for(float v:quaternion) finite &= std::isfinite(v);
                            const float norm=quaternion[0]*quaternion[0]+quaternion[1]*quaternion[1]+
                                quaternion[2]*quaternion[2]+quaternion[3]*quaternion[3];
                            if(!finite || norm<1.e-8f) break;
                            FingerJoint value{bone,ancestorIndex,finger,joint,VRMath::Pose(position,quaternion)};
                            fingerRig.joints[side][fingerRig.count[side]++]=value;
                            parent=bone; found=true; break;
                        }
                        if(!found) break;
                    }
                }
                // Partial chains are not a supported rig; retain native fingers.
                if(fingerRig.count[side]!=15) fingerRig.count[side]=0;
                fingerJointCounts[side]=fingerRig.count[side];
            }
        }
    }

    void UpdateFingers(VRMath::Matrix* matrices,unsigned hand,int wrist,const OpenXRVR::ControllerInput& input,
        const VRMath::Matrix* nativePose,const VRMath::Matrix& nativeWrist,bool supporting=false,bool sliding=false)
    {
        if(!fingersEnabled || fingerRig.count[hand]!=15) return;
        for(const auto& finger:fingerRig.joints[hand])
        {
            if(finger.joint==0 && finger.parent!=static_cast<unsigned>(wrist)) return;
        }
        const bool magazineGrabbing=!nativePose && hand==0 && VRReloadRuntime::HoldsMagazine();
        const bool propGrabbing=!nativePose && VRHandPhysics::IsGrabbing(hand);
        const bool holding=propGrabbing || magazineGrabbing || sliding;
        const bool closed=!nativePose && (holding || supporting || (input.gripActive[hand] && input.gripped[hand]));
        const float trigger=supporting ? 1.f : (input.triggerActive[hand] ? input.trigger[hand] : 0.f);
        const bool touched=supporting || (input.padTouched[hand] && input.active[hand]);
        unsigned index=0;
        const auto wristDelta=VRMath::Multiply(VRMath::InverseRigid(VRMath::AffineXYZ(nativeWrist)),
            VRMath::AffineXYZ(matrices[wrist]));
        for(const auto& finger:fingerRig.joints[hand])
        {
            auto local=finger.rest;
            // An acquired prop/magazine owns a holding pose for ALL fingers,
            // independent of trigger/pad input. Otherwise retain free-hand
            // animation and the game's authored equipped-weapon grip.
            const float amount=sliding ? (finger.finger==0 ? .8f:.65f) : magazineGrabbing ? (finger.finger==0 ? .85f:.75f) :
                propGrabbing ? (finger.finger==0 ? 1.f:.6f) : finger.finger==0 ? (touched ? 1.f:0.f) :
                (finger.finger==1 ? trigger : (closed ? 1.f:0.f));
            const float curl=finger.finger==0 ?
                (holding ? (finger.joint==0 ? 30.f:15.f) :
                    (finger.joint==0 ? 25.f+20.f*(supporting ? 0.f:input.trackpad[hand][1]) : 20.f)) :
                (finger.joint==0 ? 65.f : (finger.joint==1 ? 85.f:60.f));
            const float lateral=!holding && !supporting && finger.finger==0 && finger.joint==0 && touched ?
                20.f*input.trackpad[hand][0]*(hand==0 ? 1.f:-1.f) : 0.f;
            const float origin[3]={};
            const float curlHalf=curl*amount*.00872664626f;
            const float lateralHalf=lateral*.00872664626f;
            const float curlQuaternion[4]={0,0,std::sin(curlHalf),std::cos(curlHalf)};
            const float lateralQuaternion[4]={std::sin(lateralHalf),0,0,std::cos(lateralHalf)};
            auto rotation=local; rotation[3]={0,0,0,1};
            rotation=VRMath::Multiply(VRMath::Multiply(VRMath::Pose(origin,lateralQuaternion),
                VRMath::Pose(origin,curlQuaternion)),rotation);
            rotation[3]=local[3]; local=rotation;
            // Carry the game's weapon-holding fingers with the tracked wrist.
            // Simply skipping writes would leave their old world transforms.
            const auto world=nativePose ? VRMath::Multiply(VRMath::AffineXYZ(nativePose[index]),wristDelta) :
                VRMath::Multiply(local,VRMath::AffineXYZ(matrices[finger.parent]));
            // Native matrix helpers preserve the fourth lane of each row.
            // Keep those homogeneous/metadata lanes rather than replacing them.
            for(unsigned row=0;row<4;++row)
                for(unsigned axis=0;axis<3;++axis) matrices[finger.bone][row][axis]=world[row][axis];
            ++index;
        }
        fingerClosed[hand]=closed; ++fingerUpdates[hand];
    }

    bool ResolveMeleeProfile(void* held,uint32_t type,VRMelee::Profile& profile)
    {
        if(type==3) {profile=VRMelee::Knife;return true;}
        if(type!=1 && type!=2) return false;
        const auto index=*reinterpret_cast<short*>(static_cast<char*>(held)+0x2E);
        if(index<0) return false;
        auto* models=reinterpret_cast<void**>(reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr))+(0x01295CD8-0x00400000));
        auto* model=static_cast<char*>(models[index]);
        if(!model) return false;
        auto* fragment=*reinterpret_cast<char**>(model+8);
        void* drawable=nullptr;
        if(fragment) drawable=*reinterpret_cast<void**>(fragment+0xB4);
        else if(auto* slot=*reinterpret_cast<void***>(model+0xC)) drawable=*slot;
        if(!drawable) return false;
        VRMath::Vector low{},high{};
        std::memcpy(low.data(),static_cast<char*>(drawable)+0x20,12);
        std::memcpy(high.data(),static_cast<char*>(drawable)+0x30,12);
        return VRMelee::ClubProfile(type,low,high,profile);
    }

    bool ResolveSupportGrip(void* model)
    {
        if(model==supportModel) return supportReady;
        supportModel=model; supportReady=false;
        // The pistol drawable owns the authored gun_grip / gun_grip2 frames.
        // CPed+B30 and model+CC belong to a different held-entity path.
        auto* fragment=*reinterpret_cast<char**>(static_cast<char*>(model)+8);
        void* drawable=nullptr;
        if(fragment) drawable=*reinterpret_cast<void**>(fragment+0xB4);
        else
        {
            auto* slot=*reinterpret_cast<void***>(static_cast<char*>(model)+0xC);
            if(slot) drawable=*slot;
        }
        if(!drawable) return false;
        auto* definition=*reinterpret_cast<char**>(static_cast<char*>(drawable)+0xC);
        if(!definition) return false;
        const unsigned count=*reinterpret_cast<unsigned short*>(definition+0x14);
        auto* bones=*reinterpret_cast<char**>(definition);
        auto* locals=*reinterpret_cast<VRMath::Matrix**>(definition+0x10);
        if(!bones || !locals || !count || count>64) return false;
        int primary=-1,secondary=-1;
        for(unsigned bone=0;bone<count;++bone)
        {
            auto* name=*reinterpret_cast<const char**>(bones+0xE0*bone);
            if(!name) continue;
            if(std::strcmp(name,"gun_grip")==0) primary=bone;
            if(std::strcmp(name,"gun_grip2")==0) secondary=bone;
        }
        if(primary<0 || secondary<0) return false;
        supportRelative=VRMath::Multiply(VRMath::AffineXYZ(locals[secondary]),
            VRMath::InverseRigid(VRMath::AffineXYZ(locals[primary])));
        for(const auto& row:supportRelative) for(float value:row)
            if(!std::isfinite(value)) return false;
        supportReady=true;
        return true;
    }

    VRMath::Matrix Rotation(int x,int y,int z)
    {
        auto rotation=VRMath::Identity();
        const int degrees[3]={x,y,z};
        const float position[3]={};
        for(unsigned axis=0;axis<3;++axis)
        {
            const float half=degrees[axis]*.00872664626f;
            float quaternion[4]={0,0,0,std::cos(half)};
            quaternion[axis]=std::sin(half);
            rotation=VRMath::Multiply(rotation,VRMath::Pose(position,quaternion));
        }
        return rotation;
    }

    void __fastcall UpdateIK(void* manager,void*)
    {

        scaledPose.pending=false;
        scaledPose.captured=false;
        // A freed/replaced rig is never followed through a cached pointer.
        // Only this same local owner is allowed to restore its prior output.
        if(scaledPose.count && manager==static_cast<char*>(scaledPose.owner)+0xBB0 &&
            *reinterpret_cast<void**>(static_cast<char*>(manager)+0x40)==scaledPose.owner)
        {
            auto* skeleton=PlayerSkeleton(scaledPose.owner);
            if(skeleton && *reinterpret_cast<void**>(static_cast<char*>(skeleton)+0x14)==scaledPose.matrices)
                RestoreBodyScale();
            else scaledPose.count=0;
        }
        // Preserve the native finishing callback's exact context/ABI, but run
        // its arm twist/support-bone updates after the tracked primary bones.
        // The guard also finishes the native pose on every early return.
        FinishTrackedPose finish;
        AcquireSRWLockShared(&targetLock);
        deferFinishPose=finishPoseHook && enabled && targetOwner && targetAt &&
            GetTickCount64()-targetAt<=100 && OpenXRVR::HasFocusedViews() &&
            static_cast<char*>(manager)==static_cast<char*>(targetOwner)+0xBB0;
        ReleaseSRWLockShared(&targetLock);
        // Finish ordinary animation/IK first. Override only the local player's
        // arms on this native skeleton callback, never once per rendered eye.
        updateHook.thiscall<void>(manager);
        if(!enabled || !solveArm || !OpenXRVR::HasFocusedViews()) {supportGripping=false; return;}
        VRMath::Matrix transform{},correction[2]{}; void* owner=nullptr; uint64_t at=0;
        AcquireSRWLockShared(&targetLock);
        transform=trackingToWorld; owner=targetOwner; at=targetAt;
        correction[0]=wristCorrection[0]; correction[1]=wristCorrection[1];
        ReleaseSRWLockShared(&targetLock);
        if(!owner || !at || GetTickCount64()-at>100 ||
            static_cast<char*>(manager)!=static_cast<char*>(owner)+0xBB0 ||
            *reinterpret_cast<void**>(static_cast<char*>(manager)+0x40)!=owner) return;
        // Same skeleton ownership as the native arm solver: fragment instance
        // virtual A0/E0 first, inline entity skeleton only as a fallback.
        void* skeleton=PlayerSkeleton(owner);
        if(!skeleton || !*reinterpret_cast<void**>(static_cast<char*>(skeleton)+0x14)) return;
        auto* matrices=*reinterpret_cast<VRMath::Matrix**>(static_cast<char*>(skeleton)+0x14);

        auto* root=*reinterpret_cast<VRMath::Matrix**>(static_cast<char*>(owner)+0x20);
        if(!root) return;
        ResolveBodyHeight(skeleton,owner);
        float scale=1,bottom=0;
        AcquireSRWLockShared(&targetLock);
        if(fitBodyHeight && owner==bodyDimensionsOwner) {scale=bodyScale; bottom=bodyBottom;}
        ReleaseSRWLockShared(&targetLock);
        VRMath::Vector pivot=(*root)[3];
        for(unsigned axis=0;axis<3;++axis) pivot[axis]+=bottom*(*root)[2][axis];
        const unsigned boneCount=*reinterpret_cast<unsigned short*>(static_cast<char*>(skeleton)+0x10);
        if((scale!=1 || trackedBody) && boneCount && boneCount<=255)
        {
            scaledPose.owner=owner; scaledPose.matrices=matrices; scaledPose.count=boneCount;
            scaledPose.pending=true;
            scaledPose.pivot=pivot; scaledPose.scale=scale;
        }
        const auto modelIndex=*reinterpret_cast<short*>(static_cast<char*>(owner)+0x2E);
        if(modelIndex<0) return;
        auto* models= reinterpret_cast<void**>(reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr))+
            (0x01295CD8-0x00400000));
        void* model=models[modelIndex];
        if(!model) return;
        if(scaledPose.pending)
        {
            NeutralStandingPose(skeleton,owner,matrices,*root);
            LowerTrackedTorso(skeleton,matrices,*root,pivot,scale);
        }
        using BoneMapper=int (__thiscall*)(void*,int);
        auto mapper=reinterpret_cast<BoneMapper>((*reinterpret_cast<void***>(model))[0x38/4]);
        OpenXRVR::ControllerInput input{};
        if(!OpenXRVR::GetControllerInput(input)) return;
        ResolveFingerRig(skeleton);
        void* held=nullptr;
        void* weapon=EquippedWeapon(owner,held);
        physicsRig={}; physicsRig.owner=owner; physicsRig.held=held; physicsRig.matrices=matrices;
        physicsRig.trackingTransform=transform; physicsRig.sampledAt=input.sampledAt;
        finish.publish=true;
        const bool holdingWeapon=weapon && *reinterpret_cast<int*>(static_cast<char*>(weapon)+0x18)!=0;
        weaponGrip=holdingWeapon;
        ++updates;
        alignas(16) const VRMath::Vector offset{0,0,0,0};
        VRMath::Matrix targets[2]{};
        for(unsigned hand=0;hand<2;++hand)
        {
            if(!input.gripPoseValid[hand]) continue;
            const auto grip=VRMath::Multiply(
                VRMath::Pose(input.gripPosition[hand],input.gripOrientation[hand]),transform);
            // Provisional attachment basis X=right,Y=forward,Z=up; the live
            // wrist correction calibrates the rig's authored hand axes.
            // OpenXR grip frames use X/Y/Z with -Z as their longitudinal axis.
            // Feed the native solver that attachment convention, retaining the
            // exact controller position. The runtime owns the Wand grip tilt.
            auto target=grip;
            for(unsigned axis=0;axis<3;++axis)
            {
                target[1][axis]=-grip[2][axis];
                target[2][axis]=grip[1][axis];
            }
            target=VRMath::Multiply(correction[hand],target);
            targets[hand]=target;
        }
        bool supporting=false;
        supportDistance=-1.f;
        if(supportOwner!=owner || supportWeapon!=held) supportGripping=false;
        supportOwner=owner; supportWeapon=held;
        if(supportGripEnabled && holdingWeapon && input.gripPoseValid[0] && input.gripPoseValid[1] &&
            *reinterpret_cast<int*>(static_cast<char*>(weapon)+0x18)==7)
        {
            const auto weaponModelIndex=*reinterpret_cast<short*>(static_cast<char*>(held)+0x2E);
            if(weaponModelIndex>=0 && models[weaponModelIndex] && ResolveSupportGrip(models[weaponModelIndex]))
            {
                const auto supportTarget=VRMath::Multiply(supportRelative,targets[1]);
                float distanceSquared=0;
                for(unsigned axis=0;axis<3;++axis)
                {
                    const float difference=targets[0][3][axis]-supportTarget[3][axis];
                    distanceSquared+=difference*difference;
                }
                supportDistance=std::sqrt(distanceSquared);
                // Proximity permits acquisition; the held grip button owns
                // release. Do not detach when the physical support Wand moves.
                supporting=input.support && !VRReloadRuntime::UsesLeftHand() && !VRHandPhysics::IsGrabbing(0) &&
                    (supportGripping || distanceSquared<.12f*.12f);
                if(supporting) targets[0]=supportTarget;
            }
        }
        supportGripping=supporting;
        physicsRig.supporting[0]=supporting;
        for(unsigned hand=0;hand<2;++hand)
        {
            // Keep the tracked pose before collision feedback/IK. Physics must
            // chase the Wand, never its own collision-resolved wrist rotation.
            physicsRig.requested[hand]=targets[hand];
            if(input.gripPoseValid[hand]) VRHandPhysics::Resolve(owner,hand,targets[hand]);
        }
        VRMath::Matrix solvedRight{};
        bool rightSolved=false;
        VRMath::Matrix slideRelative{};
        const bool sliding=VRReloadRuntime::SlideGrip(owner,held,slideRelative);
        physicsRig.supporting[0]=supporting || sliding;
        for(unsigned order=0;order<2;++order)
        {
            const unsigned hand=1-order;
            if(!input.gripPoseValid[hand]) continue;
            alignas(16) auto target=targets[hand];
            // Solve in the original rig's space, then fit the finished body.
            // The final wrist position remains exactly at the tracked Wand.
            for(unsigned axis=0;axis<3;++axis)
                target[3][axis]=pivot[axis]+(target[3][axis]-pivot[axis]-
                    (trackedBody && axis==2 ? bodyVerticalOffset:0.f))/scale;
            if(hand==0 && (supporting || sliding))
            {
                if(!rightSolved) {supportGripping=false; continue;}
                // Anchor to the actual solved wrist, not the requested Wand
                // pose: native reach limits can move the pistol hand away
                // from that request. Express attachment spacing before fit.
                auto relative=sliding ? slideRelative:supportRelative;
                for(unsigned axis=0;axis<3;++axis) relative[3][axis]/=scale;
                target=VRMath::Multiply(relative,solvedRight);
            }
            const auto absoluteTarget=target;
            const int wrist=mapper(model,hand==0 ? 15 : 11);
            if(wrist<0 || wrist>=255) continue;
            VRMath::Matrix nativePose[15];
            const bool preserveGrip=hand==1 && holdingWeapon && fingerRig.count[hand]==15;
            if(preserveGrip)
                for(unsigned joint=0;joint<15;++joint) nativePose[joint]=matrices[fingerRig.joints[hand][joint].bone];
            const auto nativeWrist=matrices[wrist];
            // 004687A0 rotates the existing bone in entity-local space; the
            // solver does not accept an absolute wrist orientation. Cancel the
            // current animated wrist before applying the desired world frame.
            auto current=VRMath::AffineXYZ(matrices[wrist]);
            auto rootRotation=VRMath::AffineXYZ(*root);
            current[3]={0,0,0,1}; rootRotation[3]={0,0,0,1};
            auto desired=target; desired[3]={0,0,0,1};
            const auto delta=VRMath::Multiply(VRMath::Multiply(rootRotation,
                VRMath::InverseRigid(current)),desired);
            for(unsigned row=0;row<3;++row) target[row]=delta[row];
            bool finite=true;
            for(const auto& row:target) for(float value:row) finite &= std::isfinite(value);
            if(finite)
            {
                if(solveArm(manager,hand==0 ? 1 : 0,offset.data(),target.data()))
                    ++solves[hand];
                // Tracking owns the physics target even if native IK cannot
                // reach it. A failed solve must not destroy its collider.
                physicsRig.wrist[hand]=wrist;
                auto* definition=*reinterpret_cast<void**>(static_cast<char*>(skeleton)+4);
                auto* bones=definition ? *reinterpret_cast<char**>(definition):nullptr;
                auto* parent=bones ? *reinterpret_cast<char**>(bones+0xE0*wrist+0x10):nullptr;
                const auto parentOffset=reinterpret_cast<uintptr_t>(parent)-reinterpret_cast<uintptr_t>(bones);
                if(parent && parentOffset%0xE0==0 && parentOffset/0xE0<boneCount)
                {physicsRig.elbow[hand]=static_cast<int>(parentOffset/0xE0); physicsRig.valid[hand]=true;}
                if(hand==1)
                {
                    solvedRight=VRMath::AffineXYZ(matrices[wrist]);
                    rightSolved=true;
                    for(const auto& row:solvedRight) for(float value:row)
                        rightSolved &= std::isfinite(value);
                }
                if(hand==0 && (supporting || sliding))
                {
                    // IK positions the elbow/arm, but its endpoint clamp must
                    // not make an attached hand slide over the gun. Retain
                    // native W lanes and carry fingers from this locked wrist.
                    for(unsigned row=0;row<4;++row)
                        for(unsigned axis=0;axis<3;++axis)
                            matrices[wrist][row][axis]=absoluteTarget[row][axis];
                }
                UpdateFingers(matrices,hand,wrist,input,preserveGrip ? nativePose : nullptr,nativeWrist,hand==0 && supporting,hand==0 && sliding);
            }
        }
    }
}


namespace
{
    void PublishPhysicsPose()
    {
        if(physicsRig.valid[0] && physicsRig.valid[1])
            VRReloadRuntime::Publish(physicsRig.owner,physicsRig.trackingTransform,physicsRig.requested);
        for(unsigned hand=0;hand<2;++hand)
        {
            if(physicsRig.valid[hand])
            {
                AcquireSRWLockExclusive(&wristLock);
                publishedWrists[hand]=VRMath::AffineXYZ(physicsRig.matrices[physicsRig.wrist[hand]]);
                publishedWristAt[hand]=GetTickCount64();
                ReleaseSRWLockExclusive(&wristLock);
            }
            if(!physicsRig.valid[hand] || fingerRig.count[hand]!=15) continue;
            const auto wrist=physicsRig.matrices[physicsRig.wrist[hand]][3];
            const auto elbow=physicsRig.matrices[physicsRig.elbow[hand]][3];
            auto shift=VRMath::Difference3(physicsRig.requested[hand][3],wrist);
            VRMath::Vector points[VRHandPhysics::NodeCount]{};
            points[0]=physicsRig.requested[hand][3];
            unsigned tip=2;
            for(unsigned i=0;i<15;++i)
            {
                const auto& finger=fingerRig.joints[hand][i];
                const auto position=physicsRig.matrices[finger.bone][3];
                if(finger.joint==0)
                    for(unsigned axis=0;axis<3;++axis) points[1][axis]+=position[axis]*.2f;
                if(finger.joint==2)
                {
                    const auto parent=physicsRig.matrices[finger.parent][3];
                    // Extend the distal phalanx by its measured preceding
                    // segment length, covering the mesh's fingertip region.
                    for(unsigned axis=0;axis<3;++axis)
                        points[tip][axis]=position[axis]+(position[axis]-parent[axis])*.75f;
                    ++tip;
                }
            }
            for(unsigned i=7;i<10;++i)
                for(unsigned axis=0;axis<3;++axis)
                    points[i][axis]=wrist[axis]+(elbow[axis]-wrist[axis])*((i-6)/3.f);
            for(unsigned i=1;i<VRHandPhysics::ContactNodeCount;++i)
                for(unsigned axis=0;axis<3;++axis) points[i][axis]+=shift[axis];
            const auto gripWrist=VRMath::AffineXYZ(physicsRig.requested[hand]);
            // Palm/fingertip contacts also use tracked orientation. Translation
            // alone would leave their targets rotated with the blocked hand.
            auto solvedWrist=physicsRig.matrices[physicsRig.wrist[hand]];
            if(VRGrip::Rigid(solvedWrist))
            {
                const auto toTracked=VRMath::Multiply(VRMath::InverseRigid(solvedWrist),gripWrist);
                for(unsigned i=1;i<7;++i)
                {
                    auto point=points[i];
                    for(unsigned axis=0;axis<3;++axis) point[axis]-=shift[axis];
                    point[3]=1;points[i]=VRMath::Transform(point,toTracked);
                }
            }
            points[VRHandPhysics::ContactNodeCount]=gripWrist[3];
            VRHandPhysics::Publish(physicsRig.owner,physicsRig.held,hand,points,gripWrist,physicsRig.supporting[hand]);
        }
        // Use the rendered equipped entity, not a gun-muzzle semantic (the
        // knife has none). Transform back to tracking space for motion gating
        // so locomotion with a stationary blade cannot become an attack.
        void* held=nullptr;
        void* weapon=EquippedWeapon(physicsRig.owner,held);
        VRMelee::Profile profile{};
        if(held && held==physicsRig.held && weapon && ResolveMeleeProfile(held,
            *reinterpret_cast<int*>(static_cast<char*>(weapon)+0x18),profile))
        {
            auto* frame=*reinterpret_cast<VRMath::Matrix**>(static_cast<char*>(held)+0x20);
            if(frame)
            {
                const auto world=VRMelee::SampleBlade(profile,VRMath::AffineXYZ(*frame));
                auto tracked=world;
                const auto inverse=VRMath::InverseRigid(physicsRig.trackingTransform);
                for(auto& point:tracked) point=VRMath::Transform(point,inverse);
                VRHandPhysics::PublishBlade(physicsRig.owner,held,world.data(),tracked.data(),physicsRig.sampledAt,profile);
            }
        }
    }
}

void VRHands::PublishTrackingTransform(const VRMath::Matrix& transform,void* owner,bool active)
{
    if(!enabled) return;
    AcquireSRWLockExclusive(&targetLock);
    trackingToWorld=transform; targetOwner=active ? owner : nullptr;
    targetAt=active ? GetTickCount64() : 0;
    if(!active) supportGripping=false;
    ReleaseSRWLockExclusive(&targetLock);
}

bool VRHands::GetWrist(unsigned hand,VRMath::Matrix& world)
{
    if(hand>1 || !OpenXRVR::HasFocusedViews()) return false;
    AcquireSRWLockShared(&wristLock);
    world=publishedWrists[hand];
    const auto at=publishedWristAt[hand];
    ReleaseSRWLockShared(&wristLock);
    return at && GetTickCount64()-at<=100;
}

bool VRHands::Active(void* owner)
{
    AcquireSRWLockShared(&targetLock);
    const bool active=enabled && trackedBody && owner && owner==targetOwner && targetAt &&
        GetTickCount64()-targetAt<=100;
    ReleaseSRWLockShared(&targetLock);
    return active;
}

void VRHands::SetTraversalState(void* owner,bool moving,bool neutralAllowed)
{
    AcquireSRWLockExclusive(&targetLock);
    const auto now=GetTickCount64();
    const bool idle=neutralAllowed && !moving;
    if(owner!=traversalOwner || !idle || !neutralIdle || now-traversalAt>100)
        idleSince=idle ? now:0;
    traversalOwner=owner; traversalAt=now; neutralIdle=idle;
    ReleaseSRWLockExclusive(&targetLock);
}

bool VRHands::StationaryBodyHeading(void* owner,float current,float& heading)
{
    if(!Active(owner)) return false;
    OpenXRVR::ControllerInput input{}; OpenXRVR::EyeView eyes[2]{}; long long time=0;
    if(!OpenXRVR::GetControllerInput(input) || !input.gripPoseValid[0] || !input.gripPoseValid[1] ||
        !OpenXRVR::GetEyeViews(eyes,time)) return false;
    VRMath::Vector hands{},head{};
    for(unsigned axis=0;axis<3;++axis)
    {
        hands[axis]=(input.gripPosition[0][axis]+input.gripPosition[1][axis])*.5f;
        head[axis]=(eyes[0].position[axis]+eyes[1].position[axis])*.5f;
    }
    auto direction=VRMath::Difference3(hands,head);
    AcquireSRWLockShared(&targetLock);
    direction=VRMath::Transform(direction,trackingToWorld);
    ReleaseSRWLockShared(&targetLock);
    // Hands at the sides or close to the chest have no reliable torso demand.
    if(std::hypot(direction[0],direction[1])<.22f) return false;
    const float demand=std::atan2(-direction[0],direction[1]);
    const float difference=std::remainder(demand-current,2.f*3.14159265358979323846f);
    constexpr float slack=35.f*3.14159265358979323846f/180.f;
    if(std::abs(difference)<=slack) return false;
    // Only body yaw eases; HMD and wrist poses remain immediate.
    const float excess=difference-std::copysign(slack,difference);
    heading=current+std::clamp(excess,-.06f,.06f);
    return true;
}

std::string VRHands::Status()
{
    int angles[3];
    bool captured[2];
    VRMath::Matrix corrections[2];
    AcquireSRWLockShared(&targetLock);
    std::memcpy(angles,correctionDegrees,sizeof(angles));
    std::memcpy(captured,calibrated,sizeof(captured));
    corrections[0]=wristCorrection[0]; corrections[1]=wristCorrection[1];
    ReleaseSRWLockShared(&targetLock);
    char text[800];
    sprintf_s(text,"{\"enabled\":%s,\"hookReady\":%s,\"updates\":%u,\"leftSolves\":%u,\"rightSolves\":%u,\"rotationDegrees\":[%d,%d,%d],\"calibrated\":[%s,%s],\"correctionRows\":[[%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f],[%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f]]}",
        enabled ? "true" : "false",updateHook ? "true" : "false",updates.load(),solves[0].load(),solves[1].load(),angles[0],angles[1],angles[2],
        captured[0] ? "true" : "false",captured[1] ? "true" : "false",
        corrections[0][0][0],corrections[0][0][1],corrections[0][0][2],corrections[0][1][0],corrections[0][1][1],corrections[0][1][2],corrections[0][2][0],corrections[0][2][1],corrections[0][2][2],
        corrections[1][0][0],corrections[1][0][1],corrections[1][0][2],corrections[1][1][0],corrections[1][1][1],corrections[1][1][2],corrections[1][2][0],corrections[1][2][1],corrections[1][2][2]);
    std::string result=text;
    result.pop_back();
    char fingers[360];
    sprintf_s(fingers,",\"fingersEnabled\":%s,\"fingerJoints\":[%u,%u],\"fingerUpdates\":[%u,%u],\"closed\":[%s,%s],\"weaponGrip\":%s,\"pistolAimReady\":%s,\"muzzleShots\":%u,\"triggerReady\":%s,\"triggerAttempts\":%u,\"triggerShots\":%u}",
        fingersEnabled ? "true":"false",fingerJointCounts[0].load(),fingerJointCounts[1].load(),
        fingerUpdates[0].load(),fingerUpdates[1].load(),fingerClosed[0].load() ? "true":"false",fingerClosed[1].load() ? "true":"false",
        weaponGrip.load() ? "true":"false",pistolFireHook ? "true":"false",muzzleShots.load(),
        weaponFire ? "true":"false",triggerAttempts.load(),triggerShots.load());
    result+=fingers;
    char body[400];
    AcquireSRWLockShared(&targetLock);
    sprintf_s(body,",\"bodyHeightEnabled\":%s,\"playerHeight\":%.4f,\"nativeBodyHeight\":%.4f,\"bodyScale\":%.5f,\"poseFinishReady\":%s,\"poseFinishUpdates\":%u,\"trackedBodyEnabled\":%s,\"torsoDrop\":%.4f,\"legUpdates\":%u,\"neutralPoseUpdates\":%u}",
        fitBodyHeight ? "true":"false",configuredHeight,bodyNativeHeight,bodyScale,
        finishPoseHook ? "true":"false",finishPoseUpdates.load(),trackedBody ? "true":"false",torsoDrop.load(),legUpdates.load(),neutralPoseUpdates.load());
    ReleaseSRWLockShared(&targetLock);
    result.pop_back(); result+=body;
    char support[160];
    sprintf_s(support,",\"supportReady\":%s,\"supportGripping\":%s,\"supportDistance\":%.4f}",
        supportReady.load() ? "true":"false",supportGripping.load() ? "true":"false",supportDistance.load());
    result.pop_back(); result+=support;
    char swapping[100];
    sprintf_s(swapping,",\"weaponSwapEnabled\":%s,\"weaponSwaps\":%u}",
        weaponSwapEnabled ? "true":"false",weaponSwaps.load());
    result.pop_back(); result+=swapping;
    result.pop_back(); result+=",\"physics\":"+VRHandPhysics::Status()+"}";
    return result;
}

bool VRHands::SetRotation(int x,int y,int z)
{
    if(!enabled || x < -180 || x > 180 || y < -180 || y > 180 || z < -180 || z > 180) return false;
    const auto rotation=Rotation(x,y,z);
    const int degrees[3]={x,y,z};
    AcquireSRWLockExclusive(&targetLock);
    wristCorrection[0]=wristCorrection[1]=rotation;
    calibrated[0]=calibrated[1]=false;
    std::memcpy(correctionDegrees,degrees,sizeof(degrees));
    ReleaseSRWLockExclusive(&targetLock);
    return true;
}

bool VRHands::Trim(unsigned hand,int x,int y,int z)
{
    if(!enabled || hand>1 || x < -45 || x > 45 || y < -45 || y > 45 || z < -45 || z > 45) return false;
    const auto trim=Rotation(x,y,z);
    AcquireSRWLockExclusive(&targetLock);
    wristCorrection[hand]=VRMath::Multiply(trim,wristCorrection[hand]);
    ReleaseSRWLockExclusive(&targetLock);
    return true;
}

bool VRHands::Calibrate(unsigned hand)
{
    if(!enabled || hand>1 || !OpenXRVR::HasFocusedViews()) return false;
    OpenXRVR::ControllerInput input{};
    OpenXRVR::EyeView eyes[2]{}; long long time=0;
    if(!OpenXRVR::GetControllerInput(input) || !input.gripPoseValid[hand] ||
        !OpenXRVR::GetEyeViews(eyes,time)) return false;
    const float origin[3]={};
    const auto level=VRMath::LevelTrackingReference(VRMath::Pose(origin,eyes[0].orientation),false);
    // Neutral pistol wrist: longitudinal X forward, Y left, Z up. Use
    // headset yaw only: looking up/down must not bake pitch into calibration.
    auto desired=VRMath::Identity();
    for(unsigned axis=0;axis<3;++axis)
    {
        desired[0][axis]=-level[2][axis];
        desired[1][axis]=-level[0][axis];
        desired[2][axis]=level[1][axis];
    }
    const auto grip=VRMath::Pose(origin,input.gripOrientation[hand]);
    auto basis=grip;
    for(unsigned axis=0;axis<3;++axis)
    {
        basis[1][axis]=-grip[2][axis]; basis[2][axis]=grip[1][axis];
    }
    const auto correction=VRMath::Multiply(desired,VRMath::InverseRigid(basis));
    AcquireSRWLockExclusive(&targetLock);
    wristCorrection[hand]=correction; calibrated[hand]=true;
    ReleaseSRWLockExclusive(&targetLock);
    return true;
}

void VRHands::Install()
{
    CIniReader ini("");
    if(!ini.ReadInteger("VR","ExperimentalHandIK",0) ||
        !ini.ReadInteger("VR","ExperimentalControllerInput",0)) return;
    auto update=hook::pattern("55 8B EC 83 E4 F0 83 EC 14 53 56 57 FF 35 ? ? ? ? 8B F1 FF 15");
    auto solver=hook::pattern("55 8B EC 83 E4 F0 81 EC C8 00 00 00 80 7D 08 00 56 8B F1 57 8B 46 40");
    if(update.size()!=1 || solver.size()!=1) return;
    solveArm=reinterpret_cast<ArmSolver>(solver.get_first());
    updateHook=safetyhook::create_inline(update.get_first(),UpdateIK);
    enabled=static_cast<bool>(updateHook);
    if(!enabled) return;
    VRHandPhysics::Install();
    configuredHeight=ini.ReadFloat("VR","PlayerHeight",1.75f);
    if(!std::isfinite(configuredHeight) || configuredHeight<1.f || configuredHeight>2.5f) configuredHeight=1.75f;
    fitBodyHeight=ini.ReadInteger("VR","ExperimentalBodyHeight",0)!=0;
    bodyHeadAllowance=ini.ReadFloat("VR","BodyHeadAllowance",.25f);
    bodyHeightScale=ini.ReadFloat("VR","BodyHeightScale",1.f);
    trackedBody=ini.ReadInteger("VR","ExperimentalTrackedBody",0)!=0;
    bodyVerticalOffset=ini.ReadFloat("VR","BodyVerticalOffset",.10f);
    if(!std::isfinite(bodyVerticalOffset) || bodyVerticalOffset<-.15f || bodyVerticalOffset>.25f) bodyVerticalOffset=.10f;
    if(!std::isfinite(bodyHeightScale) || bodyHeightScale<.75f || bodyHeightScale>1.15f) bodyHeightScale=1.f;
    if(!std::isfinite(bodyHeadAllowance) || bodyHeadAllowance<.1f || bodyHeadAllowance>.4f) bodyHeadAllowance=.25f;
    auto finishPose=hook::pattern("56 8B F1 57 8B 7E 10 8B CF 8B 07 FF 90 A0 00 00 00 85 C0");
    if(finishPose.size()==1) finishPoseHook=safetyhook::create_inline(finishPose.get_first(),FinishPose);
    fingersEnabled=ini.ReadInteger("VR","ExperimentalFingerGrip",0)!=0;
    supportGripEnabled=ini.ReadInteger("VR","ExperimentalSupportGrip",0)!=0;
    VRReloadRuntime::Install();
    weaponSwapEnabled=ini.ReadInteger("VR","ExperimentalWeaponSwap",0)!=0;
    if(weaponSwapEnabled) FusionFix::onGameProcessEvent() += ProcessWeaponSwap;
    if(ini.ReadInteger("VR","ExperimentalPistolAim",0))
    {
        auto fire=hook::pattern("55 8B EC 83 E4 F0 81 EC 68 0F 00 00 A1 ? ? ? ? 33 C4 89 84 24 64 0F 00 00");
        auto muzzle=hook::pattern("55 8B EC 83 E4 F0 83 EC 18 56 8B 75 08 57 8B 7D 0C 89 4C 24 0C 8B 47 30");
        auto range=hook::pattern("FF 71 18 E8 ? ? ? ? D9 40 18 83 C4 04 C2 04 00");
        if(fire.size()==1 && muzzle.size()==1 && range.size()==1)
        {
            muzzleLookup=reinterpret_cast<MuzzleLookup>(muzzle.get_first());
            weaponRange=reinterpret_cast<WeaponRange>(range.get_first());
            pistolFireHook=safetyhook::create_inline(fire.get_first(),FirePistol);
            auto impact=hook::pattern("55 8B EC 83 E4 F0 83 EC 28 8B 45 10 56 57 FF 30 89 4C 24 14 E8 ? ? ? ? 8B 75 08 8B F8 83 C4 04 8B 47 6C");
            if(impact.size()==1) bulletHitHook=safetyhook::create_inline(impact.get_first(),BulletHit);
            auto aimedHeadshot=hook::pattern("F6 80 D0 03 00 00 08 75 17 8D 44 24 1C 50 8D 44 24 24 50 51 E8 ? ? ? ? 83 C4 0C 84 C0 74 0B");
            if(aimedHeadshot.size()==1) aimedHeadshotHook=safetyhook::create_mid(aimedHeadshot.get_first(7),AimedPistolHeadshot);
            // CE A6F17D native target query; A6F437 constructs and sends the
            // gun-aim event to the selected ped's intelligence event group.
            auto target=hook::pattern("6A 01 6A 00 8D 44 24 28 50 8D 44 24 3C 50 57 E8 ? ? ? ? 6A 00 89 44 24 28 6A 01 8D 44 24 3C");
            auto threat=hook::pattern("57 8D 4C 24 34 E8 ? ? ? ? 6A 01 6A 00 8D 44 24 38 50 8B 44 24 18 8B 88 24 02 00 00 81 C1 84 00 00 00 E8 ? ? ? ? 8D 4C 24 30 C7 46 38 00 00 00 00 E8");
            if(target.size()==1 && threat.size()==1)
            {
                aimTarget=reinterpret_cast<AimTarget>(injector::GetBranchDestination(target.get_first(15)).as_int());
                aimEventCtor=reinterpret_cast<AimEventCtor>(injector::GetBranchDestination(threat.get_first(5)).as_int());
                aimAddEvent=reinterpret_cast<AimAddEvent>(injector::GetBranchDestination(threat.get_first(35)).as_int());
                aimEventDtor=reinterpret_cast<AimEventDtor>(injector::GetBranchDestination(threat.get_first(51)).as_int());
                aimRaiseBelowHead=ini.ReadFloat("VR","PistolAimRaiseBelowHead",.40f);
                if(!std::isfinite(aimRaiseBelowHead)) aimRaiseBelowHead=.40f;
                aimRaiseBelowHead=std::clamp(aimRaiseBelowHead,.15f,1.f);
                FusionFix::onGameProcessEvent() += ProcessPistolThreat;
            }
            if(pistolFireHook && ini.ReadInteger("VR","ExperimentalPistolTrigger",0))
            {
                auto dispatch=hook::pattern("55 8B EC 83 E4 F0 83 EC 68 56 8B F1 57 FF 76 18 89 74 24 50 E8");
                if(dispatch.size()==1)
                {
                    weaponFire=reinterpret_cast<WeaponFire>(dispatch.get_first());
                    weaponFireHook=safetyhook::create_inline(dispatch.get_first(),FireWeapon);
                    FusionFix::onGameProcessEvent() += ProcessPistolTrigger;
                }
            }
        }
    }
    for(unsigned hand=0;hand<2;++hand)
    {
        const auto value=ini.ReadString("VR",hand==0 ? "HandGripRotationLeft" : "HandGripRotationRight","");
        float quaternion[4]{}; char trailing=0;
        if(sscanf_s(value.c_str(),"%f %f %f %f %c",&quaternion[0],&quaternion[1],&quaternion[2],&quaternion[3],&trailing,1u)!=4) continue;
        bool finite=true;
        for(float component:quaternion) finite &= std::isfinite(component);
        if(!finite) continue;
        try
        {
            const float origin[3]={};
            wristCorrection[hand]=VRMath::Pose(origin,quaternion);
            calibrated[hand]=true;
        }
        catch(const std::invalid_argument&) { /* Invalid optional calibration retains default basis. */ }
    }
}

std::string VRHands::PistolStatus()
{
    return "{\"hitObserverReady\":"+std::string(bulletHitHook ? "true":"false")+
        ",\"aimedHeadshotReady\":"+(aimedHeadshotHook ? "true":"false")+
        ",\"aimedHeadshotChecks\":"+std::to_string(aimedHeadshotChecks.load())+
        ",\"aimedHeadshotOverrides\":"+std::to_string(aimedHeadshotOverrides.load())+
        ",\"aimThreatReady\":"+(aimTarget && aimEventCtor && aimEventDtor && aimAddEvent ? "true":"false")+
        ",\"aimRaised\":"+(aimRaised ? "true":"false")+",\"aimHasTarget\":"+(aimHasTarget ? "true":"false")+
        ",\"aimQueries\":"+std::to_string(aimQueries.load())+",\"aimEvents\":"+std::to_string(aimEvents.load())+
        ",\"aimVehicleEvents\":"+std::to_string(aimVehicleEvents.load())+
        ",\"magazineGrip\":"+(VRReloadRuntime::HoldsMagazine() ? "true":"false")+
        ",\"hits\":"+std::to_string(bulletHits.load())+",\"part\":"+std::to_string(bulletPart.load())+
        ",\"shape\":"+std::to_string(bulletShape.load())+",\"baseDamage\":"+std::to_string(bulletDamage.load())+
        ",\"appliedDamage\":"+std::to_string(bulletApplied.load())+",\"healthBefore\":"+std::to_string(bulletHealthBefore.load())+
        ",\"healthAfter\":"+std::to_string(bulletHealthAfter.load())+"}";
}
