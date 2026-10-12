module;
#include <common.hxx>
#include "includes/vrmath.hpp"
#include "includes/vrphoneinput.hpp"
#include <atomic>
#include <cstring>
#include <cstdio>
#include <cwchar>
#include <intrin.h>
export module vrcamera;
import common;
import vrdiagnostics;
import comvars;
import openxrvr;
import natives;
import vrhands;

export namespace VRCamera
{
    bool GenerateOpticalView(const rage::grcViewport& base,const VRMath::Matrix& camera,float verticalDegrees,rage::grcViewport& output);
    void Install();
    void Recenter(); // Atomic request, consumed by the world list builder.
    bool RestartAnchorProbe();
    std::string MovementStatus();
    std::string ControllerMovementStatus();
    std::string AimStatus();
    std::string AudioStatus();
    void SetVisibilityAvailable(bool available);
    bool HideLocalCharacter(const void* entity);
    bool ShowLocalBody(const void* entity);
    void SetHeadlessBodyAvailable(bool available);
    bool IsFirstPersonActive();
    bool GetVehicleHudAnchor(VRMath::Matrix& anchor); // World-list builder only; snapshot with eye commands.
    bool Generate(const rage::grcViewport& base, const OpenXRVR::EyeView (&eyes)[2],
        rage::grcViewport (&output)[2]);
}

namespace
{
    using Setter=void (__thiscall*)(rage::grcViewport*,const void*);
    Setter setCamera=nullptr, setProjection=nullptr;
    SafetyHookInline audioUpdate{};
    SafetyHookMid audioListeners[2]{};
    SRWLOCK audioLock=SRWLOCK_INIT;
    VRMath::Matrix audioHead=VRMath::Identity(),audioNative=VRMath::Identity();
    uint64_t audioHeadAt=0;
    std::atomic<unsigned> audioUpdates{0},audioOverrides[2]{};
    thread_local bool vrAudioUpdate=false;
    alignas(16) thread_local VRMath::Matrix audioFrame=VRMath::Identity();

    void __fastcall UpdateAudioListeners(void* self,void* edx)
    {
        const bool previous=vrAudioUpdate;
        AcquireSRWLockShared(&audioLock);
        const auto now=GetTickCount64();
        vrAudioUpdate=audioHeadAt && now-audioHeadAt<=250 && OpenXRVR::HasFocusedViews();
        if(vrAudioUpdate) audioFrame=audioHead;
        ReleaseSRWLockShared(&audioLock);
        VRDiagnostics::Increment(audioUpdates);
        audioUpdate.unsafe_fastcall<void>(self,edx);
        vrAudioUpdate=previous;
    }

    void AudioListenerInput(SafetyHookContext& regs,unsigned route)
    {
        if(!vrAudioUpdate) return;
        // CE thiscall(matrix*, listenerIndex, gain), RET 0xC. Only replace
        // inputs while the native GTA listener update owns this thread.
        // Preserve its double buffering, weights and world-to-listener math.
        auto** matrix=reinterpret_cast<const VRMath::Matrix**>(regs.esp+4);
        if(VRDiagnostics::counters && route==0 && *reinterpret_cast<int*>(regs.esp+8)==0)
        {
            AcquireSRWLockExclusive(&audioLock);
            audioNative=VRMath::AffineXYZ(**matrix);
            ReleaseSRWLockExclusive(&audioLock);
        }
        *matrix=&audioFrame;
        VRDiagnostics::Increment(audioOverrides[route]);
    }
    void AudioListenerSpatial(SafetyHookContext& regs) {AudioListenerInput(regs,0);}
    void AudioListenerDistance(SafetyHookContext& regs) {AudioListenerInput(regs,1);}
    std::atomic<bool> recenter{true};
    VRMath::Matrix referenceInverse{};
    bool probeAnchor=false;
    bool firstPerson=false;
    bool controllerButtons=false;
    SafetyHookInline controllerPadUpdate{};
    using PadGetter=void* (__cdecl*)();
    PadGetter localPad=nullptr;
    bool previousJump=false,previousEnter=false,previousHandbrake=false,previousRun=false,previousHorn=false,previousHangGrip[2]{true,true};
    bool buttonsArmed=false;
    std::atomic<unsigned> padButtonUpdates{0},padJumpPresses{0},padEnterPresses{0},padDrivingUpdates{0},padHangDrops{0};
    std::atomic<unsigned> phoneDelivered[6]{};
    SafetyHookInline phoneRawPressed{},phoneRawJustPressed{};
    std::atomic<unsigned> phoneRawDown{0},phoneRawRising{0},phoneRawQueries{0},phoneRawMatches{0};
    std::atomic<uint64_t> phoneRawSampledAt{0};
    std::atomic<bool> scriptCameraActive{false};
    SafetyHookInline missionCoverInput{},missionCoverTask{};
    using ScriptCameraAccessor=void* (__thiscall*)(void*);
    void* scriptCameraManager=nullptr;
    ScriptCameraAccessor scriptCameraRoot=nullptr,scriptCameraDraw=nullptr;
    std::atomic<unsigned> scriptRawDown{0},scriptRawRising{0};
    std::atomic<float> padSteering{0},padThrottle{0},padBrake{0};
    std::atomic<bool> firstPersonActive{false};
    bool visibilityAvailable=false;
    bool showBody=true;
    std::atomic<bool> headlessBodyAvailable{false};
    VRMath::Matrix trackingBasis{};
    float headHeight=0;
    Vehicle anchorVehicle=0;
    std::atomic<bool> vehicleView{false};
    std::atomic<bool> crouchedView{false},airborneView{false};
    std::atomic<bool> animatedView{false},ragdollView{false};
    std::atomic<bool> vehicleExitView{false};
    SRWLOCK animationLock=SRWLOCK_INIT;
    void* animationOwner=nullptr;
    Ped animationPed=0;
    bool animationDriven=false,animationRagdoll=false,animationVehicleExit=false;
    uint64_t animationSampledAt=0;
    bool physicalCrouchEnabled=false;
    std::atomic<bool> physicalCrouchOwnsDuck{false};
    bool physicalDuckAcknowledged=false;
    Ped physicalCrouchOwner=0;
    VRMath::PhysicalCrouch physicalCrouch{};
    Vehicle seatVehicle=0;
    Ped seatPed=0;
    bool seatAnchorValid=false;
    bool seatIsCar=false,vehicleHudValid=false;
    VRMath::Matrix vehicleHudAnchor{};
    VRMath::Vector seatOffset{};
    float vehicleEyeHeightOffset=.075f;
    unsigned anchorFrames=0, anchorSamples=0;
    unsigned probeSamples=8;
    using ColliderLookup=void* (__thiscall*)(void*);
    ColliderLookup colliderLookup=nullptr;
    SafetyHookInline colliderVelocity{};
    SafetyHookInline moveBlendOutput{};
    std::atomic<void*> localPed{nullptr};
    std::atomic<bool> motionProbeComplete{false};
    std::atomic<bool> restartAnchorProbe{false};
    std::atomic<void*> localCollider{nullptr};
    SRWLOCK motionLock=SRWLOCK_INIT;
    unsigned velocityCalls=0;
    float lastVelocity[3]{};
    unsigned moveBlendCalls=0;
    uintptr_t moveBlendCaller=0;
    float lastMoveBlend[3]{}, lastMoveTarget[3]{}, lastMoveDt=0;
    bool headRelativeMovement=false;
    SafetyHookInline movementHeading{};
    SafetyHookInline controllerMoveAxis{};
    SafetyHookInline controllerMoveFloatAxis{};
    SafetyHookInline ordinaryWalk{};
    SafetyHookInline movementSpeed{},runControl{};
    thread_local int padGait=-1;
    thread_local unsigned char padRunControl[16]{};
    std::atomic<unsigned> padJogReads{0},padSprintReads{0};
    using DirectionalWalk=void (__thiscall*)(void*,void*);
    DirectionalWalk directionalWalk=nullptr;
    std::atomic<unsigned> directionalWalkCalls{0};
    bool controllerLocomotion=false;
    std::atomic<unsigned> controllerMoveReads{0};
    std::atomic<unsigned> controllerAxisCalls{0},controllerMotorReads{0};
    std::atomic<uintptr_t> controllerAxisCallers[8]{};
    SRWLOCK headingLock=SRWLOCK_INIT;
    bool headingValid=false;
    void* headingOwner=nullptr;
    float headHeading=0,lastOriginalHeading=0,lastAppliedHeading=0;
    uint64_t headingSampledAt=0;
    unsigned headingQueries=0,headingOverrides=0;
    bool headsetAim=false;
    SafetyHookInline weaponCameraUpdate{};
    SafetyHookMid weaponBasis[2]{};
    SRWLOCK aimLock=SRWLOCK_INIT;
    bool aimValid=false;
    void* aimOwner=nullptr;
    VRMath::Matrix aimCamera{};
    float aimPitch=0,aimYaw=0;
    uint64_t aimSampledAt=0;
    unsigned aimUpdates=0,aimOverrides=0;
    std::atomic<bool> lockonSuppressed{false};
    // Only accessed by the simulation callback, never by the render hooks.
    void* lockonOwner=nullptr;
    bool savedLockon=false;
    bool roomScale=false;
    SRWLOCK roomLock=SRWLOCK_INIT;
    VRMath::RoomMotion roomMotion{};
    VRMath::Vector lastRoomPosition{};
    VRMath::Vector processedRoomPosition{};
    bool roomSampleValid=false;
    void* roomOwner=nullptr;
    uint64_t roomSampledAt=0;
    unsigned roomMotorCalls=0;
    VRMath::Vector roomAchieved{}, lastBodyPosition{}, normalTarget{};
    VRMath::Vector lastRoomVelocity{},diagnosticBody{},diagnosticView{};
    bool bodySampleValid=false;
    uint64_t roomDrivingAt=0;
    float gameGroundZ=NAN,physicalEyeHeight=NAN;
    float bodyRootAboveFloor=.9f;
    using VolumeQuery=bool (__cdecl*)(const float*,const float*,float,void*,void*,uint32_t,uint32_t,int,int);
    VolumeQuery volumeQuery=nullptr;
    using PositionSetter=void (__thiscall*)(void*,const float*,int,int);
    using PhysicsSync=void (__thiscall*)(void*,int);
    PositionSetter positionSetter=nullptr;
    PhysicsSync physicsSync=nullptr;
    unsigned collisionProbeSamples=0;
    uint64_t collisionProbeAt=0;

    // Bounded diagnostic on the existing pedestrian motor callback thread.
    // These native scene queries do not move the entity. Their two query modes
    // and result fields must be measured before using them to resolve movement.
    void ProbeRoomCollision(void* owner)
    {
        if(!probeAnchor || !volumeQuery || collisionProbeSamples>=8 ||
            !owner || owner!=localPed.load() || GetTickCount64()-collisionProbeAt<1000) return;
        auto* matrix=*reinterpret_cast<float**>(static_cast<char*>(owner)+0x20);
        if(!matrix) return;
        alignas(16) float start[4]{matrix[12],matrix[13],matrix[14]+.085f,0};
        if(!std::isfinite(start[0]+start[1]+start[2])) return;
        wchar_t path[MAX_PATH]{};
        if(!GetModuleFileNameW(nullptr,path,MAX_PATH)) return;
        auto* name=wcsrchr(path,L'\\');
        if(!name) return;
        name[1]=0;
        if(wcscat_s(path,L"FusionFix.vr-collision-probe.log")) return;
        FILE* file=nullptr;
        if(_wfopen_s(&file,path,collisionProbeSamples ? L"a" : L"w")) return;
        collisionProbeAt=GetTickCount64();
        const unsigned direction=collisionProbeSamples%4;
        alignas(16) float end[4]{start[0],start[1],start[2],0};
        end[direction/2]+=(direction%2 ? -2.f : 2.f);
        for(int mode=0;mode<2;++mode)
        {
            // Native callers initialize 0x60-byte results. Guard the following
            // bytes to detect a wrong capacity assumption in this diagnostic.
            alignas(16) unsigned char result[0x80]{};
            const uint32_t invalidPair=0x0000ffff;
            std::memcpy(result+0x54,&invalidPair,sizeof(invalidPair));
            std::memset(result+0x60,0xA5,0x20);
            const bool hit=volumeQuery(start,end,.25f,owner,result,0x8E,0xffffffff,mode,0);
            bool guard=true;
            for(unsigned i=0x60;i<sizeof(result);++i) guard=guard && result[i]==0xA5;
            fprintf(file,"sample=%u mode=%d start=(%.6f %.6f %.6f) end=(%.6f %.6f %.6f) radius=.25 hit=%u guard=%u raw=",
                collisionProbeSamples,mode,start[0],start[1],start[2],end[0],end[1],end[2],hit,guard);
            for(unsigned i=0;i<0x60;++i) fprintf(file,"%02X",result[i]);
            fprintf(file,"\n");
            if(!guard) {volumeQuery=nullptr; break;}
        }
        fclose(file);
        ++collisionProbeSamples;
    }

    void StopRoomMotion()
    {
        AcquireSRWLockExclusive(&roomLock);
        roomOwner=nullptr;
        roomSampleValid=false;
        bodySampleValid=false;
        roomDrivingAt=0;
        roomMotion.ClearPending();
        ReleaseSRWLockExclusive(&roomLock);
    }

    bool MoveRoomBody(void* owner,const VRMath::Vector& delta,VRMath::Vector& body)
    {
        if(!positionSetter || !physicsSync || !volumeQuery || !owner ||
            !*reinterpret_cast<void**>(static_cast<char*>(owner)+0x38)) return false;
        auto* matrix=*reinterpret_cast<float**>(static_cast<char*>(owner)+0x20);
        if(!matrix || reinterpret_cast<PositionSetter>((*reinterpret_cast<uintptr_t**>(owner))[2])!=positionSetter)
            return false;
        alignas(16) float position[4]{matrix[12],matrix[13],matrix[14],matrix[15]};
        const float distance=std::hypot(delta[0],delta[1]);
        if(!std::isfinite(position[0]+position[1]+position[2]+distance) || distance>.3f) return false;
        float fraction=1.f;
        if(distance>1.e-5f)
        {
            // Conservative sphere cover of the measured standing torso capsule:
            // axis z=[-.4,.57], radius .25. Inflation covers the space between
            // samples. Native floor/support physics still owns vertical travel;
            // this is not a complete composite/crouched-body sweep.
            for(unsigned sample=0;sample<=10 && fraction>0;++sample)
            {
                alignas(16) float start[4]{position[0],position[1],position[2]-.4f+.097f*sample,0};
                alignas(16) float end[4]{start[0]+delta[0],start[1]+delta[1],start[2],0};
                alignas(16) unsigned char hit[0x60]{};
                const uint32_t invalidPair=0xffff;
                std::memcpy(hit+0x54,&invalidPair,sizeof(invalidPair));
                if(volumeQuery(start,end,.256f,owner,hit,0x8E,0xffffffff,1,0))
                {
                    float contactFraction=0;
                    std::memcpy(&contactFraction,hit+0x40,sizeof(contactFraction));
                    fraction=std::min(fraction,VRMath::SafeSweepFraction(contactFraction,distance));
                }
            }
            position[0]+=delta[0]*fraction;
            position[1]+=delta[1]*fraction;
            // Skip the ped's full relocation/reset path. Synchronize the
            // physics matrices/broadphase directly, including previous collider
            // transform, without clearing ped contact/motor/IK history.
            positionSetter(owner,position,1,0);
            physicsSync(owner,1);
            VRDiagnostics::Increment(roomMotorCalls);
        }
        for(unsigned axis=0;axis<3;++axis) body[axis]=matrix[12+axis];
        body[3]=1;
        return true;
    }

    bool LadderControlsBody(void* owner)
    {
        if(!owner || !*reinterpret_cast<void**>(static_cast<char*>(owner)+0x224)) return false;
        for(auto task:{TaskID::TaskComplexClimbLadder,TaskID::TaskSimpleClimbLadder,
            TaskID::TaskComplexClimbLadderFully})
            if(CPed::ComparePedTasks(reinterpret_cast<uintptr_t>(owner),task)) return true;
        return false;
    }

    bool AnimationControlsBody(Ped ped,void* owner,bool& ragdoll,bool* exiting=nullptr)
    {
        if(exiting) *exiting=false;
        ragdoll=NativeInvoke::GetNativeHandler(std::to_underlying(Natives::NativeHashes::IS_PED_RAGDOLL)) &&
            Natives::IsPedRagdoll(ped);
        if(ragdoll) return true;
        if(!owner || !*reinterpret_cast<void**>(static_cast<char*>(owner)+0x224)) return false;
        if(LadderControlsBody(owner)) return true;
        const auto address=reinterpret_cast<uintptr_t>(owner);
        for(auto task:{TaskID::TaskComplexNewExitVehicle,TaskID::TaskSimpleCarGetOut,
            TaskID::TaskSimpleCarJumpOut,TaskID::TaskSimpleCarSlowBeDraggedOut})
            if(CPed::ComparePedTasks(address,task))
            {if(exiting) *exiting=true; return true;}
        // ComparePedTasks combines its arguments with AND; test alternatives
        // separately. These task IDs already drive FusionFix's climb/jump code.
        for(auto task:{TaskID::TaskSimpleClimb,TaskID::TaskSimpleClimbLadder,
            TaskID::TaskSimpleGetUp,TaskID::TaskSimpleBlendFromNM})
            if(CPed::ComparePedTasks(address,task)) return true;
        return false;
    }

    void SampleAnimationState()
    {
        Ped ped=0; void* owner=nullptr; bool animated=false,ragdoll=false,exiting=false;
        using Hash=Natives::NativeHashes;
        if(NativeInvoke::GetNativeHandler(std::to_underlying(Hash::GET_PLAYER_ID)) &&
            NativeInvoke::GetNativeHandler(std::to_underlying(Hash::GET_PLAYER_CHAR)) &&
            NativeInvoke::GetNativeHandler(std::to_underlying(Hash::DOES_CHAR_EXIST)))
        {
            Natives::GetPlayerChar(Natives::GetPlayerId(),&ped);
            if(ped && Natives::DoesCharExist(ped))
            {
                owner=FindPlayerPed ? FindPlayerPed(0) : nullptr;
                animated=AnimationControlsBody(ped,owner,ragdoll,&exiting);
            }
        }
        // Preserve native idle/head-IK flags while tracked arms own the rig.
        // Never follow a cached pointer after the player entity is replaced.
        static void* idleOwner=nullptr;
        static uint32_t idleFlags=0,headFlags=0;
        constexpr uint32_t idleMask=0x28000,headMask=0x300000;
        const bool tracked=owner && firstPersonActive && !animated && !ragdoll && !vehicleView &&
            OpenXRVR::HasFocusedViews() && VRHands::Active(owner);
        if(idleOwner && idleOwner==owner && !tracked)
        {
            auto& flags=*reinterpret_cast<uint32_t*>(static_cast<char*>(owner)+0x2A0);
            auto& head=*reinterpret_cast<uint32_t*>(static_cast<char*>(owner)+0x29C);
            flags=(flags&~idleMask)|idleFlags;
            head=(head&~headMask)|headFlags;
            idleOwner=nullptr;
        }
        if(idleOwner!=owner) idleOwner=nullptr;
        if(tracked)
        {
            auto& flags=*reinterpret_cast<uint32_t*>(static_cast<char*>(owner)+0x2A0);
            auto& head=*reinterpret_cast<uint32_t*>(static_cast<char*>(owner)+0x29C);
            if(!idleOwner) {idleOwner=owner; idleFlags=flags&idleMask; headFlags=head&headMask;}
            flags|=idleMask;
            head=(head&~headMask)|0x100000;
        }
        AcquireSRWLockExclusive(&animationLock);
        animationOwner=owner; animationPed=ped;
        animationDriven=animated; animationRagdoll=ragdoll;
        animationVehicleExit=exiting;
        animationSampledAt=GetTickCount64();
        ReleaseSRWLockExclusive(&animationLock);
    }

    void ProcessRoomBody()
    {
        // Simulation owns entity/physics writes. Camera preparation only
        // publishes tracking and renders the unconsumed immediate head delta.
        if(!roomScale || !OpenXRVR::HasFocusedViews() ||
            (CMenuManager::m_MenuActive && *CMenuManager::m_MenuActive)) return;
        // A render sample can still describe on-foot play on the simulation
        // tick that enters a vehicle, dies or starts a cutscene. Recheck the
        // current native state before moving the ped's physics transform.
        using Hash=Natives::NativeHashes;
        for(auto hash:{Hash::GET_PLAYER_ID,Hash::GET_PLAYER_CHAR,Hash::DOES_CHAR_EXIST,
            Hash::IS_CHAR_IN_ANY_CAR,Hash::IS_CHAR_DEAD,Hash::HAS_CUTSCENE_LOADED,
            Hash::HAS_CUTSCENE_FINISHED})
            if(!NativeInvoke::GetNativeHandler(std::to_underlying(hash))) return;
        Ped ped=0;
        Natives::GetPlayerChar(Natives::GetPlayerId(),&ped);
        if(!ped || !Natives::DoesCharExist(ped) || Natives::IsCharInAnyCar(ped) ||
            Natives::IsCharDead(ped) ||
            (Natives::HasCutsceneLoaded() && !Natives::HasCutsceneFinished())) return;
        void* owner=FindPlayerPed ? FindPlayerPed(0) : nullptr;
        bool ragdoll=false;
        if(AnimationControlsBody(ped,owner,ragdoll)) {StopRoomMotion(); return;}
        AcquireSRWLockExclusive(&roomLock);
        if(owner && owner==roomOwner && roomSampleValid && GetTickCount64()-roomSampledAt<=100)
        {
            VRMath::Vector delta{},body{};
            for(unsigned axis=0;axis<2;++axis) delta[axis]=lastRoomPosition[axis]-processedRoomPosition[axis];
            auto* matrix=*reinterpret_cast<float**>(static_cast<char*>(owner)+0x20);
            VRMath::Vector before{};
            if(matrix) for(unsigned axis=0;axis<3;++axis) before[axis]=matrix[12+axis];
            if(MoveRoomBody(owner,delta,body))
            {
                for(unsigned axis=0;axis<2;++axis)
                {
                    roomMotion.consumed[axis]+=delta[axis];
                    roomAchieved[axis]+=body[axis]-before[axis];
                }
                VRHands::ConsumeRoomMotion(owner,delta);
            }
            processedRoomPosition=lastRoomPosition;
        }
        ReleaseSRWLockExclusive(&roomLock);
    }

    void InvalidateHeading()
    {
        AcquireSRWLockExclusive(&headingLock);
        headingValid=false;
        headingOwner=nullptr;
        ReleaseSRWLockExclusive(&headingLock);
        AcquireSRWLockExclusive(&aimLock);
        aimValid=false;
        aimOwner=nullptr;
        ReleaseSRWLockExclusive(&aimLock);
    }

    bool ReadAim(void* ped,VRMath::Matrix& camera,float& pitch,float& yaw)
    {
        const bool focused=OpenXRVR::HasFocusedViews();
        AcquireSRWLockShared(&aimLock);
        const bool valid=headsetAim && focused && firstPersonActive && !vehicleView && !animatedView &&
            !(CMenuManager::m_MenuActive && *CMenuManager::m_MenuActive) && aimValid && ped && ped==aimOwner &&
            GetTickCount64()-aimSampledAt<=250;
        if(valid) {camera=aimCamera; pitch=aimPitch; yaw=aimYaw;}
        ReleaseSRWLockShared(&aimLock);
        return valid;
    }

    void SuppressAimLockon()
    {
        void* ped=FindPlayerPed ? FindPlayerPed(0) : nullptr;
        VRMath::Matrix camera{};
        float pitch=0,yaw=0;
        const bool apply=ReadAim(ped,camera,pitch,yaw);
        // DISABLE_PLAYER_LOCKON's verified native storage. Preserve its prior
        // bit rather than blindly enabling lock-on on VR fallback. Never follow
        // an old ped pointer after the player owner changes.
        if(lockonOwner && lockonOwner!=ped) {lockonOwner=nullptr; lockonSuppressed=false;}
        if(!ped) return;
        auto& flags=*reinterpret_cast<uint32_t*>(static_cast<char*>(ped)+0x264);
        if(apply)
        {
            if(!lockonOwner) {savedLockon=(flags&0x800000)!=0; lockonOwner=ped;}
            flags|=0x800000;
            lockonSuppressed=true;
        }
        else if(lockonOwner==ped)
        {
            flags=(flags&~0x800000u)|(savedLockon ? 0x800000u : 0u);
            lockonOwner=nullptr;
            lockonSuppressed=false;
        }
    }

    void HeadsetWeaponBasis(SafetyHookContext& regs)
    {
        auto* cam=reinterpret_cast<char*>(regs.edi);
        VRMath::Matrix camera{};
        float pitch=0,yaw=0;
        if(!ReadAim(*reinterpret_cast<void**>(cam+0x204),camera,pitch,yaw)) return;
        // Immediately before native thiscall matrix.SetPitchYaw(pitch,yaw).
        // Both aim branches have allocated its two stack arguments here.
        regs.xmm0.f32[0]=pitch;
        *reinterpret_cast<float*>(regs.esp+4)=yaw;
        *reinterpret_cast<float*>(cam+0x218)=pitch;
        *reinterpret_cast<float*>(cam+0x21C)=yaw;
        AcquireSRWLockExclusive(&aimLock);
        ++aimOverrides;
        ReleaseSRWLockExclusive(&aimLock);
    }

    char __fastcall UpdateWeaponCamera(void* self,void* edx)
    {
        const char result=weaponCameraUpdate.unsafe_fastcall<char>(self,edx);
        VRMath::Matrix camera{};
        float pitch=0,yaw=0;
        auto* cam=static_cast<char*>(self);
        if(result && ReadAim(*reinterpret_cast<void**>(cam+0x204),camera,pitch,yaw))
        {
            // Native camera matrix rows are right/forward/up, unlike the
            // viewport's right/up/back. Publish the eye midpoint for gameplay
            // ray queries after update, rather than the chase-camera origin.
            const VRMath::Vector rows[4]{camera[0],
                {-camera[2][0],-camera[2][1],-camera[2][2],0},camera[1],camera[3]};
            for(unsigned row=0;row<4;++row)
                std::memcpy(cam+0x10+row*16,rows[row].data(),3*sizeof(float));
            AcquireSRWLockExclusive(&aimLock);
            ++aimUpdates;
            ReleaseSRWLockExclusive(&aimLock);
        }
        return result;
    }

    // CE camera helper: thiscall(camera*, ped*, bool), ret 8, x87 return.
    // It supplies a reference angle; it does not own axis magnitude or motion.
    float __fastcall WalkingHeading(void* self,void* edx,void* ped,int reverse)
    {
        const float original=movementHeading.unsafe_fastcall<float>(self,edx,ped,reverse);
        const bool focused=OpenXRVR::HasFocusedViews();
        AcquireSRWLockExclusive(&headingLock);
        // This helper is called during loading, before player-info lookup is
        // necessarily safe. Compare against the owner published with a valid
        // on-foot camera, rather than looking up the player in this callback.
        if(!headingOwner || ped!=headingOwner)
        {
            ReleaseSRWLockExclusive(&headingLock);
            return original;
        }
        VRDiagnostics::Increment(headingQueries);
        lastOriginalHeading=original;
        const bool apply=focused && headingValid && GetTickCount64()-headingSampledAt<=250;
        const float result=apply ? headHeading : original;
        if(apply) VRDiagnostics::Increment(headingOverrides);
        lastAppliedHeading=result;
        ReleaseSRWLockExclusive(&headingLock);
        return result;
    }

    void __fastcall WalkFacingHead(void* task,void* edx,void* owner)
    {
        bool apply=firstPersonActive && !vehicleView && !animatedView && !ragdollView &&
            !airborneView && OpenXRVR::HasFocusedViews() &&
            !(CMenuManager::m_MenuActive && *CMenuManager::m_MenuActive);
        AcquireSRWLockShared(&headingLock);
        apply=apply && headingValid && owner==headingOwner && GetTickCount64()-headingSampledAt<=250;
        const float heading=headHeading;
        ReleaseSRWLockShared(&headingLock);
        AcquireSRWLockShared(&animationLock);
        const Ped ped=animationPed;
        apply=apply && ped && owner==animationOwner && !animationDriven && !animationRagdoll &&
            GetTickCount64()-animationSampledAt<=250;
        ReleaseSRWLockShared(&animationLock);
        using Hash=Natives::NativeHashes;
        if(apply && directionalWalk &&
            NativeInvoke::GetNativeHandler(std::to_underlying(Hash::SET_CHAR_HEADING)) &&
            NativeInvoke::GetNativeHandler(std::to_underlying(Hash::SET_CHAR_DESIRED_HEADING)))
        {
            // This native locomotion callback is on the simulation thread.
            // Use native entity rotation, not a render-time matrix overwrite.
            float bodyHeading=heading;
            if(VRHands::Active(owner))
            {
                static void* facingOwner=nullptr;
                static float facing=0;
                if(facingOwner!=owner)
                {
                    float degrees=0; Natives::GetCharHeading(ped,&degrees);
                    facing=degrees*3.14159265358979323846f/180.f; facingOwner=owner;
                }
                bool moving=false;
                if(auto* pad=localPad ? localPad():nullptr)
                    // The directional walking motor selects these signed
                    // axes (8F00C0/8F00F0). Auxiliary 90/91 may retain stale
                    // values when untouched and must not assert traversal.
                    for(unsigned id:{12u,13u,14u,15u})
                    {
                        auto* value=static_cast<unsigned char*>(pad)+0x2698+16*id;
                        moving |= std::abs(static_cast<int>(value[6]^value[4])-128)>34;
                    }
                if(moving) facing=heading; // Traversal owns logical forward.
                else VRHands::StationaryBodyHeading(owner,facing,facing);
                VRHands::SetTraversalState(owner,moving,!crouchedView);
                bodyHeading=facing;
            }
            const float degrees=bodyHeading*180.f/3.14159265358979323846f;
            Natives::SetCharHeading(ped,degrees);
            Natives::SetCharDesiredHeading(ped,degrees);
            VRDiagnostics::Increment(directionalWalkCalls);
            // Same task layout/thiscall ABI as ordinary walk; preserves native
            // collision and selects signed forward/lateral animation blends.
            directionalWalk(task,owner);
            return;
        }
        ordinaryWalk.unsafe_fastcall<void>(task,edx,owner);
    }

    unsigned PhoneRawButton(unsigned pad,unsigned button,bool rising)
    {
        // Script IS_BUTTON_* uses the raw gamepad table, independently of the
        // semantic CPad controls used in keyboard mode. Keep native input and
        // device ownership intact; supplement only local phone button queries.
        constexpr unsigned ids[]{8,9,10,11,16,17}; // D-pad, A, B
        const bool phone=CPhoneMgr::bDisplayMobile && *CPhoneMgr::bDisplayMobile;
        const bool scripted=scriptCameraActive.load() && !phone;
        if(pad>1 || (!phone && !scripted) ||
            (CMenuManager::m_MenuActive && *CMenuManager::m_MenuActive) ||
            !OpenXRVR::HasFocusedViews() || GetTickCount64()-phoneRawSampledAt.load()>250) return 0;
        for(unsigned i=0;i<6;++i) if(button==ids[i])
        {
            VRDiagnostics::Increment(phoneRawQueries);
            const auto mask=scripted ? (rising ? scriptRawRising.load():scriptRawDown.load()) :
                (rising ? phoneRawRising.load():phoneRawDown.load());
            const bool pressed=(mask & (1u<<i))!=0;
            if(pressed) VRDiagnostics::Increment(phoneRawMatches);
            return pressed ? 1:0;
        }
        return 0;
    }
    int __cdecl PhoneRawPressed(unsigned pad,unsigned button)
    {
        const int native=phoneRawPressed.unsafe_ccall<int>(pad,button);
        return native ? native:static_cast<int>(PhoneRawButton(pad,button,false));
    }
    bool __cdecl PhoneRawJustPressed(unsigned pad,unsigned button)
    {
        const bool native=phoneRawJustPressed.unsafe_ccall<bool>(pad,button);
        return PhoneRawButton(pad,button,true)!=0 || native;
    }

    bool IsConcreteJungleVRCall()
    {
        const auto* thread=rage::scrThread::s_CurrentThread ? *rage::scrThread::s_CurrentThread:nullptr;
        return missionCoverInput && missionCoverTask && thread &&
            std::strncmp(thread->m_szProgramName,"jacob1",sizeof(thread->m_szProgramName))==0 &&
            OpenXRVR::IsSessionRunning();
    }

    void __cdecl ConcreteJungleCoverInput(scrNativeCallContext* context)
    {
        // jacob1 has one pad-2/control-28 query: its wall tutorial gate.
        // Change that script query directly; do not synthesize cover input
        // that would also reach the player's native movement task.
        if(context && context->GetArgumentCount()==2 && context->GetArgument<int>(0)==2 &&
            context->GetArgument<int>(1)==28 && IsConcreteJungleVRCall())
        {context->SetResult<uint32_t>(0,1);return;}
        missionCoverInput.unsafe_ccall<void>(context);
    }

    void __cdecl ConcreteJungleCoverTask(scrNativeCallContext* context)
    {
        if(context && context->GetArgumentCount()==6 && IsConcreteJungleVRCall())
        {
            Ped playerPed=0;
            Natives::GetPlayerChar(Natives::GetPlayerId(),&playerPed);
            // Keep Jacob's cover and sequence placeholder (ped 0) calls.
            // The player's omitted task reports finished through the game's
            // own task-status query; the mission already handles that case.
            if(playerPed && context->GetArgument<Ped>(0)==playerPed) return;
        }
        missionCoverTask.unsafe_ccall<void>(context);
    }

    void __fastcall UpdateControllerPad(void* pad,void* edx)
    {
        // The classifier changes movement value XOR keys according to native
        // device ownership. Merge after it, using the final keys for this tick.
        controllerPadUpdate.unsafe_fastcall<void>(pad,edx);
        if(!localPad || pad!=localPad()) return;
        OpenXRVR::ControllerInput input{};
        // Shared default: a short left Menu press pauses on release; a hold
        // opens the native phone once. Separate semantic bindings still work.
        static bool previousPause=false,leftHeld=false,holdConsumed=false,pauseHeld=false;
        static uint64_t heldAt=0,phonePulseUntil=0,cutsceneSkipUntil=0;
        static bool previousCutsceneSkip=false;
        static Ped carPhonePed=0;
        static uint64_t carPhoneStarted=0;
        static bool carPhoneSeen=false;
        static bool previousPhone[12]{};
        const auto now=GetTickCount64();
        const bool inputFresh=OpenXRVR::HasFocusedViews() && OpenXRVR::GetControllerInput(input);
        const bool inMenu=CMenuManager::m_MenuActive && *CMenuManager::m_MenuActive;
        using Hash=Natives::NativeHashes;
        const bool inCutscene=!inMenu &&
            NativeInvoke::GetNativeHandler(std::to_underlying(Hash::HAS_CUTSCENE_LOADED)) &&
            NativeInvoke::GetNativeHandler(std::to_underlying(Hash::HAS_CUTSCENE_FINISHED)) &&
            Natives::HasCutsceneLoaded() && !Natives::HasCutsceneFinished();
        // Native service/menu cameras retain the ordinary third-person rig.
        // GET_SCRIPT_DRAW_CAM assumes a nonnull script root and crashes in
        // ordinary gameplay/startup. Use its verified internal accessors with
        // a null check between them, before any camera-handle conversion.
        bool scripted=false;
        if(!inCutscene && scriptCameraManager && scriptCameraRoot && scriptCameraDraw)
        {
            auto* root=scriptCameraRoot(scriptCameraManager);
            // The draw accessor selects a child whose entire parent chain has
            // native active/propagation bits set (A7C6D0), or returns null.
            scripted=root && scriptCameraDraw(root)!=nullptr;
        }
        // A script draw camera can outlive a tutorial after native movement
        // is restored (gunlockup), or accompany an execution choice. Camera
        // presence alone does not mean a menu owns input. Service previews
        // disable native player control; retain their camera/menu routing.
        if(scripted && FindPlayerPed && FindPlayerPed(0) &&
            NativeInvoke::GetNativeHandler(std::to_underlying(Hash::IS_PLAYER_CONTROL_ON)) &&
            Natives::IsPlayerControlOn(Natives::GetPlayerId())) scripted=false;
        scriptCameraActive=scripted;OpenXRVR::SetScriptMenu(scripted);
        const bool down=inputFresh && (input.pause || (!inMenu && input.phoneOpen));
        if(down && !leftHeld) {heldAt=now;holdConsumed=false;pauseHeld=false;}
        if(down && input.pause) pauseHeld=true;
        // Feed native Enter/confirm instead of forcibly stopping the scene:
        // its own skip handler owns skip eligibility and mission bookkeeping.
        if(inCutscene && down && !leftHeld && input.pause)
        {cutsceneSkipUntil=now+100;holdConsumed=true;}
        const bool directPause=!inCutscene && down && !leftHeld && input.pause && (inMenu || !input.phoneOpen);
        // A runtime long-press binding delivers PhoneOpen separately. Execute
        // immediately; only the shared suggested click needs our fallback timer.
        if(down && !inMenu && !inCutscene && input.phoneOpen && !holdConsumed &&
            (!input.pause || now-heldAt>=500))
        {holdConsumed=true;phonePulseUntil=now+100;}
        const bool pause=inputFresh && !inCutscene && (directPause || (!down && leftHeld && pauseHeld && !holdConsumed));
        if(directPause) holdConsumed=true;
        leftHeld=down;
        if(!inputFresh) {holdConsumed=false;pauseHeld=false;phonePulseUntil=0;}
        if(!inputFresh || !inCutscene) cutsceneSkipUntil=0;
        auto mergeButton=[&](unsigned id,bool pressed,bool wasPressed)
        {
            auto* value=static_cast<unsigned char*>(pad)+0x2698+16*id;
            const auto key=value[4];
            value[6]=static_cast<unsigned char>(std::max<unsigned>(value[6]^key,pressed ? 255:0)^key);
            value[7]=static_cast<unsigned char>(std::max<unsigned>(value[7]^key,wasPressed ? 255:0)^key);
            auto* history=*reinterpret_cast<unsigned char**>(value+12);
            if(history && value[8]<64) history[8*value[8]]=value[6];
        };
        mergeButton(76,pause,previousPause);previousPause=pause;
        const bool phone=inputFresh && !inMenu && input.phoneActive;
        const auto held=VRPhoneInput::Buttons(input);
        static VRPhoneInput::Pulse phonePulses[6]{};
        bool directions[6]{};
        unsigned rawDown=0,rawRising=0;
        // OpenXR updates on the render thread, native input on simulation.
        // Preserve rising edges even when press/release both fall between pad
        // updates; give scripts a held pulse and a distinct release interval.
        for(unsigned i=0;i<6;++i)
        {
            const bool wasDown=phonePulses[i].down;
            directions[i]=phonePulses[i].Update(input.phonePresses[i],held[i],phone,now);
            if(directions[i] && !wasDown) VRDiagnostics::Increment(phoneDelivered[i]);
            if(directions[i]) rawDown|=1u<<i;
            if(directions[i] && !wasDown) rawRising|=1u<<i;
        }
        phoneRawDown=rawDown;phoneRawRising=rawRising;phoneRawSampledAt=now;
        // Native gamepad phone Up shares TAKE_OUT with opening the phone;
        // scripts also use it to advance the raised phone to the keypad.
        static bool previousOpen=false;
        const bool open=(inputFresh && !inMenu && !input.phoneActive && now<phonePulseUntil) || directions[0];
        // Seated phone menus can be active without the native held-phone task.
        // Start it only for our manual open, preserving incoming-call tasks.
        if(open && !previousOpen && input.driving && !input.phoneActive)
        {
            Ped ped=0;Natives::GetPlayerChar(Natives::GetPlayerId(),&ped);
            if(ped && Natives::DoesCharExist(ped) && !Natives::IsCharDead(ped) && Natives::IsCharInAnyCar(ped))
            {
                Natives::TaskUseMobilePhone(ped,true);
                carPhonePed=ped;carPhoneStarted=now;carPhoneSeen=false;
            }
        }
        if(carPhonePed && inputFresh && !inMenu)
        {
            carPhoneSeen |= input.phoneActive;
            if(!input.phoneActive && (carPhoneSeen || now-carPhoneStarted>2000))
            {
                if(Natives::DoesCharExist(carPhonePed) && !Natives::IsCharDead(carPhonePed))
                    Natives::TaskUseMobilePhone(carPhonePed,false);
                carPhonePed=0;carPhoneSeen=false;
            }
        }
        mergeButton(21,open,previousOpen);previousOpen=open;
        // Keyboard/frontend controls complement the raw gamepad query hooks.
        constexpr unsigned phoneIds[]{65,64,66,67,77,78,130,131,132,133,181,182};
        for(unsigned i=0;i<12;++i)
        {mergeButton(phoneIds[i],directions[i%6],previousPhone[i]);previousPhone[i]=directions[i%6];}
        const bool cutsceneSkip=inCutscene && inputFresh && now<cutsceneSkipUntil;
        mergeButton(77,cutsceneSkip,previousCutsceneSkip); // INPUT_FRONTEND_ACCEPT
        previousCutsceneSkip=cutsceneSkip;
        // Script actions work outside the first-person movement gate. Preserve
        // native input, prices, task ownership and mission-side bookkeeping.
        static VRPhoneInput::Pulse contextPulse{},scriptPulses[6]{};
        static bool previousContext=false,previousScript[6]{};
        const bool scriptEnabled=inputFresh && !inMenu && !inCutscene && !input.phoneActive &&
            scripted && input.scriptMenuActive;
        const bool scriptHeld[]{input.scriptUp,input.scriptDown,input.scriptLeft,input.scriptRight,input.scriptAccept,input.scriptBack};
        bool scriptButtons[6]{};unsigned scriptDown=0,scriptRising=0;
        for(unsigned i=0;i<6;++i)
        {
            const bool wasDown=scriptPulses[i].down;
            scriptButtons[i]=scriptPulses[i].Update(input.scriptPresses[i],scriptHeld[i],scriptEnabled,now);
            if(scriptButtons[i]) scriptDown|=1u<<i;
            if(scriptButtons[i] && !wasDown) scriptRising|=1u<<i;
        }
        scriptRawDown=scriptDown;scriptRawRising=scriptRising;
        const bool context=contextPulse.Update(input.contextPresses,input.contextInteract,
            inputFresh && !inMenu && !inCutscene && !input.phoneActive,now) || scriptButtons[5];
        mergeButton(23,context,previousContext);previousContext=context;
        constexpr unsigned scriptIds[]{65,64,66,67,77,78};
        for(unsigned i=0;i<6;++i)
        {mergeButton(scriptIds[i],scriptButtons[i],previousScript[i]);previousScript[i]=scriptButtons[i];}
        const bool fresh=firstPersonActive && !ragdollView && OpenXRVR::HasFocusedViews() &&
            !scripted &&
            !(CMenuManager::m_MenuActive && *CMenuManager::m_MenuActive) &&
            !(CTimer::ms_bScriptPause && *CTimer::ms_bScriptPause) &&
            OpenXRVR::GetControllerInput(input) && input.gripPoseValid[1];
        const bool center=fresh && (vehicleView ? input.handbrake:input.jump);
        const bool menu=fresh && (vehicleView ? input.exitVehicle:input.interact);
        if(!fresh)
        {
            buttonsArmed=false; previousJump=previousEnter=previousHandbrake=previousRun=previousHorn=false;
            previousHangGrip[0]=previousHangGrip[1]=true;
            padSteering=padThrottle=padBrake=0;
            return; // The freshly updated native values already cleared VR input.
        }
        if(!buttonsArmed && !center && !menu) buttonsArmed=true;
        auto writeValue=[](unsigned char* value,unsigned char decoded)
        {
            value[6]=decoded^value[4];
            // Vehicle exit B7BDE0 tests held input through 8F0230's native
            // 64-sample history, not just current/prior bytes. Replace this
            // tick's existing sample; retain native index, time and allocation.
            auto* history=*reinterpret_cast<unsigned char**>(value+12);
            if(history && value[8]<64) history[8*value[8]]=value[6];
        };
        auto button=[&](unsigned id,bool down,bool wasDown)
        {
            auto* value=static_cast<unsigned char*>(pad)+0x2698+16*id;
            const unsigned char key=value[4];
            const unsigned char current=value[6]^key,previous=value[7]^key;
            writeValue(value,static_cast<unsigned char>(std::max<unsigned>(current,down ? 255:0)));
            value[7]=static_cast<unsigned char>(std::max<unsigned>(previous,wasDown ? 255:0)^key);
        };
        const bool driving=vehicleView;
        const bool enter=buttonsArmed && menu;
        const bool grip[2]{input.gripActive[0] && input.gripped[0],input.gripActive[1] && input.gripped[1]};
        const bool newGrip=(grip[0] && !previousHangGrip[0]) || (grip[1] && !previousHangGrip[1]);
        bool hanging=false;
        if(!driving && (newGrip || (buttonsArmed && center)) &&
            NativeInvoke::GetNativeHandler(std::to_underlying(Natives::NativeHashes::GET_PED_CLIMB_STATE)))
        {
            Ped ped=0;Natives::GetPlayerChar(Natives::GetPlayerId(),&ped);
            const unsigned climb=ped ? Natives::GetPedClimbState(ped):0;
            // CF47A0 maps hang/shimmy to2/3/4. CF57C0 asks CF4210
            // mode0, which reads INPUT_ENTER's rising edge before selecting
            // the native hang_drop animation. Keep vault/ladder input native.
            hanging=climb>=2 && climb<=4;
        }
        // Climb owns the body's animation while hanging, but still needs the
        // ordinary jump edge to pull up. Other animated states retain the gate.
        const bool jump=buttonsArmed && center && !driving && (!animatedView || hanging);
        const bool drop=newGrip && hanging;
        previousHangGrip[0]=grip[0];previousHangGrip[1]=grip[1];
        const bool handbrake=buttonsArmed && center && driving;
        button(2,jump,previousJump); // INPUT_JUMP
        button(3,(enter || drop) && !driving,previousEnter && !driving); // INPUT_ENTER / hang drop
        button(43,enter && driving,previousEnter && driving); // INPUT_VEH_EXIT
        button(44,handbrake,previousHandbrake); // INPUT_VEH_HANDBRAKE
        button(45,handbrake,previousHandbrake); // alternate handbrake
        const bool horn=buttonsArmed && driving && input.horn;
        button(54,horn,previousHorn); // native vehicle horn
        previousHorn=horn;
        if(jump && !previousJump) VRDiagnostics::Increment(padJumpPresses);
        if(enter && !previousEnter) VRDiagnostics::Increment(padEnterPresses);
        if(drop) VRDiagnostics::Increment(padHangDrops);
        previousJump=jump; previousEnter=enter; previousHandbrake=handbrake;
        VRDiagnostics::Increment(padButtonUpdates);
        auto analog=[&](unsigned id,unsigned char decoded)
        {
            writeValue(static_cast<unsigned char*>(pad)+0x2698+16*id,decoded);
        };
        const bool ladder=!driving && LadderControlsBody(FindPlayerPed ? FindPlayerPed(0):nullptr);
        if(controllerLocomotion && !driving && (!animatedView || ladder) && input.gripPoseValid[0] &&
            input.moveActive && input.moveEnabled)
        {
            const auto axes=VRMath::TouchpadMovement(input.move[0],input.move[1]);
            auto axisByte=[](float axis)
            {
                // Undo the native signed decoder's additional 25% deadzone.
                const float native=std::abs(axis)>.0001f ? std::copysign(.25f+.75f*std::abs(axis),axis):0.f;
                return static_cast<unsigned char>(std::lround(127.5f+127.5f*native));
            };
            // Supply the actual shared movement values too: jump selection and
            // takeoff query them outside the walking-only decoder call sites.
            for(unsigned id:{12u,13u,91u}) analog(id,axisByte(ladder ? 0.f:axes[0]));
            for(unsigned id:{14u,15u,90u}) analog(id,axisByte(-axes[1]));
            // Ladder ProcessPed (CE CF9F30) reads the shared 14/15 controls
            // through 8F00F0; negative Y climbs up, positive Y climbs down.
            // Its animation owns the body, but must still receive our axis.
            const bool run=!ladder && std::hypot(axes[0],axes[1])>.01f &&
                (input.sprint || std::hypot(input.move[0],input.move[1])>=.85f);
            button(1,run,previousRun);
            previousRun=run;
        }
        else previousRun=false;
        if(driving && input.gripPoseValid[0])
        {
            float steering=input.steer;
            VRHands::WheelSteering(steering);
            const float throttle=input.accelerate;
            const float brake=VRHands::DrivingWeaponActive() && !input.phoneActive ? input.weaponBrake:input.brake;
            const auto encodedSteer=static_cast<unsigned char>(std::lround(127.5f+127.5f*steering));
            for(unsigned id:{30u,31u,111u}) analog(id,encodedSteer);
            analog(40,static_cast<unsigned char>(std::lround(throttle*255.f)));
            analog(41,static_cast<unsigned char>(std::lround(brake*255.f)));
            padSteering=steering; padThrottle=throttle; padBrake=brake;
            VRDiagnostics::Increment(padDrivingUpdates);
        }
        else padSteering=padThrottle=padBrake=0;
    }

    bool ControllerMoveAxes(std::array<float,2>& axes,OpenXRVR::ControllerInput* snapshot=nullptr)
    {
        if(!firstPersonActive || vehicleView || animatedView || ragdollView ||
            !OpenXRVR::HasFocusedViews() || (CMenuManager::m_MenuActive && *CMenuManager::m_MenuActive)) return false;
        AcquireSRWLockShared(&headingLock);
        const bool headingFresh=headingOwner && headingValid && GetTickCount64()-headingSampledAt<=250;
        ReleaseSRWLockShared(&headingLock);
        OpenXRVR::ControllerInput input{};
        if(!headingFresh || !OpenXRVR::GetControllerInput(input) || !input.moveActive ||
            !input.gripPoseValid[0] || !input.moveEnabled) return false;
        axes=VRMath::TouchpadMovement(input.move[0],input.move[1]);
        if(snapshot) *snapshot=input;
        return true;
    }

    void* __fastcall ReadPadRunControl(void* control,void* edx)
    {
        auto* original=runControl.unsafe_fastcall<unsigned char*>(control,edx);
        if(padGait<0 || !original) return original;
        // Native action bytes are XOR encoded with byte4. Supply a private
        // read-only view for this speed query; never mutate shared controls.
        std::memcpy(padRunControl,original,sizeof(padRunControl));
        const auto key=padRunControl[4];
        padRunControl[6]=static_cast<unsigned char>((padGait ? 255 : 0)^key);
        // Sprint's native tap accumulator sees a fresh press while click is
        // held. The original speed routine still owns stamina/exhaustion.
        padRunControl[7]=static_cast<unsigned char>((padGait==1 ? 255 : 0)^key);
        if(padGait==2) VRDiagnostics::Increment(padSprintReads);
        else if(padGait==1) VRDiagnostics::Increment(padJogReads);
        return padRunControl;
    }

    float __fastcall PadMovementSpeed(void* owner,void* edx,int mode,int tapMode)
    {
        std::array<float,2> axes{};
        OpenXRVR::ControllerInput input{};
        AcquireSRWLockShared(&headingLock);
        const bool local=owner && owner==headingOwner;
        ReleaseSRWLockShared(&headingLock);
        const int previous=padGait;
        if(local && ControllerMoveAxes(axes,&input))
        {
            const float radius=std::hypot(input.move[0],input.move[1]);
            const bool moving=std::hypot(axes[0],axes[1])>.01f;
            padGait=moving && input.sprint ? 2 : moving && radius>=.85f ? 1 : 0;
        }
        const float result=movementSpeed.unsafe_fastcall<float>(owner,edx,mode,tapMode);
        padGait=previous;
        return result;
    }

    void RecordControllerAxisCaller(uintptr_t caller)
    {
        VRDiagnostics::Increment(controllerAxisCalls);
        for(auto& recorded:controllerAxisCallers)
        {
            auto existing=recorded.load();
            if(existing==caller || (!existing && recorded.compare_exchange_strong(existing,caller))) break;
        }
    }

    float __cdecl ReadControllerMoveFloatAxis(const void* value)
    {
        const auto caller=reinterpret_cast<uintptr_t>(_ReturnAddress())-
            reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr))+0x400000;
        RecordControllerAxisCaller(caller);
        // Ordinary CTaskSimpleMovePlayer uses normalized float axes.
        const bool horizontal=caller==0x00A741FC;
        const bool vertical=caller==0x00A74212;
        std::array<float,2> axes{};
        if(horizontal || vertical)
        {
            VRDiagnostics::Increment(controllerMotorReads);
            if(ControllerMoveAxes(axes))
            {
                VRDiagnostics::Increment(controllerMoveReads);
                return horizontal ? axes[0] : -axes[1];
            }
        }
        return controllerMoveFloatAxis.ccall<float>(value);
    }

    int __cdecl ReadControllerMoveAxis(const void* value)
    {
        const auto caller=reinterpret_cast<uintptr_t>(_ReturnAddress())-
            reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr))+0x400000;
        RecordControllerAxisCaller(caller);
        // Restrict substitution to the normal and directional on-foot motors.
        // Other control reads (camera, menus, vehicles, NPCs) remain native.
        const bool horizontal=caller==0x00CD4B54 || caller==0x00CD4EB8 || caller==0x00A74701;
        const bool vertical=caller==0x00CD4B73 || caller==0x00CD4ED7 || caller==0x00A7471E;
        if(horizontal || vertical) VRDiagnostics::Increment(controllerMotorReads);
        std::array<float,2> axes{};
        if((horizontal || vertical) && ControllerMoveAxes(axes))
        {
                VRDiagnostics::Increment(controllerMoveReads);
                // Native motors negate vertical input; OpenXR pad +Y is forward.
                return static_cast<int>(std::lround((horizontal ? axes[0] : -axes[1])*127.5f));
        }
        return controllerMoveAxis.ccall<int>(value);
    }

    // CE CPedMoveBlendOnFoot virtual +0x10, thiscall(output*, dt), ret 8.
    // Call the original motor unchanged, then correct horizontal delta velocity
    // for room motion. The physics consumer still owns force/collision; no ped
    // transform, velocity setter or animation-target field is overwritten.
    float* __fastcall ObserveMoveBlend(void* self,void* edx,float* output,float dt)
    {
        const auto caller=reinterpret_cast<uintptr_t>(_ReturnAddress());
        auto* owner=*reinterpret_cast<void**>(static_cast<char*>(self)+0x24);
        auto* result=moveBlendOutput.unsafe_fastcall<float*>(self,edx,output,dt);
        if(caller-reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr))+0x400000==0x009E3337)
            ProbeRoomCollision(owner);
        if((probeAnchor || VRDiagnostics::counters) && owner && owner==localPed.load() && result)
        {
            AcquireSRWLockExclusive(&motionLock);
            VRDiagnostics::Increment(moveBlendCalls);
            moveBlendCaller=caller;
            lastMoveDt=dt;
            std::memcpy(lastMoveBlend,result,sizeof(lastMoveBlend));
            std::memcpy(lastMoveTarget,static_cast<char*>(owner)+0xA90,sizeof(lastMoveTarget));
            ReleaseSRWLockExclusive(&motionLock);
        }
        return result;
    }

    void __fastcall ObserveVelocity(void* self,void* edx,const float* velocity)
    {
        if((probeAnchor || VRDiagnostics::counters) && self==localCollider.load() && velocity)
        {
            AcquireSRWLockExclusive(&motionLock);
            VRDiagnostics::Increment(velocityCalls);
            std::memcpy(lastVelocity,velocity,sizeof(lastVelocity));
            ReleaseSRWLockExclusive(&motionLock);
        }
        colliderVelocity.unsafe_fastcall(self,edx,velocity);
    }

    void ProcessPhysicalCrouch()
    {
        if(!physicalCrouchEnabled || !firstPersonActive || vehicleView || !OpenXRVR::HasFocusedViews() ||
            (CMenuManager::m_MenuActive && *CMenuManager::m_MenuActive)) return;
        using Hash=Natives::NativeHashes;
        for(auto hash:{Hash::GET_PLAYER_ID,Hash::GET_PLAYER_CHAR,Hash::DOES_CHAR_EXIST,
            Hash::IS_CHAR_IN_ANY_CAR,Hash::IS_CHAR_DEAD,Hash::IS_CHAR_DUCKING,Hash::SET_CHAR_DUCKING,Hash::IS_CHAR_IN_AIR,
            Hash::HAS_CUTSCENE_LOADED,Hash::HAS_CUTSCENE_FINISHED})
            if(!NativeInvoke::GetNativeHandler(std::to_underlying(hash))) return;
        Ped ped=0; Natives::GetPlayerChar(Natives::GetPlayerId(),&ped);
        if(!ped || !Natives::DoesCharExist(ped) || Natives::IsCharInAnyCar(ped) || Natives::IsCharDead(ped) || Natives::IsCharInAir(ped) ||
            (Natives::HasCutsceneLoaded() && !Natives::HasCutsceneFinished())) return;
        bool ragdoll=false;
        if(AnimationControlsBody(ped,FindPlayerPed ? FindPlayerPed(0) : nullptr,ragdoll)) return;
        if(ped!=physicalCrouchOwner)
        {physicalCrouchOwner=ped; physicalCrouchOwnsDuck=false; physicalDuckAcknowledged=false;}
        const bool ducking=Natives::IsCharDucking(ped);
        // Observe the game's response, so keyboard and gamepad bindings both
        // work. Once duck was acknowledged, its cancellation must not be
        // immediately replaced by the still-low physical height.
        const bool canceled=physicalDuckAcknowledged && !ducking;
        AcquireSRWLockExclusive(&roomLock);
        if(canceled) physicalCrouch.CancelUntilStanding();
        const float height=physicalEyeHeight;
        const bool valid=std::isfinite(height) && height>=.45f && height<=2.5f;
        const bool lowered=valid && physicalCrouch.Update(height);
        ReleaseSRWLockExclusive(&roomLock);
        if(canceled) {physicalCrouchOwnsDuck=false; physicalDuckAcknowledged=false;}
        if(!valid) return;
        if(ducking) physicalDuckAcknowledged=true;
        if(lowered && !ducking)
        {Natives::SetCharDucking(ped,true); physicalCrouchOwnsDuck=true; physicalDuckAcknowledged=false;}
        else if(!lowered && physicalCrouchOwnsDuck)
        {
            if(ducking) Natives::SetCharDucking(ped,false);
            physicalCrouchOwnsDuck=false;
            physicalDuckAcknowledged=false;
        }
    }

    Vector3 HeadPosition(Ped ped)
    {
        Vector3 head{NAN,NAN,NAN};
        NativeContext context;
        context.Push(ped); context.Push(uint32_t{1205});
        context.Push(0.f); context.Push(0.f); context.Push(0.f); context.Push(&head);
        auto handler=NativeInvoke::GetNativeHandler(std::to_underlying(Natives::NativeHashes::GET_PED_BONE_POSITION));
        if(!handler) return head;
        handler(&context);
        // Explicit vector output finalization; generic Invoke<void> skips it.
        (void)context.scrNativeCallContext::GetResult<uint32_t>(0);
        return head;
    }

    bool GameplayAnchor(VRMath::Vector& position,float& heading,float& height,float& groundZ,
        Vehicle& vehicle,bool& ducking,bool& airborne,VRMath::Matrix& vehicleBasis,bool& animated,bool& ragdoll,bool& exiting)
    {
        using Hash=Natives::NativeHashes;
        for(auto hash:{Hash::GET_PLAYER_ID,Hash::GET_PLAYER_CHAR,Hash::DOES_CHAR_EXIST,
            Hash::GET_CHAR_COORDINATES,Hash::GET_CHAR_HEADING,Hash::IS_CHAR_IN_ANY_CAR,
            Hash::IS_CHAR_DEAD,Hash::HAS_CUTSCENE_LOADED,Hash::HAS_CUTSCENE_FINISHED})
            if(!NativeInvoke::GetNativeHandler(std::to_underlying(hash))) return false;
        if(Natives::HasCutsceneLoaded() && !Natives::HasCutsceneFinished()) return false;
        Ped ped=0;
        Natives::GetPlayerChar(Natives::GetPlayerId(),&ped);
        if(!ped || !Natives::DoesCharExist(ped)) return false;
        position={NAN,NAN,NAN,1}; heading=NAN;
        Natives::GetCharCoordinates(ped,&position[0],&position[1],&position[2]);
        Natives::GetCharHeading(ped,&heading);
        auto head=HeadPosition(ped);
        void* owner=FindPlayerPed ? FindPlayerPed(0) : nullptr;
        // Task lists are traversed only by simulation callbacks. Rendering
        // consumes a short-lived snapshot belonging to this same player.
        AcquireSRWLockShared(&animationLock);
        if(owner && owner==animationOwner && ped==animationPed && GetTickCount64()-animationSampledAt<=250)
        {animated=animationDriven; ragdoll=animationRagdoll; exiting=animationVehicleExit;}
        ReleaseSRWLockShared(&animationLock);
        // Ejection can be fatal: retain the ragdoll anchor while that body
        // still exists, rather than selecting the ordinary death/chase camera.
        if(Natives::IsCharDead(ped) && !ragdoll) return false;
        if(ragdoll && owner && NaturalMotion::GetRagdollComponentMatrixInternal)
        {
            NaturalMotion::RagdollComponentMatrix neck{};
            if(NaturalMotion::GetRagdollComponentMatrixInternal(reinterpret_cast<uintptr_t>(owner),&neck,
                static_cast<int32_t>(NaturalMotion::RagdollComponent::Neck)) &&
                std::isfinite(neck.values[12]+neck.values[13]+neck.values[14]))
                head={neck.values[12],neck.values[13],neck.values[14]+.12f};
        }
        height=head.fZ-position[2];
        ducking=NativeInvoke::GetNativeHandler(std::to_underlying(Hash::IS_CHAR_DUCKING)) && Natives::IsCharDucking(ped);
        airborne=NativeInvoke::GetNativeHandler(std::to_underlying(Hash::IS_CHAR_IN_AIR)) && Natives::IsCharInAir(ped);
        if((!animated || exiting) && !ragdoll && Natives::IsCharInAnyCar(ped))
        {
            for(auto hash:{Hash::GET_CAR_CHAR_IS_USING,Hash::GET_CAR_HEADING,Hash::DOES_VEHICLE_EXIST,
                Hash::GET_OFFSET_FROM_CAR_IN_WORLD_COORDS,Hash::GET_OFFSET_FROM_CAR_GIVEN_WORLD_COORDS,
                Hash::IS_CHAR_SITTING_IN_ANY_CAR})
                if(!NativeInvoke::GetNativeHandler(std::to_underlying(hash))) return false;
            Natives::GetCarCharIsUsing(ped,&vehicle);
            if(!vehicle || !Natives::DoesVehicleExist(vehicle)) return false;
            Natives::GetCarHeading(vehicle,&heading);
            if(seatVehicle!=vehicle || seatPed!=ped)
            {
                seatVehicle=vehicle; seatPed=ped; seatAnchorValid=false;seatIsCar=false;
                if(NativeInvoke::GetNativeHandler(std::to_underlying(Natives::NativeHashes::GET_CAR_MODEL)) &&
                    NativeInvoke::GetNativeHandler(std::to_underlying(Natives::NativeHashes::IS_THIS_MODEL_A_CAR)))
                {
                    uint32_t model=0;Natives::GetCarModel(vehicle,&model);seatIsCar=Natives::IsThisModelACar(model);
                }
            }
            // Calibrate once after the ped is seated. Animated bone transforms
            // can lag the vehicle and must not supply the moving camera origin.
            if(!seatAnchorValid && Natives::IsCharSittingInAnyCar(ped) &&
                std::isfinite(head.fX+head.fY+head.fZ) &&
                std::hypot(head.fX-position[0],head.fY-position[1])<5 && std::abs(height)<5)
            {
                seatOffset={NAN,NAN,NAN,1};
                Natives::GetOffsetFromCarGivenWorldCoords(vehicle,head.fX,head.fY,head.fZ,
                    &seatOffset[0],&seatOffset[1],&seatOffset[2]);
                seatAnchorValid=std::isfinite(seatOffset[0]+seatOffset[1]+seatOffset[2]) &&
                    std::hypot(seatOffset[0],seatOffset[1])<5 && std::abs(seatOffset[2])<5;
            }
            auto carPoint=[vehicle](float x,float y,float z)
            {
                VRMath::Vector point{NAN,NAN,NAN,1};
                Natives::GetOffsetFromCarInWorldCoords(vehicle,x,y,z,&point[0],&point[1],&point[2]);
                return point;
            };
            const auto origin=carPoint(0,0,0);
            const auto right=carPoint(1,0,0),up=carPoint(0,0,1),back=carPoint(0,-1,0);
            vehicleBasis=VRMath::Identity();
            for(unsigned axis=0;axis<3;++axis)
            {
                vehicleBasis[0][axis]=right[axis]-origin[axis];
                vehicleBasis[1][axis]=up[axis]-origin[axis];
                vehicleBasis[2][axis]=back[axis]-origin[axis];
            }
            if(seatAnchorValid && !exiting)
                position=carPoint(seatOffset[0],seatOffset[1],seatOffset[2]+vehicleEyeHeightOffset);
            else if(std::isfinite(head.fX+head.fY+head.fZ))
            {
                position={head.fX,head.fY,head.fZ,1};
                for(unsigned axis=0;axis<3;++axis)
                    position[axis]+=vehicleEyeHeightOffset*vehicleBasis[1][axis];
            }
            else return false;
            groundZ=NAN;
            return std::isfinite(position[0]+position[1]+position[2]+heading);
        }
        seatVehicle=0; seatPed=0; seatAnchorValid=false;
        // Jump/fall/get-up animation can move the head below the root. That
        // is not a reason to return to a third-person camera.
        if(!std::isfinite(height) || std::abs(height)>5) height=headHeight>.1f ? headHeight : .7f;
        float aboveGround=NAN;
        if(NativeInvoke::GetNativeHandler(std::to_underlying(Hash::GET_CHAR_HEIGHT_ABOVE_GROUND)))
            Natives::GetCharHeightAboveGround(ped,&aboveGround);
        groundZ=std::isfinite(aboveGround) && aboveGround>=0 && aboveGround<20 ? position[2]-aboveGround : NAN;
        if(animated && std::isfinite(head.fX+head.fY+head.fZ))
        {
            // Vaults and ragdolls can move the skeleton without moving the
            // script-native root. Follow translation, never forced body roll.
            position={head.fX,head.fY,head.fZ,1};
            groundZ=NAN;
        }
        return std::isfinite(position[0]+position[1]+position[2]+heading+height);
    }

    // Runs on the world list-building thread. Read-only native queries; keep
    // this bounded and disabled by default while establishing camera ownership.
    void ProbeAnchor(const rage::grcViewport& base)
    {
        if(!probeAnchor || anchorSamples>=probeSamples || ++anchorFrames<180 ||
            (anchorFrames-180)%120) return;
        using Hash=Natives::NativeHashes;
        for(auto hash:{Hash::GET_PLAYER_ID,Hash::GET_PLAYER_CHAR,
            Hash::GET_CHAR_COORDINATES,Hash::GET_CHAR_HEADING,Hash::IS_CHAR_IN_ANY_CAR,
            Hash::GET_PED_BONE_POSITION})
            if(!NativeInvoke::GetNativeHandler(std::to_underlying(hash))) return;
        Ped ped=0;
        Natives::GetPlayerChar(Natives::GetPlayerId(),&ped);
        if(!ped) return;
        float position[3]{NAN,NAN,NAN},heading=NAN;
        Natives::GetCharCoordinates(ped,&position[0],&position[1],&position[2]);
        Natives::GetCharHeading(ped,&heading);
        if(!std::isfinite(position[0]+position[1]+position[2]+heading)) return;
        // GTA IV Head=1205 in Sanny Builder's gta_iv/enums.json. Treat the
        // resulting point as a candidate until paired runtime samples verify it.
        const auto head=HeadPosition(ped);
        wchar_t path[MAX_PATH]{};
        if(!GetModuleFileNameW(nullptr,path,MAX_PATH)) return;
        auto* name=wcsrchr(path,L'\\');
        if(!name) return;
        name[1]=L'\0';
        if(wcscat_s(path,L"FusionFix.vr-camera.log")) return;
        FILE* file=nullptr;
        if(_wfopen_s(&file,path,anchorSamples ? L"a" : L"w")) return;
        fprintf(file,"issue014 sample=%u frame=%u pedHandle=%d pedPointer=%p inVehicle=%u char=(%.6f %.6f %.6f) heading=%.6f\n",
            anchorSamples,anchorFrames,ped,FindPlayerPed ? FindPlayerPed(0) : nullptr,
            Natives::IsCharInAnyCar(ped),position[0],position[1],position[2],heading);
        fprintf(file,"candidateHeadBone1205=(%.6f %.6f %.6f)\n",head.fX,head.fY,head.fZ);
        fprintf(file,"firstPersonConfigured=%u firstPersonActive=%u\n",firstPerson,firstPersonActive.load());
        AcquireSRWLockShared(&motionLock);
        fprintf(file,"motionProbe collider=%p velocityCalls=%u lastVelocity=(%.6f %.6f %.6f)\n",
            localCollider.load(),velocityCalls,lastVelocity[0],lastVelocity[1],lastVelocity[2]);
        const auto moduleBase=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        fprintf(file,"moveBlendProbe installed=%u calls=%u callerVA=%08X dt=%.6f output=(%.6f %.6f %.6f) target=(%.6f %.6f %.6f)\n",
            bool(moveBlendOutput),moveBlendCalls,
            moveBlendCaller ? unsigned(moveBlendCaller-moduleBase+0x400000) : 0,lastMoveDt,
            lastMoveBlend[0],lastMoveBlend[1],lastMoveBlend[2],
            lastMoveTarget[0],lastMoveTarget[1],lastMoveTarget[2]);
        ReleaseSRWLockShared(&motionLock);
        for(unsigned row=0;row<4;++row)
            fprintf(file,"gameCamera[%u]=(%.6f %.6f %.6f %.6f)\n",row,
                base.mViewInverseMatrix[row][0],base.mViewInverseMatrix[row][1],
                base.mViewInverseMatrix[row][2],base.mViewInverseMatrix[row][3]);
        fclose(file);
        ++anchorSamples;
        if(anchorSamples>=probeSamples) motionProbeComplete=true;
    }
}

void VRCamera::SetVisibilityAvailable(bool available) {visibilityAvailable=available; if(!available) {InvalidateHeading(); StopRoomMotion();}}
void VRCamera::Recenter() {InvalidateHeading(); StopRoomMotion(); recenter=true;}
std::string VRCamera::AimStatus()
{
    AcquireSRWLockShared(&aimLock);
    char text[512];
    sprintf_s(text,"{\"configured\":%s,\"installed\":%s,\"fresh\":%s,\"pitchRadians\":%.6f,\"yawRadians\":%.6f,\"basisOverrides\":%u,\"cameraUpdates\":%u,\"lockonSuppressed\":%s}",
        headsetAim ? "true":"false",weaponCameraUpdate && weaponBasis[0] && weaponBasis[1] ? "true":"false",
        aimValid && GetTickCount64()-aimSampledAt<=250 ? "true":"false",aimPitch,aimYaw,aimOverrides,aimUpdates,
        lockonSuppressed ? "true":"false");
    ReleaseSRWLockShared(&aimLock);
    return text;
}
std::string VRCamera::MovementStatus()
{
    const bool focused=OpenXRVR::HasFocusedViews();
    AcquireSRWLockShared(&headingLock);
    char text[512];
    sprintf_s(text,"{\"configured\":%s,\"installed\":%s,\"active\":%s,\"headingRadians\":%.6f,\"queries\":%u,\"overrides\":%u,\"originalRadians\":%.6f,\"appliedRadians\":%.6f,\"firstPerson\":%s,\"vehicle\":%s,\"ducking\":%s,\"airborne\":%s,\"animated\":%s,\"ragdoll\":%s,\"vehicleExiting\":%s}",
        headRelativeMovement ? "true" : "false",movementHeading ? "true" : "false",
        focused && headingValid && GetTickCount64()-headingSampledAt<=250 ? "true" : "false",
        headHeading,headingQueries,headingOverrides,lastOriginalHeading,lastAppliedHeading,
        firstPersonActive ? "true" : "false",vehicleView ? "true" : "false",
        crouchedView ? "true" : "false",airborneView ? "true" : "false",animatedView ? "true" : "false",ragdollView ? "true" : "false",vehicleExitView ? "true" : "false");
    ReleaseSRWLockShared(&headingLock);
    AcquireSRWLockShared(&roomLock);
    char room[640];
    sprintf_s(room,",\"physicalCrouchInhibited\":%s,\"roomScale\":{\"configured\":%s,\"installed\":%s,\"ownerReady\":%s,\"motorCalls\":%u,\"pending\":[%.6f,%.6f],\"consumed\":[%.6f,%.6f],\"achieved\":[%.6f,%.6f],\"groundZ\":%.6f,\"floorEyeHeight\":%.6f,\"body\":[%.6f,%.6f,%.6f],\"view\":[%.6f,%.6f,%.6f]}}",
        physicalCrouch.inhibited ? "true" : "false",roomScale ? "true" : "false",positionSetter && physicsSync && volumeQuery ? "true" : "false",roomOwner ? "true" : "false",
        roomMotorCalls,roomMotion.pending[0],roomMotion.pending[1],roomMotion.consumed[0],roomMotion.consumed[1],
        roomAchieved[0],roomAchieved[1],std::isfinite(gameGroundZ)?gameGroundZ:0,std::isfinite(physicalEyeHeight)?physicalEyeHeight:0,
        diagnosticBody[0],diagnosticBody[1],diagnosticBody[2],diagnosticView[0],diagnosticView[1],diagnosticView[2]);
    ReleaseSRWLockShared(&roomLock);
    std::string result=text;
    result.pop_back();
    return result+room;
}

std::string VRCamera::ControllerMovementStatus()
{
    char text[600];
    sprintf_s(text,"{\"configured\":%s,\"installed\":%s,\"axisReads\":%u,\"deadzone\":0.15,\"decoderCalls\":%u,\"motorReads\":%u,\"directionalInstalled\":%s,\"directionalCalls\":%u,\"gaitInstalled\":%s,\"jogReads\":%u,\"sprintReads\":%u,\"callers\":[%u,%u,%u,%u,%u,%u,%u,%u]}",
        controllerLocomotion ? "true":"false",controllerMoveAxis && controllerMoveFloatAxis ? "true":"false",controllerMoveReads.load(),
        controllerAxisCalls.load(),controllerMotorReads.load(),
        ordinaryWalk && directionalWalk ? "true":"false",directionalWalkCalls.load(),
        movementSpeed && runControl ? "true":"false",padJogReads.load(),padSprintReads.load(),
        static_cast<unsigned>(controllerAxisCallers[0].load()),static_cast<unsigned>(controllerAxisCallers[1].load()),
        static_cast<unsigned>(controllerAxisCallers[2].load()),static_cast<unsigned>(controllerAxisCallers[3].load()),
        static_cast<unsigned>(controllerAxisCallers[4].load()),static_cast<unsigned>(controllerAxisCallers[5].load()),
        static_cast<unsigned>(controllerAxisCallers[6].load()),static_cast<unsigned>(controllerAxisCallers[7].load()));
    std::string result=text; result.pop_back();
    char buttons[340];
    sprintf_s(buttons,",\"buttonsConfigured\":%s,\"buttonsInstalled\":%s,\"buttonUpdates\":%u,\"jumpPresses\":%u,\"enterPresses\":%u,\"drivingUpdates\":%u,\"steering\":%.3f,\"throttle\":%.3f,\"brake\":%.3f,\"hangDrops\":%u}",
        controllerButtons ? "true":"false",controllerPadUpdate ? "true":"false",padButtonUpdates.load(),
        padJumpPresses.load(),padEnterPresses.load(),padDrivingUpdates.load(),padSteering.load(),padThrottle.load(),padBrake.load(),padHangDrops.load());
    result+=buttons;result.pop_back();result+=",\"phoneDelivered\":[";
    for(unsigned i=0;i<6;++i) {if(i) result+=",";result+=std::to_string(phoneDelivered[i].load());}
    return result+"],\"phoneRawInstalled\":"+(phoneRawPressed && phoneRawJustPressed ? "true":"false")+
        ",\"phoneRawQueries\":"+std::to_string(phoneRawQueries.load())+
        ",\"phoneRawMatches\":"+std::to_string(phoneRawMatches.load())+
        ",\"scriptCameraResolved\":"+(scriptCameraRoot && scriptCameraDraw ? "true":"false")+
        ",\"scriptCameraActive\":"+(scriptCameraActive.load() ? "true":"false")+
        ",\"missionCoverAdapterReady\":"+(missionCoverInput && missionCoverTask ? "true":"false")+"}";
}
bool VRCamera::RestartAnchorProbe()
{
    if(!probeAnchor) return false;
    restartAnchorProbe=true;
    return true;
}

bool VRCamera::HideLocalCharacter(const void* entity)
{
    return firstPersonActive && !scriptCameraActive && visibilityAvailable && FindPlayerPed && entity==FindPlayerPed(0) &&
        !(showBody && headlessBodyAvailable.load());
}

bool VRCamera::ShowLocalBody(const void* entity)
{
    return firstPersonActive && !scriptCameraActive && visibilityAvailable && showBody && headlessBodyAvailable.load() &&
        FindPlayerPed && entity==FindPlayerPed(0);
}
void VRCamera::SetHeadlessBodyAvailable(bool available) {headlessBodyAvailable=available;}

std::string VRCamera::AudioStatus()
{
    AcquireSRWLockShared(&audioLock);
    const auto head=audioHead,native=audioNative;
    const auto at=audioHeadAt;
    ReleaseSRWLockShared(&audioLock);
    char text[1024]{};
    sprintf_s(text,"{\"hookReady\":%s,\"updates\":%u,\"spatialOverrides\":%u,\"distanceOverrides\":%u,\"poseAgeMs\":%llu,\"headPosition\":[%.6f,%.6f,%.6f],\"headForward\":[%.6f,%.6f,%.6f],\"nativePosition\":[%.6f,%.6f,%.6f],\"nativeForward\":[%.6f,%.6f,%.6f]}",
        audioUpdate ? "true":"false",audioUpdates.load(),audioOverrides[0].load(),audioOverrides[1].load(),
        static_cast<unsigned long long>(at ? GetTickCount64()-at:0),
        head[3][0],head[3][1],head[3][2],head[1][0],head[1][1],head[1][2],
        native[3][0],native[3][1],native[3][2],native[1][0],native[1][1],native[1][2]);
    return text;
}

void VRCamera::Install()
{
    CIniReader ini("");
    showBody=ini.ReadInteger("VR","ShowFirstPersonBody",1)!=0;
    probeAnchor=ini.ReadInteger("DEVELOPMENT","ProbeFirstPersonAnchor",0)!=0;
    probeSamples=std::clamp(ini.ReadInteger("DEVELOPMENT","FirstPersonProbeSamples",8),1,240);
    firstPerson=ini.ReadInteger("VR","ExperimentalFirstPerson",0)!=0;
    if(firstPerson)
    {
        const auto base=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        const unsigned char updateSignature[]{0x55,0x8B,0xEC,0x83,0xE4,0xF0,0x81,0xEC,0xC4,0x01,0,0,0x53,0x56,0x57,0x8B,0xF9};
        const unsigned char setterSignature[]{0x55,0x8B,0xEC,0x83,0xE4,0xF0,0x81,0xEC,0xC8,0,0,0,0x56,0x8B,0x75,0x08,0x57};
        auto* update=reinterpret_cast<void*>(base+0x56D490);
        auto* spatial=reinterpret_cast<void*>(base+0x4A8BE0);
        auto* distance=reinterpret_cast<void*>(base+0x4A8F50);
        // Verified CE 1.2.0.59 entries; unknown executables retain native audio.
        if(!std::memcmp(update,updateSignature,sizeof(updateSignature)) &&
            !std::memcmp(spatial,setterSignature,sizeof(setterSignature)) &&
            !std::memcmp(distance,setterSignature,sizeof(setterSignature)))
        {
            audioListeners[0]=safetyhook::create_mid(spatial,AudioListenerSpatial);
            audioListeners[1]=safetyhook::create_mid(distance,AudioListenerDistance);
            if(audioListeners[0] && audioListeners[1])
                audioUpdate=safetyhook::create_inline(update,UpdateAudioListeners);
            if(!audioUpdate) {audioListeners[0].reset();audioListeners[1].reset();}
        }
    }
    controllerButtons=firstPerson && ini.ReadInteger("VR","ExperimentalControllerInput",0) &&
        ini.ReadInteger("VR","ExperimentalControllerButtons",0);
    if(controllerButtons)
    {
        auto coverInput=hook::pattern("68 ? ? ? ? 68 61 57 63 0E E8");
        auto coverTask=hook::pattern("68 ? ? ? ? 68 D3 58 33 14 E8");
        if(coverInput.size()==1 && coverTask.size()==1)
        {
            missionCoverInput=safetyhook::create_inline(*coverInput.get_first<void*>(1),ConcreteJungleCoverInput);
            missionCoverTask=safetyhook::create_inline(*coverTask.get_first<void*>(1),ConcreteJungleCoverTask);
            if(!missionCoverInput || !missionCoverTask)
            {missionCoverInput.reset();missionCoverTask.reset();}
        }
        // CE GET_SCRIPT_DRAW_CAM implementation: static camera manager, root
        // accessor, draw accessor, then handle-pool conversion. Unknown builds
        // retain the native camera and do not activate scripted-menu routing.
        auto camera=hook::pattern("B9 ? ? ? ? E8 ? ? ? ? 8B C8 E8 ? ? ? ? 8B 0D ? ? ? ? 50 E8 ? ? ? ? 8B 4C 24 04 89 01 C2 04 00");
        void* cameraManager=nullptr;
        ScriptCameraAccessor cameraRoot=nullptr,cameraDraw=nullptr;
        unsigned scriptCameraMatches=0;
        for(size_t i=0;i<camera.size();++i)
        {
            auto* code=camera.get(i).get<unsigned char>();
            auto* root=code+10+*reinterpret_cast<int32_t*>(code+6);
            auto* draw=code+17+*reinterpret_cast<int32_t*>(code+13);
            // GET_SCRIPT_DRAW_CAM selects root type 6. The same outer code
            // also belongs to a type-39 camera native; do not reject the valid
            // script candidate just because the outer pattern is shared.
            const unsigned char rootBytes[]{0x8B,0x49,0x04,0x85,0xC9,0x75,0x03,0x33,0xC0,0xC3,0x6A,0x00,0x6A,0x06};
            const unsigned char drawBytes[]{0x81,0xEC,0xF0,0,0,0,0x8D,0x04,0x24,0x56,0x57,0x50,0x8B,0xF9};
            if(!std::memcmp(root,rootBytes,sizeof(rootBytes)) && !std::memcmp(draw,drawBytes,sizeof(drawBytes)))
            {
                ++scriptCameraMatches;
                cameraManager=*reinterpret_cast<void**>(code+1);
                cameraRoot=reinterpret_cast<ScriptCameraAccessor>(root);
                cameraDraw=reinterpret_cast<ScriptCameraAccessor>(draw);
            }
        }
        if(scriptCameraMatches==1)
        {scriptCameraManager=cameraManager;scriptCameraRoot=cameraRoot;scriptCameraDraw=cameraDraw;}
        auto update=hook::pattern("83 EC 1C 56 6A 00 6A 01 8B F1 E8 ? ? ? ? 83 C4 04 8B C8 E8 ? ? ? ? 50 E8");
        if(update.size()==1)
        {
            auto* code=update.get_first<unsigned char>();
            auto* getter=code+15+*reinterpret_cast<int32_t*>(code+11);
            // Native GetPad returns the fixed local table on this CE build.
            if(getter[0]==0xB8 && getter[5]==0xC3)
            {
                localPad=reinterpret_cast<PadGetter>(getter);
                controllerPadUpdate=safetyhook::create_inline(code,UpdateControllerPad);
                auto pressed=hook::pattern("FF 74 24 04 E8 ? ? ? ? 83 C4 04 85 C0 74 60 8B 4C 24 08 83 F9 13");
                auto justPressed=hook::pattern("56 FF 74 24 08 E8 ? ? ? ? 8B F0 83 C4 04 85 F6 0F 84 ? ? ? ? 83 7C 24 08 04");
                if(controllerPadUpdate && pressed.size()==1 && justPressed.size()==1)
                {
                    phoneRawPressed=safetyhook::create_inline(pressed.get_first(),PhoneRawPressed);
                    phoneRawJustPressed=safetyhook::create_inline(justPressed.get_first(),PhoneRawJustPressed);
                    if(!phoneRawPressed || !phoneRawJustPressed)
                    {phoneRawPressed.reset();phoneRawJustPressed.reset();}
                }
            }
        }
    }
    roomScale=firstPerson && ini.ReadInteger("VR","ExperimentalRoomScale",0)!=0;
    physicalCrouchEnabled=firstPerson && ini.ReadInteger("VR","ExperimentalPhysicalCrouch",0)!=0;
    const float crouchReference=ini.ReadFloat("VR","PlayerHeight",1.75f);
    const float vehicleHeight=ini.ReadFloat("VR","VehicleEyeHeightOffset",.075f);
    if(std::isfinite(vehicleHeight)) vehicleEyeHeightOffset=std::clamp(vehicleHeight,-.3f,.5f);
    if(std::isfinite(crouchReference) && crouchReference>=.45f && crouchReference<=2.5f)
        physicalCrouch.reference=crouchReference;
    if(firstPerson) FusionFix::onGameProcessEvent() += SampleAnimationState;
    if(physicalCrouchEnabled) FusionFix::onGameProcessEvent() += ProcessPhysicalCrouch;
    headRelativeMovement=firstPerson && ini.ReadInteger("VR","ExperimentalHeadRelativeMovement",0)!=0;
    headsetAim=firstPerson && ini.ReadInteger("VR","ExperimentalHeadsetAim",0)!=0;
    if(headsetAim)
    {
        auto update=hook::pattern("55 8B EC 83 E4 F0 81 EC A4 01 00 00 53 56 57 8B F9 C6 87 C4 02 00 00 00");
        auto basis0=hook::pattern("F3 0F 11 04 24 E8 ? ? ? ? F3 0F 10 4F 20 F3 0F 10 57 28");
        auto basis1=hook::pattern("F3 0F 11 04 24 E8 ? ? ? ? F3 0F 10 47 20 F3 0F 10 5C 24 10");
        if(update.size()==1 && basis0.size()==1 && basis1.size()==1)
        {
            weaponBasis[0]=safetyhook::create_mid(basis0.get_first(),HeadsetWeaponBasis);
            weaponBasis[1]=safetyhook::create_mid(basis1.get_first(),HeadsetWeaponBasis);
            weaponCameraUpdate=safetyhook::create_inline(update.get_first(),UpdateWeaponCamera);
            if(weaponCameraUpdate && weaponBasis[0] && weaponBasis[1])
                FusionFix::onGameProcessEvent() += SuppressAimLockon;
            else headsetAim=false;
        }
        else headsetAim=false;
    }
    if(headRelativeMovement)
    {
        auto heading=hook::pattern("56 57 6A 00 8B F9 8B 0D ? ? ? ? 6A 01 E8 ? ? ? ? 8B F0 85 F6 74 75 8B 16 8B CE FF 52 0C");
        if(heading.size()==1) movementHeading=safetyhook::create_inline(heading.get_first(),WalkingHeading);
    }
    controllerLocomotion=headRelativeMovement && movementHeading &&
        ini.ReadInteger("VR","ExperimentalControllerInput",0) &&
        ini.ReadInteger("VR","ExperimentalControllerLocomotion",0);
    if(headRelativeMovement && movementHeading && ini.ReadInteger("VR","ExperimentalHandIK",0))
    {
        auto ordinary=hook::pattern("55 8B EC 83 E4 F0 83 EC 38 56 57 8B 7D 08 8B F1 8B CF 89 74 24 24 E8 ? ? ? ? 8B C8 89 44 24 20");
        auto directional=hook::pattern("55 8B EC 83 E4 F0 83 EC 28 56 57 8B 7D 08 89 4C 24 18 6A 01 8B CF E8 ? ? ? ? 8B CF E8 ? ? ? ? 8B F0");
        if(ordinary.size()==1 && directional.size()==1)
        {
            directionalWalk=reinterpret_cast<DirectionalWalk>(directional.get_first());
            ordinaryWalk=safetyhook::create_inline(ordinary.get_first(),WalkFacingHead);
        }
    }
    if(controllerLocomotion)
    {
        // Integer and float decoders share a prologue. Include the signed
        // normalization and integer-only result conversion to distinguish it.
        auto axis=hook::pattern("8B 44 24 04 F3 0F 10 0D ? ? ? ? 8A 48 06 32 48 04 0F B6 C1 66 0F 6E C0 0F 5B C0 F3 0F 5C 05 ? ? ? ? F3 0F 59 05 ? ? ? ? 0F 2F C8 77 10 F3 0F 10 0D ? ? ? ? 0F 2F C1 77 03 0F 28 C8 83 EC 08 C7 44 24 04 00 00 80 3E F3 0F 11 0C 24 E8 ? ? ? ? D9 5C 24 0C");
        if(axis.size()==1) controllerMoveAxis=safetyhook::create_inline(axis.get_first(),ReadControllerMoveAxis);
        auto floatAxis=hook::pattern("8B 44 24 04 F3 0F 10 0D ? ? ? ? 8A 48 06 32 48 04 0F B6 C1 66 0F 6E C0 0F 5B C0 F3 0F 5C 05 ? ? ? ? F3 0F 59 05 ? ? ? ? 0F 2F C8 77 10 F3 0F 10 0D ? ? ? ? 0F 2F C1 77 03 0F 28 C8 83 EC 08 C7 44 24 04 00 00 80 3E F3 0F 11 0C 24 E8 ? ? ? ? 83 C4 08 C3");
        if(floatAxis.size()==1) controllerMoveFloatAxis=safetyhook::create_inline(floatAxis.get_first(),ReadControllerMoveFloatAxis);
        auto speed=hook::pattern("56 8B F1 8B 86 28 02 00 00 85 C0 0F 84 ? ? ? ? 83 C0 70 0F 84 ? ? ? ? 55 E8 ? ? ? ? 8B E8 85 ED");
        auto run=hook::pattern("83 3D ? ? ? ? 00 75 2E 80 B9 8D 32 00 00 00 74 25");
        if(speed.size()==1 && run.size()==1)
        {
            runControl=safetyhook::create_inline(run.get_first(),ReadPadRunControl);
            if(runControl) movementSpeed=safetyhook::create_inline(speed.get_first(),PadMovementSpeed);
        }
    }
    if(probeAnchor || roomScale)
    {
        if(probeAnchor || roomScale)
        {
            auto query=hook::pattern("8B 4C 24 10 53 8B 5C 24 1C 56 33 F6 85 C9 74 39 8B 41 28 C1 E8 06 83 E0 0F 83 F8 03 75 17 F6 C3");
            if(query.size()==1) volumeQuery=reinterpret_cast<VolumeQuery>(query.get_first());
        }
        if(roomScale)
        {
            auto position=hook::pattern("56 8B 71 20 85 F6 74 25 8B 54 24 08 8B 02 F3 0F 10 42 04 F3 0F 10 4A 08 89 46 30");
            auto sync=hook::pattern("56 8B F1 83 7E 38 00 0F 84 ? ? ? ? 8B 46 20 8B 16 53 57 83 C0 30 50 FF 92 0C 01 00 00");
            if(position.size()==1 && sync.size()==1 && volumeQuery)
            {
                positionSetter=reinterpret_cast<PositionSetter>(position.get_first());
                physicsSync=reinterpret_cast<PhysicsSync>(sync.get_first());
            }
        }
        auto lookup=hook::pattern("8B 41 38 85 C0 74 38 0F B7 40 08 B9 FF FF 00 00 66 3B C1 74 2A 8B C8 A1 ? ? ? ? 8B 50 04");
        auto velocity=hook::pattern("8B 54 24 04 8B 02 89 81 10 01 00 00 8B 42 04 89 81 14 01 00 00");
        auto moveBlend=hook::pattern("55 8B EC 83 E4 F0 83 EC 48 56 8B F1 57 8B 46 24 0F 57 C9 8B 88 B0 0A 00 00 F3 0F 10 98 90 0A 00 00");
        if((probeAnchor || VRDiagnostics::observationHooks) && moveBlend.size()==1)
            moveBlendOutput=safetyhook::create_inline(moveBlend.get_first(),ObserveMoveBlend);
        if((probeAnchor || VRDiagnostics::observationHooks) && lookup.size()==1 && velocity.size()==1)
        {
            colliderLookup=reinterpret_cast<ColliderLookup>(lookup.get_first());
            colliderVelocity=safetyhook::create_inline(velocity.get_first(),ObserveVelocity);
        }
        if(colliderVelocity || moveBlendOutput) FusionFix::onGameProcessEvent() += []()
        {
            void* ped=!motionProbeComplete.load() && FindPlayerPed ? FindPlayerPed(0) : nullptr;
            localPed=ped;
            localCollider=ped && colliderLookup ? colliderLookup(ped) : nullptr;
        };
        if(roomScale && positionSetter && physicsSync && volumeQuery)
            FusionFix::onGameProcessEvent() += ProcessRoomBody;
    }
    if (!ini.ReadInteger("DEVELOPMENT","ProbeEyeViewport",0) &&
        !ini.ReadInteger("VR","ExperimentalStereo",0)) return;
    auto camera=hook::pattern("55 8B EC 83 E4 F0 83 EC 48 56 8B 75 08 57 8B 06 8B F9 56");
    auto projection=hook::pattern("56 FF 74 24 08 8B F1 8D 8E C0 01 00 00 C6 86 F0 02 00 00 01 E8");
    // Optional VR hooks fail closed on an unsupported executable.
    if (camera.size()!=1 || projection.size()!=1) return;
    setCamera=reinterpret_cast<Setter>(camera.get(0).get<void>());
    setProjection=reinterpret_cast<Setter>(projection.get(0).get<void>());
    FusionFix::onBeforeReset() += [](){VRCamera::Recenter();};
}

bool VRCamera::IsFirstPersonActive() {return firstPersonActive;}

bool VRCamera::GetVehicleHudAnchor(VRMath::Matrix& anchor)
{
    if(!vehicleHudValid)return false;
    anchor=vehicleHudAnchor;return true;
}

bool VRCamera::GenerateOpticalView(const rage::grcViewport& base,const VRMath::Matrix& camera,float verticalDegrees,rage::grcViewport& output)
{
    if(!setCamera || !setProjection || !base.mIsPerspective || base.mInvertZInProjectionMatrix ||
        !std::isfinite(verticalDegrees) || verticalDegrees<1 || verticalDegrees>60) return false;
    const float half=verticalDegrees*.00872664626f;
    const float aspect=base.mAspect;
    if(!std::isfinite(aspect) || aspect<=0) return false;
    const float horizontal=std::atan(std::tan(half)*aspect);
    const float fov[]{-horizontal,horizontal,half,-half};
    alignas(16) auto pose=camera;
    alignas(16) auto projection=VRMath::Projection(fov,.05f,base.mFarClip);
    alignas(16) rage::grcViewport generated=base;
    generated.mNearClip=.05f;generated.mFov=verticalDegrees;
    generated.field_2C8=std::tan(half);generated.field_2CC=std::tan(half)*aspect;
    setCamera(&generated,pose.data());setProjection(&generated,projection.data());
    std::memcpy(&output,&generated,sizeof(generated));return true;
}

bool VRCamera::Generate(const rage::grcViewport& base, const OpenXRVR::EyeView (&eyes)[2],
    rage::grcViewport (&output)[2])
{
    vehicleHudValid=false;
    if (!setCamera || !setProjection || !base.mIsPerspective || base.mInvertZInProjectionMatrix)
        {InvalidateHeading(); StopRoomMotion(); return false;}
    if(restartAnchorProbe.exchange(false))
    {
        anchorFrames=anchorSamples=0;
        AcquireSRWLockExclusive(&motionLock);
        velocityCalls=moveBlendCalls=0;
        moveBlendCaller=0;
        lastMoveDt=0;
        std::memset(lastVelocity,0,sizeof(lastVelocity));
        std::memset(lastMoveBlend,0,sizeof(lastMoveBlend));
        std::memset(lastMoveTarget,0,sizeof(lastMoveTarget));
        ReleaseSRWLockExclusive(&motionLock);
        motionProbeComplete=false;
    }
    ProbeAnchor(base);
    try
    {
        VRMath::Vector anchor{};
        float heading=0,measuredHeight=0;
        float groundZ=NAN;
        Vehicle vehicle=0; bool ducking=false,airborne=false;
        bool animated=false,ragdoll=false,exiting=false;
        VRMath::Matrix vehicleBasis{};
        const bool useFirstPerson=firstPerson && visibilityAvailable && !scriptCameraActive &&
            GameplayAnchor(anchor,heading,measuredHeight,groundZ,vehicle,ducking,airborne,vehicleBasis,animated,ragdoll,exiting);
        vehicleHudValid=useFirstPerson && vehicle && seatAnchorValid && seatIsCar && !exiting;
        if(vehicleHudValid){vehicleHudAnchor=vehicleBasis;vehicleHudAnchor[3]=anchor;}
        auto bodyAnchor=anchor;
        if(useFirstPerson!=firstPersonActive || vehicle!=anchorVehicle) recenter=true;
        anchorVehicle=vehicle;
        vehicleView=useFirstPerson && vehicle!=0;
        OpenXRVR::SetDriving(vehicleView);
        crouchedView=useFirstPerson && ducking;
        airborneView=useFirstPerson && airborne;
        animatedView=useFirstPerson && animated;
        ragdollView=useFirstPerson && ragdoll;
        vehicleExitView=useFirstPerson && exiting;
        firstPersonActive=useFirstPerson;
        if(!useFirstPerson) {InvalidateHeading(); StopRoomMotion();}
        VRMath::Matrix gameCamera{};
        std::memcpy(gameCamera.data(),base.mViewInverseMatrix,sizeof(gameCamera));
        if (recenter.exchange(false))
        {
            float center[3]{};
            for(unsigned i=0;i<3;++i) center[i]=(eyes[0].position[i]+eyes[1].position[i])*.5f;
            // Initial prototype uses the first eye orientation as the center
            // orientation. Both sampled SteamVR views had the same orientation.
            referenceInverse=VRMath::InverseRigid(VRMath::LevelTrackingReference(
                VRMath::Pose(center,eyes[0].orientation),useFirstPerson && !vehicle &&
                eyes[0].floorRelative && eyes[1].floorRelative));
            AcquireSRWLockExclusive(&roomLock);
            roomMotion.Reset(); roomSampleValid=false; roomOwner=nullptr;
            processedRoomPosition={}; lastRoomPosition={};
            roomAchieved={}; bodySampleValid=false; roomDrivingAt=0;
            ReleaseSRWLockExclusive(&roomLock);
            if(useFirstPerson)
            {
                trackingBasis=VRMath::UprightGameCamera(heading,{0,0,0,1});
                if(!vehicle && !animated && !ducking && !airborne && measuredHeight>.1f && measuredHeight<2.f)
                    headHeight=measuredHeight;
                const float rootClearance=anchor[2]-groundZ;
                if(!animated && std::isfinite(rootClearance) && rootClearance>.1f && rootClearance<1.5f)
                    bodyRootAboveFloor=rootClearance;
            }
        }
        if(useFirstPerson)
        {
            if(vehicle) trackingBasis=vehicleBasis;
            gameCamera=trackingBasis;
            // Initial eye placement: measured head height with small eye/face
            // offsets. Calibration, mesh clipping and crouch behavior remain #014.
            const bool floorRelative=!vehicle && eyes[0].floorRelative && eyes[1].floorRelative;
            // Track the body's vertical travel (stairs/jumps), with a floor
            // offset measured independently of the startup headset height.
            if(!vehicle)
            {
                if(animated)
                    anchor[2]+=floorRelative ? -headHeight-bodyRootAboveFloor : .08f;
                else anchor[2]=floorRelative ? anchor[2]-bodyRootAboveFloor : anchor[2]+headHeight+.08f;
                AcquireSRWLockShared(&roomLock);
                const float physicalDrop=floorRelative && physicalCrouchEnabled ?
                    physicalCrouch.reference-(eyes[0].position[1]+eyes[1].position[1])*.5f : 0;
                ReleaseSRWLockShared(&roomLock);
                if(!animated && ducking)
                {
                    const float rootDrop=std::isfinite(groundZ) ? bodyRootAboveFloor-(bodyAnchor[2]-groundZ) : 0;
                    anchor[2]+=VRMath::CrouchAnchorAdjustment(headHeight,measuredHeight,rootDrop,physicalDrop,
                        physicalCrouchOwnsDuck.load());
                }
                else if(!animated) anchor[2]+=VRMath::JumpCameraRise(headHeight,measuredHeight);
            }
            AcquireSRWLockExclusive(&roomLock);
            gameGroundZ=groundZ;
            physicalEyeHeight=floorRelative ? (eyes[0].position[1]+eyes[1].position[1])*.5f : NAN;
            ReleaseSRWLockExclusive(&roomLock);
            for(unsigned axis=0;axis<3;++axis)
                gameCamera[3][axis]=anchor[axis]-.10f*trackingBasis[2][axis];
            if(animated && roomScale)
            {
                AcquireSRWLockShared(&roomLock);
                for(unsigned axis=0;axis<2;++axis) gameCamera[3][axis]-=processedRoomPosition[axis];
                ReleaseSRWLockShared(&roomLock);
            }
        }
        if(vehicle || animated || !useFirstPerson) StopRoomMotion();
        if(roomScale && useFirstPerson && !vehicle && !animated && positionSetter && physicsSync && volumeQuery)
        {
            float midpoint[3]{};
            for(unsigned axis=0;axis<3;++axis) midpoint[axis]=(eyes[0].position[axis]+eyes[1].position[axis])*.5f;
            const auto relative=VRMath::Multiply(VRMath::Multiply(
                VRMath::Pose(midpoint,eyes[0].orientation),referenceInverse),trackingBasis);
            const bool valid=OpenXRVR::HasFocusedViews() &&
                NativeInvoke::GetNativeHandler(std::to_underlying(Natives::NativeHashes::IS_PAUSE_MENU_ACTIVE)) &&
                !Natives::IsPauseMenuActive();
            void* owner=valid && FindPlayerPed ? FindPlayerPed(0) : nullptr;
            const auto now=GetTickCount64();
            AcquireSRWLockExclusive(&roomLock);
            if(!owner || !roomSampleValid || owner!=roomOwner || now-roomSampledAt>100 ||
                std::hypot(relative[3][0]-lastRoomPosition[0],relative[3][1]-lastRoomPosition[1])>.3f)
                processedRoomPosition=relative[3];
            roomOwner=owner; roomSampleValid=owner!=nullptr; roomSampledAt=now;
            lastRoomPosition=relative[3];
            // Absolute consumed tracking sample, rather than accumulated
            // movement attribution. The fresh unconsumed head delta remains
            // immediate until the next simulation resolves it against geometry.
            for(unsigned axis=0;axis<2;++axis)
                gameCamera[3][axis]=bodyAnchor[axis]-processedRoomPosition[axis];
            ReleaseSRWLockExclusive(&roomLock);
        }
        for(const auto& row:gameCamera)
            for(float value:row)
                if(!std::isfinite(value)) throw std::invalid_argument("Invalid game camera");
        VRHands::PublishTrackingTransform(VRMath::Multiply(referenceInverse,gameCamera),
            FindPlayerPed ? FindPlayerPed(0) : nullptr,useFirstPerson && !animated && !exiting && !ragdoll &&
            (!vehicle || seatIsCar) && OpenXRVR::HasFocusedViews(),vehicle!=0);
        // Eye translation comes only from the native anchor and OpenXR pose.
        // VRHands applies configured head/body placement to the visible rig;
        // head yaw must never rotate a positional offset around these eyes.
        for(unsigned eye=0;eye<2;++eye)
        {
            alignas(16) VRMath::Matrix pose=VRMath::Multiply(VRMath::Multiply(
                VRMath::Pose(eyes[eye].position,eyes[eye].orientation),referenceInverse),gameCamera);
            alignas(16) VRMath::Matrix projection=VRMath::Projection(eyes[eye].fov,base.mNearClip,base.mFarClip);
            // Engine matrix helpers use aligned SSE loads/stores. The mirrored
            // viewport type has only four-byte alignment, so never pass an
            // arbitrary caller-owned copy to the engine setters.
            alignas(16) rage::grcViewport generated=base;
            const float vertical=std::tan(eyes[eye].fov[2])-std::tan(eyes[eye].fov[3]);
            const float horizontal=std::tan(eyes[eye].fov[1])-std::tan(eyes[eye].fov[0]);
            generated.mFov=(eyes[eye].fov[2]-eyes[eye].fov[3])*180.f/3.14159265358979323846f;
            generated.mAspect=horizontal/vertical;
            generated.field_2C8=vertical*.5f;
            generated.field_2CC=horizontal*.5f;
            // Verified engine setters update camera/view matrices, world-VP,
            // screen transform product and all six planes on this local copy.
            setCamera(&generated,pose.data());
            setProjection(&generated,projection.data());
            std::memcpy(&output[eye],&generated,sizeof(generated));
        }
        if(useFirstPerson && !vehicle && !animated && ((headRelativeMovement && movementHeading) || headsetAim))
        {
            VRMath::Matrix headCamera{};
            std::memcpy(headCamera.data(),output[0].mViewInverseMatrix,sizeof(headCamera));
            float angle=0;
            const bool level=VRMath::LevelHeading(headCamera,angle);
            void* owner=FindPlayerPed ? FindPlayerPed(0) : nullptr;
            AcquireSRWLockExclusive(&headingLock);
            if(level) {headHeading=angle; headingValid=true;}
            headingOwner=owner;
            headingSampledAt=GetTickCount64();
            ReleaseSRWLockExclusive(&headingLock);
            AcquireSRWLockExclusive(&aimLock);
            if(headsetAim && VRMath::AimAngles(headCamera,aimPitch,aimYaw))
            {
                aimCamera=headCamera;
                for(unsigned axis=0;axis<3;++axis)
                    aimCamera[3][axis]=(output[0].mViewInverseMatrix[3][axis]+output[1].mViewInverseMatrix[3][axis])*.5f;
                aimOwner=owner;
                aimValid=true;
                aimSampledAt=GetTickCount64();
            }
            else aimValid=false;
            ReleaseSRWLockExclusive(&aimLock);
        }
        else InvalidateHeading();
        if(VRDiagnostics::counters)
        {
            AcquireSRWLockExclusive(&roomLock);
            diagnosticBody=bodyAnchor;
            for(unsigned axis=0;axis<3;++axis)
                diagnosticView[axis]=(output[0].mViewInverseMatrix[3][axis]+output[1].mViewInverseMatrix[3][axis])*.5f;
            ReleaseSRWLockExclusive(&roomLock);
        }
        VRMath::Matrix listener=VRMath::Identity();
        for(unsigned axis=0;axis<3;++axis)
        {
            listener[0][axis]=output[0].mViewInverseMatrix[0][axis];
            listener[1][axis]=-output[0].mViewInverseMatrix[2][axis];
            listener[2][axis]=output[0].mViewInverseMatrix[1][axis];
            listener[3][axis]=(output[0].mViewInverseMatrix[3][axis]+output[1].mViewInverseMatrix[3][axis])*.5f;
        }
        AcquireSRWLockExclusive(&audioLock);
        audioHead=listener;audioHeadAt=GetTickCount64();
        ReleaseSRWLockExclusive(&audioLock);
        return true;
    }
    catch(const std::exception&)
    {
        recenter=true;
        InvalidateHeading();
        StopRoomMotion();
        return false;
    }
}
