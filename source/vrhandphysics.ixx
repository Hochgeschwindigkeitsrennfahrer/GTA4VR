module;
#include <common.hxx>
#include "includes/vrmath.hpp"
#include "includes/vrgrip.hpp"
#include "includes/vrpunch.hpp"
#include "includes/vrmelee.hpp"
#include <atomic>
#include <cstdio>
#include <cstring>
export module vrhandphysics;
import common;
import comvars;
import natives;
import openxrvr;

export namespace VRHandPhysics
{
    // Native rigid spheres cover wrist, palm, fingertips and forearm.
    constexpr unsigned ContactNodeCount=10,NodeCount=11;
    void Install();
    void Publish(void* owner,void* held,unsigned hand,const VRMath::Vector* points,const VRMath::Matrix& wrist,bool supporting);
    void PublishBlade(void* owner,void* held,const VRMath::Vector* world,const VRMath::Vector* tracked,uint64_t sampledAt,const VRMelee::Profile& profile);
    bool IsGrabbing(unsigned hand);
    class WeaponQueryScope
    {
        void* previousOwner;
        void* previousWeapon;
    public:
        WeaponQueryScope(void* owner,void* held);
        ~WeaponQueryScope();
        WeaponQueryScope(const WeaponQueryScope&)=delete;
        WeaponQueryScope& operator=(const WeaponQueryScope&)=delete;
    };
    bool Resolve(void* owner,unsigned hand,VRMath::Matrix& wrist);
    std::string Status();
    std::string CombatStatus();
}

namespace
{
    SafetyHookInline pairHook{},queryHook{},rayHook{},overlapHook{},ragdollDeactivateHook{},ragdollSettleHook{};
    thread_local void* shotOwner=nullptr;
    thread_local void* shotWeapon=nullptr;
    std::atomic<unsigned> excludedShotQueries=0;
    thread_local bool selectingGrab=false;
    thread_local void* punchOwner=nullptr;
    thread_local void* strikeWeapon=nullptr;
    thread_local std::vector<void*> grabCandidates;
    bool enabled=false;
    std::atomic<bool> failed=false;
    uint32_t model=0;
    bool requestedModel=false;
    uint64_t requestedAt=0;
    SRWLOCK poseLock=SRWLOCK_INIT;
    struct Pose {void* owner=nullptr; void* held=nullptr; uint64_t at[2]{}; VRMath::Vector points[2][VRHandPhysics::NodeCount]{}; VRMath::Matrix wrists[2]{}; bool supporting[2]{};
        void* bladeEntity=nullptr;uint64_t bladeAt=0,bladeSample=0;VRMelee::Points bladeWorld{},bladeTracked{};VRMelee::Profile meleeProfile{};} desired;
    struct Feedback {void* owner=nullptr; uint64_t at[2]{}; VRMath::Matrix wrists[2]{};bool rotation[2]{};} feedback;
    struct Node {Object handle=0; void* instance=nullptr; VRMath::Vector target{}; uint64_t at=0;} nodes[2][VRHandPhysics::NodeCount];
    std::atomic<void*> instances[2][VRHandPhysics::NodeCount]{};
    std::atomic<void*> player=nullptr,weapon=nullptr;
    std::atomic<unsigned> created=0,removed=0,active=0,updates=0,excludedPairs=0,recoveries=0,limitedDrives=0;
    std::atomic<unsigned> excludedQueries=0;
    float armForce=250.f,grabForce=900.f,recoveryDistance=.75f,acquisitionRadius=.22f;
    constexpr float NodeMass=.15f,Stiffness=400.f,Damping=12.f;
    std::atomic<float> maxError=0;
    void* currentOwner=nullptr;
    VRMath::Vector lastTarget[2]{};
    bool haveTarget[2]{};
    bool grabbingEnabled=false;
    using VolumeQuery=bool (__cdecl*)(const float*,const float*,float,void*,void*,uint32_t,uint32_t,int,int);
    VolumeQuery grabQuery=nullptr;
    // CE B60C80, also called by CTaskSimpleMeleeActionResult at CD4725.
    using PedDamage=float (__cdecl*)(void*,void*,uint32_t,float,const float*,void*,void*,float,bool);
    PedDamage meleeDamage=nullptr;
    using WeaponCtor=void* (__thiscall*)(void*);
    using WeaponSetType=void (__thiscall*)(void*,uint32_t,uint16_t,bool);
    using WeaponImpact=void (__thiscall*)(void*,void*,const float*,void*,float,bool);
    WeaponCtor weaponCtor=nullptr;
    WeaponSetType weaponSetType=nullptr;
    void (__thiscall* weaponDtor)(void*)=nullptr;
    WeaponImpact weaponImpact=nullptr;
    // CE B5DF50: seven stack arguments, thiscall RET 1C, float in ST0.
    using WeaponDispatch=float (__thiscall*)(void*,void*,const float*,void*,int,float,uint32_t,const float*);
    WeaponDispatch weaponDispatch=nullptr;
    using MeleeEventCtor=void* (__thiscall*)(void*,void*,void*,bool);
    using EventDtor=void (__thiscall*)(void*);
    using GlobalEventGroup=void* (__cdecl*)();
    using AddEvent=void* (__thiscall*)(void*,void*,bool,bool);
    MeleeEventCtor meleeEventCtor=nullptr;
    EventDtor meleeEventDtor=nullptr;
    GlobalEventGroup globalEventGroup=nullptr;
    AddEvent addEvent=nullptr;
    bool combatEnabled=false;
    float punchSpeed=1.6f,punchDamage=25.f;
    VRPunch::Strike strikes[2]{};
    std::atomic<bool> fists[2]{};
    std::atomic<float> fistSpeed[2]{},lastPunchDamage=0;
    std::atomic<unsigned> punchSweeps=0,punchContacts=0,punchHits[2]{};
    std::atomic<unsigned> lastPunchTarget=0;
    std::atomic<unsigned> meleeEvents=0;
    std::atomic<unsigned> meleeImpacts=0;
    std::atomic<unsigned> worldMeleeCalls=0,vehicleMeleeCalls=0,objectMeleeCalls=0;
    std::atomic<float> lastWorldMeleeDamage=0;
    VRMelee::Strike bladeStrike{};
    void* bladeEntity=nullptr;
    float knifeSpeed=1.2f,knifeDamage=25.f;
    float clubSpeed=1.2f,clubDamage=30.f;
    std::atomic<unsigned> meleeType=0,meleeSamples=0;
    std::atomic<float> meleeRadius=0;
    std::atomic<float> bladeSpeed=0;
    std::atomic<bool> bladeReady=false;
    std::atomic<unsigned> bladeSweeps=0,bladeContacts=0,bladeHits=0;
    std::atomic<unsigned> bladeMisses=0,bladeRearmBlocks=0,bladeCooldownBlocks=0,bladeTrackingResets=0;
    std::atomic<unsigned> bladeNativeRejects=0,bladeNonLivingContacts=0;
    using RagdollActivity=void (__thiscall*)(void*,int,bool);
    RagdollActivity ragdollActivity=nullptr;
    using RagdollPermission=bool (__cdecl*)(void*,int,void*,float);
    using RagdollActivate=void (__thiscall*)(void*,bool,bool);
    RagdollPermission ragdollPermission=nullptr;
    RagdollActivate ragdollActivate=nullptr;
    struct Grab
    {
        Object handle=0;void* instance=nullptr;float mass=0;
        VRMath::Matrix offset{},wristInBody{},previousBody{},previousTarget{};
        VRMath::Vector inertia{};float radius=.1f;uint64_t at=0;
        VRGrip::ThrowMotion motion{};
    } grabs[2];
    std::atomic<float> grabPositionError=0,grabAngleError=0,grabAngularSpeed=0,grabTorque=0;
    std::atomic<unsigned> twoHandUpdates=0,grabUnsafeReleases=0;
    std::atomic<unsigned> throws=0;
    std::atomic<float> throwSpeed=0;
    std::atomic<bool> grabbing[2]{};
    std::atomic<void*> grabInstances[2]{},grabEntities[2]{};
    std::atomic<unsigned> grabAttempts=0,grabStarts=0,grabReleases=0,grabRejected=0;
    std::atomic<unsigned> gripSamples[2]{},grabHits=0;
    std::atomic<float> grabMass=0,grabRadius=0,grabInertia=0;
    bool gripArmed[2]{};
    uint64_t nextGrabQuery[2]{};
    using DoorRequest=void (__thiscall*)(void*,float,uint32_t);
    DoorRequest requestDoor=nullptr;
    struct DoorGrab
    {
        Vehicle handle=0;void* entity=nullptr;void* root=nullptr;void* record=nullptr;
        unsigned index=0;bool automaticLatch=false,controlled=false;VRMath::Matrix wristInDoor{};VRMath::Vector palmInDoor{};
    } doorGrabs[2];
    struct DoorState
    {
        void* entity=nullptr;void* root=nullptr;void* child=nullptr;char* record=nullptr;
        VRMath::Matrix frame{};VRMath::Vector axis{};float fraction=0,span=0;
        unsigned firstPart=0,partCount=0,hitPart=~0u;
    };
    struct RagdollGrab
    {
        Ped handle=0;void* entity=nullptr;void* root=nullptr;unsigned part=0;
        VRMath::Matrix wristInPart{};VRMath::Vector anchor{},previous{},previousTarget{};uint64_t at=0;
    } ragdollGrabs[2];
    std::atomic<bool> ragdollGrabbing[2]{};
    std::atomic<void*> ragdollEntities[2]{};
    std::atomic<unsigned> ragdollStarts=0,ragdollUpdates=0,ragdollCandidates=0,ragdollHits=0,ragdollRejected=0;
    std::atomic<int> ragdollPart=-1;
    std::atomic<float> ragdollMass=0,ragdollError=0;
    std::atomic<unsigned> ragdollWakeAttempts=0,ragdollWakeSuccesses=0;
    std::atomic<unsigned> ragdollLostPhysics=0,ragdollLostFrame=0,grabOverlapRejections=0;
    std::atomic<unsigned> ragdollKeepAlive=0;
    std::atomic<unsigned> ragdollSettleDeferrals=0;
    std::atomic<unsigned> ragdollDeactivations=0,ragdollDeactivateCaller=0,ragdollDeactivateFlags=0,ragdollDeactivateState=0;
    Ped lastWakePed[2]{};uint64_t lastWakeAt[2]{};
    std::atomic<bool> doorGrabbing[2]{};
    std::atomic<void*> doorInstances[2]{};
    std::atomic<unsigned> doorStarts=0,doorUpdates=0,doorLocked=0,doorMisses=0;
    std::atomic<float> doorFraction=0,doorTarget=0;
    std::atomic<uint32_t> doorFlagsBefore=0,doorFlagsAfter=0,doorLockState=~0u;
    std::atomic<bool> doorHasPhysicsChild=false;
    std::atomic<unsigned> doorLatchClearFailures=0;
    std::atomic<int> selectedDoorIndex=-1,selectedDoorRecordId=-1;
    std::atomic<unsigned> selectedDoorCollisionPart=~0u;
    std::atomic<unsigned> grabProxyQueryRejections=0,doorQueryNoHit=0,doorQueryWrongPart=0;
    std::atomic<unsigned> lastDoorQueryPart=~0u,handSearchHits[3]{};
    std::atomic<unsigned> blockedContactNodes=0;
    std::atomic<float> palmColliderError=0,contactDrivePeak=0;

    void* Entity(Object handle,bool vehicle=false)
    {
        auto* pool=vehicle ? CVehicle::GetVehiclePool():CObject::GetObjectsPool();
        const int slot=handle>>8;
        if(!pool || !handle || slot<0 || slot>=pool->GetSize() || !pool->m_aStorage || !pool->m_aFlags) return nullptr;
        if(pool->m_nStorageSize<4 || pool->m_nStorageSize>16384) return nullptr;
        auto* storage=pool->GetAt(handle);
        return storage && pool->m_nStorageSize==4 ? *reinterpret_cast<void**>(storage):storage;
    }

    void* PedEntity(Ped handle)
    {
        auto* pool=CPed::GetPedPool();const int slot=handle>>8;
        if(!pool || !handle || slot<0 || slot>=pool->GetSize() || !pool->m_aStorage || !pool->m_aFlags ||
            pool->m_nStorageSize<4 || pool->m_nStorageSize>16384) return nullptr;
        auto* storage=pool->GetAt(handle);
        return storage && pool->m_nStorageSize==4 ? *reinterpret_cast<void**>(storage):storage;
    }

    bool IsHeld(void* instance)
    {
        if(!instance) return false;
        auto* entity=*reinterpret_cast<void**>(static_cast<char*>(instance)+0xC);
        for(unsigned h=0;h<2;++h)
            if((grabbing[h] && grabInstances[h]==instance) ||
                (ragdollGrabbing[h] && entity && ragdollEntities[h]==entity)) return true;
        return false;
    }

    bool IsProxy(void* instance)
    {
        if(!instance || !active.load(std::memory_order_relaxed)) return false;
        for(auto& hand:instances) for(auto& value:hand)
            if(value.load(std::memory_order_relaxed)==instance) return true;
        return false;
    }

    bool __fastcall CanCollide(void* self,void*,void* other)
    {
        // Called by the native physics workers: no natives, locks or pool walks.
        if(IsHeld(self) || IsHeld(other))
        {
            auto* counterpart=IsHeld(self) ? other:self;
            auto* entity=counterpart ? *reinterpret_cast<void**>(static_cast<char*>(counterpart)+0xC):nullptr;
            if(entity && entity==player.load(std::memory_order_relaxed))
            {++excludedPairs;return false;}
        }
        const bool selfProxy=IsProxy(self),otherProxy=IsProxy(other);
        if(selfProxy || otherProxy)
        {
            auto* counterpart=selfProxy ? other:self;
            for(auto& value:doorInstances) if(counterpart && counterpart==value.load(std::memory_order_relaxed))
            {++excludedPairs;return false;}
            auto* entity=counterpart ? *reinterpret_cast<void**>(static_cast<char*>(counterpart)+0xC):nullptr;
            const bool ped=entity && ((*reinterpret_cast<uint32_t*>(static_cast<char*>(entity)+0x28)>>6)&15)==3;
            // All owned shapes ignore a held entity, including vehicle child
            // instances carrying the same owner. Publish before attaching.
            for(unsigned hand=0;hand<2;++hand)
                if(counterpart==grabInstances[hand].load(std::memory_order_relaxed) ||
                    (entity && entity==grabEntities[hand].load(std::memory_order_relaxed)))
                {++excludedPairs;return false;}
            if((selfProxy && otherProxy) || entity==player.load() || (entity && entity==weapon.load()) || ped)
            {++excludedPairs; return false;}
        }
        return pairHook.thiscall<bool>(self,other);
    }

    bool __fastcall QueryBound(void* query,void*,void* bound,void* instance)
    {
        // CE 73B7D0 is the scene-query narrowphase, not the contact solver.
        // Invisible owned limb objects must not become obstacles for room-scale
        // sweeps or native pedestrian/vehicle-entry placement queries. Reject
        // the candidate BEFORE writing any hit, so the query still finds walls
        // and props behind it. Physical prop contacts retain CanCollide above.
        if(IsProxy(instance)) {++excludedQueries;if(selectingGrab) ++grabProxyQueryRejections;return false;}
        // The all-category punch sweep can encounter the owner's alternate
        // ped collision root as well as its primary root. Ignore both before
        // a closest hit is written; keep other NPCs and world blockers.
        if(punchOwner && instance && *reinterpret_cast<void**>(static_cast<char*>(instance)+0xC)==punchOwner) return false;
        if(strikeWeapon && instance && *reinterpret_cast<void**>(static_cast<char*>(instance)+0xC)==strikeWeapon) return false;
        if(selectingGrab && std::find(grabCandidates.begin(),grabCandidates.end(),instance)==grabCandidates.end()) return false;
        return queryHook.thiscall<bool>(query,bound,instance);
    }

    bool __fastcall RayBound(void* query,void*,void* bound,void* instance)
    {
        // B60430 -> A545A0 -> 738F10 -> 73A370. Reject before hit writes,
        // including compound recursion; genuine scenery/NPCs remain eligible.
        if(shotOwner && instance)
        {
            auto* bytes=static_cast<unsigned char*>(instance);
            void* entity=(bytes[0xA]&0x40) ? *reinterpret_cast<void**>(bytes+0xC):nullptr;
            if(IsProxy(instance) || entity==shotOwner || (entity && entity==shotWeapon))
            {++excludedShotQueries;return false;}
        }
        return rayHook.thiscall<bool>(query,bound,instance);
    }

    bool __fastcall GrabOverlapBound(void* query,void*,void* bound,void* instance)
    {
        // CE73BFD0 is the stationary sphere variant (73B7D0 handles sweeps).
        // Filter before it writes a hit, including recursive compound parts.
        // Outside our grab search, preserve the native query unchanged.
        if(selectingGrab && (IsProxy(instance) ||
            std::find(grabCandidates.begin(),grabCandidates.end(),instance)==grabCandidates.end()))
        {++grabOverlapRejections;return false;}
        return overlapHook.thiscall<bool>(query,bound,instance);
    }

    void __fastcall TraceRagdollDeactivate(void* entity,void*,bool a,bool b,bool c)
    {
        // Read-only trace of native9F2CA0 for our held NPC only. Never suppress
        // deactivation, call natives, or interfere with teardown in this hook.
        bool held=false;
        for(unsigned hand=0;hand<2;++hand)
            held|=ragdollGrabbing[hand].load(std::memory_order_relaxed) &&
                ragdollEntities[hand].load(std::memory_order_relaxed)==entity;
        if(held)
        {
            ++ragdollDeactivations;
            ragdollDeactivateCaller=unsigned(reinterpret_cast<uintptr_t>(_ReturnAddress())-
                reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr))+0x400000);
            ragdollDeactivateFlags=unsigned(a)|(unsigned(b)<<1)|(unsigned(c)<<2);
            ragdollDeactivateState=*reinterpret_cast<unsigned*>(static_cast<char*>(entity)+0x7B8);
        }
        ragdollDeactivateHook.thiscall<void>(entity,a,b,c);
    }

    void __fastcall SettleRagdoll(void* task,void*,void* entity,bool alternate)
    {
        // CE CCD3B3 was observed deactivating every held corpse. Defer the
        // enclosing CCCE10 settle operation BEFORE it rebuilds the pose or
        // swaps collision roots. Never suppress generic physics teardown.
        bool held=false;
        for(unsigned hand=0;hand<2;++hand)
            held|=ragdollGrabbing[hand].load(std::memory_order_relaxed) &&
                ragdollEntities[hand].load(std::memory_order_relaxed)==entity;
        if(held && !alternate && entity)
        {
            auto* ped=static_cast<char*>(entity);
            const auto deathState=*reinterpret_cast<int*>(ped+0xA74);
            const bool dead=ped[0x210]!=0 && (deathState==1 || deathState==2);
            if(dead && *reinterpret_cast<void**>(ped+0x224) &&
                !(static_cast<unsigned char>(ped[0x24])&0x1C) &&
                *reinterpret_cast<int*>(ped+0x7B8)==6 &&
                *reinterpret_cast<void**>(ped+0x38)==*reinterpret_cast<void**>(ped+0x7B4))
            {++ragdollSettleDeferrals;return;}
        }
        ragdollSettleHook.thiscall<void>(task,entity,alternate);
    }

    void ReleaseGrab(unsigned hand);

    void ClearHand(unsigned hand)
    {
        // Release the non-owning grasp before deleting our contact shapes.
        ReleaseGrab(hand);
        grabs[hand]={};grabbing[hand]=false;grabInstances[hand]=nullptr;grabEntities[hand]=nullptr;gripArmed[hand]=false;
        for(unsigned i=0;i<VRHandPhysics::NodeCount;++i)
        {
            auto& node=nodes[hand][i];
            if(instances[hand][i].exchange(nullptr)) --active;
            auto* entity=Entity(node.handle);
            if(entity && *reinterpret_cast<void**>(static_cast<char*>(entity)+0x38)==node.instance && Natives::DoesObjectExist(node.handle))
            {Natives::DeleteObject(&node.handle); ++removed;}
            node={};
        }
        haveTarget[hand]=false;
        strikes[hand]={};fists[hand]=false;fistSpeed[hand]=0;
        AcquireSRWLockExclusive(&poseLock); feedback.at[hand]=0; ReleaseSRWLockExclusive(&poseLock);
    }

    void Clear()
    {
        bladeStrike={};bladeEntity=nullptr;bladeReady=false;bladeSpeed=0;meleeType=0;meleeSamples=0;meleeRadius=0;
        ClearHand(0); ClearHand(1);
        active=0; currentOwner=nullptr; player=nullptr; weapon=nullptr;
        haveTarget[0]=haveTarget[1]=false;
        AcquireSRWLockExclusive(&poseLock); feedback={}; ReleaseSRWLockExclusive(&poseLock);
        if(requestedModel) {Natives::MarkModelAsNoLongerNeeded(model); requestedModel=false;}
    }

    bool Create(unsigned hand,unsigned i,const VRMath::Vector& target)
    {
        auto& node=nodes[hand][i];
        // Use an existing game's spherical collision asset, never edit shared
        // bounds or install an asset. The object is registered normally.
        Natives::CreateObjectNoOffset(model,target[0],target[1],target[2],&node.handle,true);
        if(!node.handle || !Natives::DoesObjectExist(node.handle)) return false;
        auto* entity=Entity(node.handle);
        node.instance=entity ? *reinterpret_cast<void**>(static_cast<char*>(entity)+0x38):nullptr;
        if(!node.instance || !Natives::DoesObjectHavePhysics(node.handle)) return false;
        instances[hand][i]=node.instance;
        ++active;
        Natives::SetObjectVisible(node.handle,false);
        Natives::SetObjectInvincible(node.handle,true);
        Natives::SetObjectCcd(node.handle,true);
        Natives::SetObjectDynamic(node.handle,true);
        // The native wrapper clones the archetype before changing its mass.
        // -1 leaves other properties intact, including its collision bound.
        Natives::SetObjectPhysicsParams(node.handle,i==VRHandPhysics::ContactNodeCount ? 2.f:NodeMass,-1.f,-1.f,-1.f,-1.f,-1.f,-1.f,-1.f,0xbf800000,0xbf800000);
        if(*reinterpret_cast<uint16_t*>(static_cast<char*>(node.instance)+8)==0xffff) return false;
        node.target=target; node.at=GetTickCount64();
        ++created;
        return true;
    }

    bool BodyPose(Object handle,void* instance,VRMath::Matrix& pose)
    {
        auto* entity=Entity(handle);
        if(!entity || *reinterpret_cast<void**>(static_cast<char*>(entity)+0x38)!=instance ||
            !Natives::DoesObjectExist(handle) || *reinterpret_cast<uint16_t*>(static_cast<char*>(instance)+8)==0xffff)
            return false;
        auto* matrix=*reinterpret_cast<const VRMath::Matrix**>(static_cast<char*>(entity)+0x20);
        if(!matrix) return false;
        pose=*matrix;
        return VRGrip::Rigid(pose);
    }

    bool ReadDoor(Vehicle handle,unsigned index,DoorState& state)
    {
        if(!requestDoor || index>=6) return false;
        auto* entity=static_cast<char*>(Entity(handle,true));
        if(!entity || !Natives::DoesVehicleExist(handle)) return false;
        auto* root=*reinterpret_cast<char**>(entity+0xDC4);
        if(!root || root!=*reinterpret_cast<void**>(entity+0x38) ||
            *reinterpret_cast<uint16_t*>(root+8)==0xffff) return false;
        static constexpr int ids[]{5,7,6,8,39,40};
        auto* records=*reinterpret_cast<char**>(entity+0xF88);
        const int count=*reinterpret_cast<int*>(entity+0xF8C);
        if(!records || count<=0 || count>16) return false;
        char* record=nullptr;
        for(int i=0;i<count;++i) if(*reinterpret_cast<int*>(records+i*0x34)==ids[index]) {record=records+i*0x34;break;}
        if(!record) return false;
        const auto flags=*reinterpret_cast<uint32_t*>(record+0x10);
        const unsigned axis=flags&15;
        if(!(flags&0x40000) || (flags&0x80000) || axis>2 || Natives::IsCarDoorDamaged(handle,index)) return false;
        const float open=*reinterpret_cast<float*>(record+8),closed=*reinterpret_cast<float*>(record+12);
        state.fraction=*reinterpret_cast<float*>(record+0x1C);state.span=open-closed;
        if(!std::isfinite(state.fraction+state.span) || std::abs(state.span)<.1f || std::abs(state.span)>3.2f) return false;
        auto* payload=*reinterpret_cast<char**>(root+0x64);
        using Getter=void* (__thiscall*)(void*);
        auto getter=reinterpret_cast<Getter>((*reinterpret_cast<void***>(root))[0xE0/4]);
        auto* skeleton=static_cast<char*>(getter(root));
        if(!payload || !skeleton) return false;
        const int modelIndex=*reinterpret_cast<short*>(entity+0x2E);
        if(modelIndex<0) return false;
        auto* models=reinterpret_cast<void**>(reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr))+0x01295CD8-0x00400000);
        auto* info=static_cast<char*>(models[modelIndex]);
        if(!info) return false;
        auto* map=*reinterpret_cast<int**>(info+0xCC);
        const unsigned boneCount=*reinterpret_cast<uint16_t*>(skeleton+0x10);
        auto* matrices=*reinterpret_cast<VRMath::Matrix**>(skeleton+0x14);
        if(!map || !matrices || !boneCount || boneCount>255) return false;
        const int bone=map[ids[index]];
        if(bone<0 || unsigned(bone)>=boneCount) return false;
        state.frame=matrices[bone];if(!VRGrip::Rigid(state.frame)) return false;
        state.axis=state.frame[axis];
        // fragType groups map the descriptor index to its collision parts.
        // The door record's child, descriptor and skeleton indices differ.
        auto* link=*reinterpret_cast<char**>(root+0x5C);
        char* type=nullptr;
        if(link)
        {
            if((*reinterpret_cast<uint32_t*>(link+8)&0x30000000)!=0x30000000) return false;
            type=*reinterpret_cast<char**>(link);
        }
        else type=*reinterpret_cast<char**>(root+0x6C);
        auto* archetype=*reinterpret_cast<char**>(root+4);
        auto* bound=archetype ? *reinterpret_cast<char**>(archetype+12):nullptr;
        const int descriptor=*reinterpret_cast<signed char*>(record+5),child=*reinterpret_cast<signed char*>(record+4);
        if(!type || !bound || static_cast<unsigned char>(bound[4])!=12 || descriptor<=0 || child<0) return false;
        const unsigned parts=*reinterpret_cast<uint16_t*>(bound+0x92);
        if(!parts || parts>255 || unsigned(descriptor)>=parts || unsigned(child)>=parts) return false;
        auto* descriptors=*reinterpret_cast<char***>(type+0xD4);
        auto* groups=*reinterpret_cast<char***>(type+0xD0);
        if(!descriptors || !groups || !descriptors[descriptor]) return false;
        const unsigned groupIndex=static_cast<unsigned char>(descriptors[descriptor][12]);
        if(groupIndex>=parts || !groups[groupIndex]) return false;
        auto* group=groups[groupIndex];
        const unsigned first=static_cast<unsigned char>(group[0x10E]),n=static_cast<unsigned char>(group[0x10F]);
        auto* children=*reinterpret_cast<void***>(bound+0x80);
        if(!children || !n || first+n>parts) return false;
        bool haveBound=false;for(unsigned i=first;i<first+n;++i) haveBound|=children[i]!=nullptr;
        if(!haveBound) return false;
        state.firstPart=first;state.partCount=n;
        auto* childTable=*reinterpret_cast<void***>(payload+0x1B0);
        state.child=childTable ? childTable[child]:nullptr;
        state.entity=entity;state.root=root;state.record=record;
        return true;
    }

    void ReleaseDoor(unsigned hand)
    {
        if(!doorGrabbing[hand].exchange(false)) return;
        const auto grab=doorGrabs[hand];doorGrabs[hand]={};doorInstances[hand]=nullptr;++grabReleases;
        if(doorGrabbing[1-hand] && doorGrabs[1-hand].record==grab.record)
        {doorGrabs[1-hand].controlled|=grab.controlled;return;}
        if(!grab.controlled) return; // Locked acquisition never changes hinge/latch state.
        DoorState state{};
        if(ReadDoor(grab.handle,grab.index,state) && state.entity==grab.entity && state.root==grab.root && state.record==grab.record)
        {
            auto& flags=*reinterpret_cast<uint32_t*>(state.record+0x10);
            const uint32_t latch=flags&0x100u;
            flags=(flags & ~0x10000u)|(grab.automaticLatch ? 0x10000u:0u);
            requestDoor(state.record,std::clamp(state.fraction,0.f,1.f),0); // Free hinge; restore its automatic latch policy.
            flags|=latch; // A native close/lock during the grab must not be unlatched by release.
        }
    }

    void RequestGrabbedDoor(const DoorState& state,float target)
    {
        // CE C2E810: bit16 auto-latches near closed, bit17 is pending close.
        // Holding the handle cancels both. C2F5A0 mode0x400 (OPEN_CAR_DOOR)
        // clears the closed latch without CONTROL_CAR_DOOR's bit9/17 re-latch.
        // This is door-local state, not the vehicle lock at CVehicle+0x12D0.
        auto& flags=*reinterpret_cast<uint32_t*>(state.record+0x10);
        doorFlagsBefore=flags;doorHasPhysicsChild=state.child!=nullptr;
        flags&=~0x30000u;
        requestDoor(state.record,std::clamp(target,0.f,1.f),0x400);
        doorFlagsAfter=flags;
        if(flags&0x30100u) ++doorLatchClearFailures;
    }

    void ReleaseGrab(unsigned hand)
    {
        ReleaseDoor(hand);
        ragdollGrabbing[hand]=false;ragdollEntities[hand]=nullptr;ragdollGrabs[hand]={};
        if(!grabbing[hand].exchange(false)) return;
        // Safety/lifecycle releases retain solver momentum. Intentional final
        // releases may transfer recent tracked velocity in ReleaseThrows.
        grabs[hand]={};grabInstances[hand]=nullptr;grabEntities[hand]=nullptr;++grabReleases;
    }

    void ReleaseThrows(const Pose& pose,const OpenXRVR::ControllerInput& input,uint64_t now)
    {
        for(unsigned hand=0;hand<2;++hand)
        {
            if(!grabbing[hand] || !input.grabActive[hand] || input.grab[hand]) continue;
            const auto& grab=grabs[hand];
            const bool shared=grabbing[1-hand] && grabs[1-hand].instance==grab.instance;
            if(shared && (hand==1 || !input.grabActive[1-hand] || input.grab[1-hand])) continue;
            if(now<grab.at || now-grab.at>50 || (hand==1 && pose.held)) continue;
            VRMath::Matrix actual{},wrist=pose.wrists[hand];
            if(!BodyPose(grab.handle,grab.instance,actual) || !VRGrip::Rigid(wrist) || Natives::IsObjectAttached(grab.handle)) continue;
            auto target=VRGrip::Target(grab.offset,wrist);
            if(shared)
            {
                auto other=pose.wrists[1-hand];if(!VRGrip::Rigid(other)) continue;
                target=VRGrip::Combine(target,VRGrip::Target(grabs[1-hand].offset,other));
            }
            // A hand pulling against scenery must not launch a blocked prop.
            const float error=VRMath::Length3(VRMath::Difference3(target[3],actual[3]));
            if(!std::isfinite(error) || error>.12f) continue;
            auto motion=grab.motion;motion.Add(target[3],now);
            VRMath::Vector tracked{},current{};
            if(!motion.Velocity(now,tracked) || VRMath::Length3(tracked)<1.25f) continue;
            Natives::GetObjectVelocity(grab.handle,&current[0],&current[1],&current[2]);
            const float speed=VRMath::Length3(current);
            if(!std::isfinite(speed) || speed>20.f) continue;
            const auto released=VRGrip::ThrowVelocity(current,tracked,grab.mass,grabForce*(shared ? 2.f:1.f));
            // CE A06B00->B25F10 accepts world XYZ without angular setter's
            // frameTime*50 scaling. One write at release; contacts stay native.
            Natives::SetObjectInitialVelocity(grab.handle,released[0],released[1],released[2]);
            throwSpeed=VRMath::Length3(released);++throws;
        }
    }

    bool PalmOverlap(const VRMath::Vector& palm,void* owner,void* candidate,float radius)
    {
        alignas(16) unsigned char hit[0x60]{};
        const uint32_t invalidPair=0xffff;std::memcpy(hit+0x54,&invalidPair,4);
        grabCandidates.assign(1,candidate);selectingGrab=true;
        // CE A535E0 param9=1 invokes the stationary sphere overlap first.
        const bool found=grabQuery(palm.data(),palm.data(),radius,owner,hit,0x8E,0xffffffff,1,1);
        selectingGrab=false;
        return found && *reinterpret_cast<void**>(hit)==candidate;
    }

    bool DoorOverlap(const VRMath::Vector& palm,void* owner,DoorState& door,float radius)
    {
        alignas(16) unsigned char hit[0x60]{};
        const uint32_t invalidPart=0xffffffff;std::memcpy(hit+0x50,&invalidPart,4);
        const uint32_t invalidPair=0xffff;std::memcpy(hit+0x54,&invalidPair,4);
        grabCandidates.assign(1,door.root);if(door.child) grabCandidates.push_back(door.child);
        selectingGrab=true;
        const bool found=grabQuery(palm.data(),palm.data(),radius,owner,hit,0x8E,0xffffffff,1,1);
        selectingGrab=false;
        if(!found) {++doorQueryNoHit;return false;}
        const auto instance=*reinterpret_cast<void**>(hit);
        const unsigned part=*reinterpret_cast<uint16_t*>(hit+0x52);lastDoorQueryPart=part;
        // Native 767070 writes the actual compound collision-part index to
        // hit+52. Match that result, not a cumulative leaf-query boolean.
        // An independently simulated child already identifies this door.
        const bool childHit=door.child && door.child!=door.root && instance==door.child;
        const bool rootHit=instance==door.root && part>=door.firstPart && part-door.firstPart<door.partCount;
        if(childHit || rootHit) {door.hitPart=part;return true;}
        ++doorQueryWrongPart;return false;
    }

    template<class Query>
    bool HandOverlap(const Pose& pose,unsigned hand,float radius,Query query)
    {
        // Search the whole hand, not only the mean proximal finger joints.
        // Capture/drive still use the original independently tracked wrist.
        std::array<VRMath::Vector,3> centers{pose.points[hand][1],pose.points[hand][0],{}};
        for(unsigned tip=2;tip<=6;++tip)
            for(unsigned axis=0;axis<3;++axis) centers[2][axis]+=pose.points[hand][tip][axis]*.2f;
        for(unsigned i=0;i<centers.size();++i)
            if(query(centers[i],radius)) {++handSearchHits[i];return true;}
        return false;
    }

    bool ReadRagdoll(Ped handle,void* expected,void*& root,unsigned& parts,float& mass,bool cached=false)
    {
        if(!ragdollActivity || !ragdollPermission || !ragdollActivate || !ragdollSettleHook) return false;
        auto* entity=static_cast<char*>(PedEntity(handle));
        if(!entity || entity!=expected || entity==player.load() || !Natives::DoesCharExist(handle) ||
            (!Natives::IsCharDead(handle) && !Natives::IsPedRagdoll(handle))) return false;
        root=*reinterpret_cast<void**>(entity+0x7B4);
        if(!root || (!cached && root!=*reinterpret_cast<void**>(entity+0x38)) ||
            (!cached && *reinterpret_cast<uint16_t*>(static_cast<char*>(root)+8)==0xffff) ||
            !*reinterpret_cast<void**>(static_cast<char*>(root)+0x64)) return false;
        auto* archetype=*reinterpret_cast<char**>(static_cast<char*>(root)+4);
        auto* bound=archetype ? *reinterpret_cast<char**>(archetype+12):nullptr;
        if(!bound || static_cast<unsigned char>(bound[4])!=12) return false;
        parts=*reinterpret_cast<uint16_t*>(bound+0x92);
        if(!parts || parts>64 || !NaturalMotion::GetRagdollComponentMatrixInternal) return false;
        // Same registered archetype mass getter used by APPLY_FORCE_TO_PED
        // (CE BA3F3C..BA3F48). Never change a pedestrian's mass/ownership.
        using MassGetter=float (__thiscall*)(void*);
        auto getter=reinterpret_cast<MassGetter>((*reinterpret_cast<void***>(archetype))[0x24/4]);
        mass=getter(archetype);
        return std::isfinite(mass) && mass>0 && mass<2000.f;
    }

    bool RagdollFrame(void* entity,unsigned part,VRMath::Matrix& frame)
    {
        NaturalMotion::RagdollComponentMatrix value{};
        if(!NaturalMotion::GetRagdollComponentMatrixInternal(reinterpret_cast<uintptr_t>(entity),&value,int(part))) return false;
        std::memcpy(&frame,value.values,sizeof(frame));
        return VRGrip::Rigid(frame);
    }

    bool AcquireRagdoll(unsigned hand,const Pose& pose,const VRMath::Matrix& wrist)
    {
        auto* pool=CPed::GetPedPool();
        if(!pool || pool->GetSize()<=0 || pool->GetSize()>4096) return false;
        RagdollGrab selected{};float nearest=acquisitionRadius,selectedMass=0;
        for(int slot=0;slot<pool->GetSize();++slot)
        {
            auto* storage=pool->GetSlot(slot);
            auto* entity=storage && pool->m_nStorageSize==4 ? *reinterpret_cast<void**>(storage):storage;
            if(!entity || entity==pose.owner) continue;
            auto* matrix=*reinterpret_cast<VRMath::Matrix**>(static_cast<char*>(entity)+0x20);
            if(!matrix || VRMath::Length3(VRMath::Difference3((*matrix)[3],pose.points[hand][1]))>4.f) continue;
            Ped handle=pool->GetIndex(storage);void* root=nullptr;unsigned parts=0;float mass=0;
            if(!ReadRagdoll(handle,entity,root,parts,mass,true)) continue;
            if(root!=*reinterpret_cast<void**>(static_cast<char*>(entity)+0x38))
            {
                // Settled corpses retain their articulated bound but the game
                // switches active collision back to the simple ped body.
                // Wake only a DEAD corpse whose cached part is within reach;
                // Use native contact permission and physics activation without
                // posting a scripted NMRelax task against the corpse's task.
                if(!Natives::IsCharDead(handle)) continue;
                bool withinReach=false;
                auto* archetype=*reinterpret_cast<char**>(static_cast<char*>(root)+4);
                auto* bound=*reinterpret_cast<char**>(archetype+12);
                auto* children=*reinterpret_cast<char***>(bound+0x80);
                for(unsigned p=0;p<parts && !withinReach;++p)
                {
                    if(!children || !children[p]) continue;
                    VRMath::Matrix frame{};if(!RagdollFrame(entity,p,frame)) continue;
                    // Native768B50 uses child bound+8 sphere radius and +30
                    // center. Include limb extent, not just its joint origin,
                    // for waking; exact native overlap still gates acquisition.
                    const auto limbRadius=*reinterpret_cast<float*>(children[p]+8);
                    VRMath::Vector center{};std::memcpy(center.data(),children[p]+0x30,12);center[3]=1.f;
                    center=VRMath::Transform(center,frame);
                    if(!std::isfinite(limbRadius) || limbRadius<0 || limbRadius>2.f ||
                        !std::isfinite(center[0]+center[1]+center[2])) continue;
                    withinReach=HandOverlap(pose,hand,acquisitionRadius,[&](const auto& point,float radius)
                    {return VRMath::Length3(VRMath::Difference3(point,center))<=radius+limbRadius;});
                }
                const auto now=GetTickCount64();
                if(!withinReach || (lastWakePed[hand]==handle && now-lastWakeAt[hand]<2000)) continue;
                lastWakePed[hand]=handle;lastWakeAt[hand]=now;++ragdollWakeAttempts;
                if(!ragdollPermission(entity,2,pose.owner,grabForce)) continue;
                ragdollActivate(entity,true,true);
                // Do not trust the wrapper's return alone: require actual
                // native root replacement before querying/applying force.
                if(!ReadRagdoll(handle,entity,root,parts,mass)) continue;
                ++ragdollWakeSuccesses;
            }
            ++ragdollCandidates;unsigned part=~0u;
            auto overlap=[&](const auto& point,float radius)
            {
                alignas(16) unsigned char hit[0x60]{};
                const uint32_t invalid=0xffffffff;std::memcpy(hit+0x50,&invalid,4);std::memcpy(hit+0x54,&invalid,4);
                grabCandidates.assign(1,root);selectingGrab=true;
                // Search all native instance categories; narrowphase remains
                // restricted to the selected NPC's active articulated root.
                const bool found=grabQuery(point.data(),point.data(),radius,pose.owner,hit,0xFF,0xffffffff,1,1);
                selectingGrab=false;
                unsigned foundPart=*reinterpret_cast<uint16_t*>(hit+0x52);
                if(!found || *reinterpret_cast<void**>(hit)!=root || foundPart>=parts) return false;
                part=foundPart;return true;
            };
            if(!HandOverlap(pose,hand,nearest,overlap)) continue;
            ++ragdollHits;float low=0,high=nearest;
            for(unsigned pass=0;pass<4;++pass)
            {float middle=(low+high)*.5f;if(HandOverlap(pose,hand,middle,overlap)) high=middle;else low=middle;}
            VRMath::Matrix frame{};
            if(!RagdollFrame(entity,part,frame)) {++ragdollRejected;continue;}
            selected={};selected.handle=handle;selected.entity=entity;selected.root=root;selected.part=part;
            selected.wristInPart=VRMath::Multiply(wrist,VRMath::InverseRigid(frame));
            selected.anchor=selected.wristInPart[3];selected.previous=wrist[3];selected.previousTarget=wrist[3];selected.at=GetTickCount64();
            selectedMass=mass;nearest=high;
        }
        if(!selected.handle) return false;
        ragdollGrabs[hand]=selected;ragdollEntities[hand]=selected.entity;ragdollGrabbing[hand]=true;
        gripArmed[hand]=false;ragdollMass=selectedMass;ragdollPart=int(selected.part);++ragdollStarts;
        return true;
    }

    void DriveRagdolls(const Pose& pose,float dt,uint64_t now,Feedback& result)
    {
        float maxDistance=0;
        for(unsigned hand=0;hand<2;++hand)
        {
            if(!ragdollGrabbing[hand]) continue;
            auto& grab=ragdollGrabs[hand];void* root=nullptr;unsigned parts=0;float mass=0;
            VRMath::Matrix frame{};
            if(!ReadRagdoll(grab.handle,grab.entity,root,parts,mass) || root!=grab.root || grab.part>=parts)
            {
                // A corpse can retire its articulated root between callbacks.
                // Allow a still-held button to retry that same valid corpse;
                // never rearm a recovered living NPC or invalid pool identity.
                void* cached=nullptr;unsigned count=0;float registeredMass=0;
                const bool retry=ReadRagdoll(grab.handle,grab.entity,cached,count,registeredMass,true) &&
                    cached==grab.root && Natives::IsCharDead(grab.handle);
                ++ragdollLostPhysics;ReleaseGrab(hand);if(retry) gripArmed[hand]=true;continue;
            }
            if(!RagdollFrame(grab.entity,grab.part,frame)) {++ragdollLostFrame;ReleaseGrab(hand);continue;}
            auto actual=VRMath::Multiply(grab.wristInPart,frame);
            const auto target=pose.wrists[hand][3],delta=VRMath::Difference3(target,actual[3]);
            const float distance=VRMath::Length3(delta);
            if(!std::isfinite(distance) || distance>recoveryDistance) {ReleaseGrab(hand);++grabUnsafeReleases;continue;}
            VRMath::Vector velocity{},targetVelocity{},force{};
            if(now>grab.at && now-grab.at<=100)
                for(unsigned axis=0;axis<3;++axis)
                {velocity[axis]=(actual[3][axis]-grab.previous[axis])/dt;targetVelocity[axis]=(target[axis]-grab.previousTarget[axis])/dt;}
            if(VRMath::Length3(velocity)>20.f) {ReleaseGrab(hand);++grabUnsafeReleases;continue;}
            targetVelocity=VRGrip::Bounded(targetVelocity,5.f);
            const bool shared=ragdollGrabbing[1-hand] && ragdollGrabs[1-hand].root==root;
            // Gain uses a bounded share of total native mass, not a replacement
            // mass. Separate grips distribute gravity support over the body.
            const float driveMass=mass/float(parts);
            for(unsigned axis=0;axis<3;++axis) force[axis]=VRGrip::Spring(delta[axis],velocity[axis],targetVelocity[axis],driveMass,12.f,dt);
            force[2]+=mass*9.81f/(shared ? 2.f:1.f);
            force=VRGrip::Bounded(force,grabForce);
            auto* entityMatrix=*reinterpret_cast<VRMath::Matrix**>(static_cast<char*>(grab.entity)+0x20);
            if(!entityMatrix) {ReleaseGrab(hand);continue;}
            if(Natives::IsCharDead(grab.handle) && *reinterpret_cast<int*>(static_cast<char*>(grab.entity)+0x7B8)==6)
            {
                // Native9F0890 settles state6 after one inactive second.
                // Refresh existing state6 through native9F29E0 only while a
                // valid dead-body grip is active; release retains its normal
                // one-second grace. Recovery/teardown paths are not bypassed.
                ragdollActivity(grab.entity,6,false);++ragdollKeepAlive;
            }
            const auto offset=VRMath::Difference3(actual[3],(*entityMatrix)[3]);
            // Native continuous WORLD force at the captured grip, on the
            // returned collision component. Joints/contacts remain native.
            Natives::ApplyForceToPed(grab.handle,0,force[0],force[1],force[2],offset[0],offset[1],offset[2],grab.part,false,false,false);
            result.wrists[hand]=actual;result.rotation[hand]=true;result.at[hand]=now;
            grab.previous=actual[3];grab.previousTarget=target;grab.at=now;
            maxDistance=std::max(maxDistance,distance);ragdollMass=mass;++ragdollUpdates;
        }
        ragdollError=maxDistance;
    }

    bool AcquireDoor(unsigned hand,const Pose& pose,const VRMath::Matrix& wrist)
    {
        if(!requestDoor) return false;
        auto* pool=CVehicle::GetVehiclePool();
        if(!pool || pool->GetSize()<=0 || pool->GetSize()>4096) return false;
        const auto& palm=pose.points[hand][1];
        float nearest=acquisitionRadius;DoorGrab selected{};DoorState best{};
        for(int slot=0;slot<pool->GetSize();++slot)
        {
            auto* storage=pool->GetSlot(slot);
            auto* entity=storage && pool->m_nStorageSize==4 ? *reinterpret_cast<void**>(storage):storage;
            if(!entity) continue;
            auto* root=*reinterpret_cast<VRMath::Matrix**>(static_cast<char*>(entity)+0x20);
            if(!root || VRMath::Length3(VRMath::Difference3((*root)[3],palm))>6.f) continue;
            const auto handle=pool->GetIndex(storage);
            uint32_t lock=~0u;Natives::GetCarDoorLockStatus(handle,&lock);
            if(lock==~0u) continue;
            for(unsigned index=0;index<6;++index)
            {
                DoorState state{};
                if(!ReadDoor(handle,index,state) || !HandOverlap(pose,hand,nearest,[&](const auto& point,float r){return DoorOverlap(point,pose.owner,state,r);})) continue;
                float low=0,high=nearest;
                for(unsigned pass=0;pass<4;++pass)
                {
                    const float middle=(low+high)*.5f;
                    if(HandOverlap(pose,hand,middle,[&](const auto& point,float r){return DoorOverlap(point,pose.owner,state,r);})) high=middle;else low=middle;
                }
                if(selected.handle && high>=nearest) continue;
                nearest=high;best=state;selected.handle=handle;selected.index=index;
                selected.entity=state.entity;selected.root=state.root;selected.record=state.record;
            }
        }
        if(!selected.handle) {++doorMisses;return false;}
        // A second hand inherits the original policy, not the temporarily
        // disabled latch state, so either hand can be the last to release.
        selected.automaticLatch=doorGrabbing[1-hand] && doorGrabs[1-hand].record==selected.record
            ? doorGrabs[1-hand].automaticLatch
            : (*reinterpret_cast<uint32_t*>(best.record+0x10)&0x10000u)!=0;
        selected.controlled=doorGrabbing[1-hand] && doorGrabs[1-hand].record==selected.record && doorGrabs[1-hand].controlled;
        const auto inverse=VRMath::InverseRigid(best.frame);
        selected.wristInDoor=VRMath::Multiply(wrist,inverse);
        auto point=palm;point[3]=1;selected.palmInDoor=VRMath::Transform(point,inverse);
        doorGrabs[hand]=selected;doorGrabbing[hand]=true;gripArmed[hand]=false;
        // Closed parts can initially share the vehicle root. Never exclude
        // that whole car from player contact to simulate holding its door.
        doorInstances[hand]=best.child!=best.root ? best.child:nullptr;
        ++doorStarts;++grabStarts;
        selectedDoorIndex=selected.index;selectedDoorRecordId=*reinterpret_cast<int*>(best.record);
        selectedDoorCollisionPart=best.hitPart;
        uint32_t lock=~0u;Natives::GetCarDoorLockStatus(selected.handle,&lock);doorLockState=lock;
        if(lock<=1) {RequestGrabbedDoor(best,best.fraction);doorGrabs[hand].controlled=true;}
        else {++doorLocked;doorFlagsBefore=*reinterpret_cast<uint32_t*>(best.record+0x10);doorFlagsAfter=doorFlagsBefore.load();}
        return true;
    }

    void DriveDoors(const Pose& pose,uint64_t now,Feedback& result)
    {
        for(unsigned hand=0;hand<2;++hand)
        {
            if(!doorGrabbing[hand]) continue;
            auto& grab=doorGrabs[hand];DoorState state{};
            if(!ReadDoor(grab.handle,grab.index,state) || state.entity!=grab.entity || state.root!=grab.root || state.record!=grab.record)
            {ReleaseDoor(hand);continue;}
            uint32_t lock=~0u;Natives::GetCarDoorLockStatus(grab.handle,&lock);
            doorLockState=lock;
            const auto palm=VRMath::Transform(grab.palmInDoor,state.frame);
            auto requested=pose.points[hand][1];
            const bool shared=doorGrabbing[1-hand] && doorGrabs[1-hand].record==grab.record;
            if(shared)
            {
                const auto otherPalm=VRMath::Transform(doorGrabs[1-hand].palmInDoor,state.frame);
                for(unsigned axis=0;axis<3;++axis)
                    requested[axis]=(requested[axis]+pose.points[1-hand][1][axis]+palm[axis]-otherPalm[axis])*.5f;
            }
            if(VRMath::Length3(VRMath::Difference3(palm,pose.points[hand][1]))>recoveryDistance)
            {ReleaseDoor(hand);++grabUnsafeReleases;continue;}
            if(lock>1)
            {
                // A locked car remains a valid contact/grasp. Do not send an
                // opening request, clear latch bits, unlock or break windows.
                // Cancel a motor we started before a mid-grab native lock.
                if(grab.controlled)
                {
                    auto& flags=*reinterpret_cast<uint32_t*>(state.record+0x10);
                    const auto latch=flags&0x100u;
                    flags=(flags&~0x10000u)|(grab.automaticLatch ? 0x10000u:0u);
                    requestDoor(state.record,std::clamp(state.fraction,0.f,1.f),0);flags|=latch;
                    grab.controlled=false;
                    if(shared) doorGrabs[1-hand].controlled=false;
                }
                doorFlagsBefore=*reinterpret_cast<uint32_t*>(state.record+0x10);doorFlagsAfter=doorFlagsBefore.load();
                doorFraction=state.fraction;doorTarget=state.fraction;++doorLocked;
                result.wrists[hand]=VRMath::Multiply(grab.wristInDoor,state.frame);
                result.rotation[hand]=true;result.at[hand]=now;continue;
            }
            float target=state.fraction;
            if(!VRGrip::HingeTarget(state.frame[3],state.axis,palm,requested,state.fraction,state.span,target))
            {ReleaseDoor(hand);continue;}
            if(!shared || hand==0)
            {
                // The native capped 1DOF motor retains contacts and angle limits.
                RequestGrabbedDoor(state,target);
                grab.controlled=true;if(shared) doorGrabs[1-hand].controlled=true;
                doorFraction=state.fraction;doorTarget=target;++doorUpdates;
            }
            doorInstances[hand]=state.child!=state.root ? state.child:nullptr;
            result.wrists[hand]=VRMath::Multiply(grab.wristInDoor,state.frame);
            result.rotation[hand]=true;result.at[hand]=now;
        }
    }

    void ProcessGrab(unsigned hand,const Pose& pose,const OpenXRVR::ControllerInput& input)
    {
        const bool down=input.grab[hand];
        if(down) ++gripSamples[hand];
        if(!down) {gripArmed[hand]=true;ReleaseGrab(hand);}
        VRMath::Matrix body{};
        if(grabbing[hand] && (!BodyPose(grabs[hand].handle,grabs[hand].instance,body) ||
            Natives::IsObjectAttached(grabs[hand].handle) || (hand==1 && pose.held)))
        {ReleaseGrab(hand);gripArmed[hand]=false;}
        if(doorGrabbing[hand] && (hand==1 && pose.held)) ReleaseDoor(hand);
        if(ragdollGrabbing[hand] && (hand==1 && pose.held)) ReleaseGrab(hand);
        if(!down || !gripArmed[hand] || grabbing[hand] || doorGrabbing[hand] || ragdollGrabbing[hand] || (hand==1 && pose.held) || GetTickCount64()<nextGrabQuery[hand]) return;
        nextGrabQuery[hand]=GetTickCount64()+50;++grabAttempts;
        auto wrist=pose.wrists[hand];if(!VRGrip::Rigid(wrist)) return;
        const auto& palm=pose.points[hand][1];
        auto* pool=CObject::GetObjectsPool();
        if(!pool || pool->GetSize()<=0 || pool->GetSize()>=65536) return;
        Object selected=0;void* selectedBody=nullptr;float nearest=acquisitionRadius,selectedMass=0;
        // Loose CObjects first. Vehicle roots/door fragments remain adapters;
        // do not reuse the unstable whole-car attachment experiment here.
        for(int slot=0;slot<pool->GetSize();++slot)
        {
            auto* storage=pool->GetSlot(slot);
            auto* entity=storage && pool->m_nStorageSize==4 ? *reinterpret_cast<void**>(storage):storage;
            if(!entity || entity==pose.held) continue;
            auto* instance=*reinterpret_cast<void**>(static_cast<char*>(entity)+0x38);
            if(!instance || IsProxy(instance)) continue;
            const auto handle=pool->GetIndex(storage);
            if(!BodyPose(handle,instance,body) || VRMath::Length3(VRMath::Difference3(body[3],palm))>3.f ||
                !Natives::DoesObjectHavePhysics(handle) || Natives::IsObjectAttached(handle) || Natives::IsObjectStatic(handle)) continue;
            float mass=0;Natives::GetObjectMass(handle,&mass);
            if(!std::isfinite(mass) || mass<=0 || !HandOverlap(pose,hand,nearest,[&](const auto& point,float r){return PalmOverlap(point,pose.owner,instance,r);})) continue;
            ++grabHits;
            // Rank by actual collision-surface proximity, not model origin.
            // Bisection also works when the palm is already inside a collider.
            float low=0,high=nearest;
            for(unsigned pass=0;pass<4;++pass)
            {
                const float middle=(low+high)*.5f;
                if(HandOverlap(pose,hand,middle,[&](const auto& point,float r){return PalmOverlap(point,pose.owner,instance,r);})) high=middle;else low=middle;
            }
            if(selected && high>=nearest) continue;
            selected=handle;selectedBody=instance;selectedMass=mass;nearest=high;
        }
        if(!selected) {if(!AcquireRagdoll(hand,pose,wrist) && !AcquireDoor(hand,pose,wrist)) ++grabRejected;return;}
        if(!BodyPose(selected,selectedBody,body)) {++grabRejected;return;}
        auto& grab=grabs[hand];grab={};grab.handle=selected;grab.instance=selectedBody;grab.mass=selectedMass;
        grab.offset=VRGrip::Capture(body,wrist);
        grab.wristInBody=VRMath::Multiply(wrist,VRMath::InverseRigid(body));
        grab.previousBody=body;grab.previousTarget=body;grab.at=GetTickCount64();
        grab.motion.Add(body[3],grab.at);
        uint32_t objectModel=0;Natives::GetObjectModel(selected,&objectModel);
        Vector3 minimum{},maximum{};Natives::GetModelDimensions(objectModel,&minimum,&maximum);
        VRMath::Vector size{maximum.fX-minimum.fX,maximum.fY-minimum.fY,maximum.fZ-minimum.fZ,0};
        for(unsigned axis=0;axis<3;++axis)
            size[axis]=std::isfinite(size[axis]) ? std::clamp(std::abs(size[axis]),.04f,6.f):.2f;
        grab.inertia=VRGrip::BoxInertia(size,grab.mass);grab.radius=std::clamp(VRMath::Length3(size)*.5f,.04f,2.f);
        grabRadius=grab.radius;grabInertia=std::max({grab.inertia[0],grab.inertia[1],grab.inertia[2]});
        grabInstances[hand]=selectedBody;grabEntities[hand]=Entity(selected);
        grabbing[hand]=true;gripArmed[hand]=false;grabMass=selectedMass;++grabStarts;
        Natives::SetObjectDynamic(selected,true);
    }

    void DriveGrabs(const Pose& pose,float dt,uint64_t now,Feedback& result)
    {
        float positionError=0,angleError=0,angularSpeed=0,appliedTorque=0;
        for(unsigned hand=0;hand<2;++hand)
        {
            if(!grabbing[hand]) continue;
            auto& grab=grabs[hand];
            const bool shared=grabbing[1-hand] && grabs[1-hand].instance==grab.instance;
            if(hand==1 && shared) continue; // Exactly one motor per held body.
            VRMath::Matrix actual{};
            if(!BodyPose(grab.handle,grab.instance,actual)) {ReleaseGrab(hand);if(shared)ReleaseGrab(1-hand);continue;}
            auto wrist=pose.wrists[hand];
            if(!VRGrip::Rigid(wrist)) {ReleaseGrab(hand);continue;}
            auto target=VRGrip::Target(grab.offset,wrist);
            if(shared)
            {
                auto other=pose.wrists[1-hand];
                if(!VRGrip::Rigid(other)) {ReleaseGrab(1-hand);continue;}
                target=VRGrip::Combine(target,VRGrip::Target(grabs[1-hand].offset,other));++twoHandUpdates;
            }
            const auto delta=VRMath::Difference3(target[3],actual[3]);
            const float distance=VRMath::Length3(delta);
            // No accumulated catch-up/teleport when tracking or physics diverges.
            if(!std::isfinite(distance) || distance>recoveryDistance)
            {ReleaseGrab(hand);if(shared)ReleaseGrab(1-hand);++grabUnsafeReleases;continue;}
            VRMath::Vector velocity{},angular{},targetVelocity{},targetAngular{};
            // Read motion from successive rigid poses rather than assuming the
            // native rotation getter's frame-scaled units are radians/second.
            if(now>grab.at && now-grab.at<=100)
            {
                velocity=VRMath::Difference3(actual[3],grab.previousBody[3]);
                targetVelocity=VRMath::Difference3(target[3],grab.previousTarget[3]);
                angular=VRGrip::RotationError(grab.previousBody,actual);
                targetAngular=VRGrip::RotationError(grab.previousTarget,target);
                for(unsigned axis=0;axis<3;++axis)
                {velocity[axis]/=dt;targetVelocity[axis]/=dt;angular[axis]/=dt;targetAngular[axis]/=dt;}
            }
            if(VRMath::Length3(velocity)>20.f || VRMath::Length3(angular)>60.f)
            {ReleaseGrab(hand);if(shared)ReleaseGrab(1-hand);++grabUnsafeReleases;continue;}
            targetVelocity=VRGrip::Bounded(targetVelocity,5.f);targetAngular=VRGrip::Bounded(targetAngular,12.f);
            VRMath::Vector force{},torque{};
            const auto rotationError=VRGrip::RotationError(actual,target);
            for(unsigned axis=0;axis<3;++axis)
                force[axis]=VRGrip::Spring(delta[axis],velocity[axis],targetVelocity[axis],grab.mass,12.f,dt);
            force[2]+=grab.mass*9.81f;
            const float strength=grabForce*(shared ? 2.f:1.f);
            force=VRGrip::Bounded(force,strength);
            // Solve angular drive in principal MODEL axes, then return torque
            // to world axes. Box inertia is an explicit bounded estimate.
            for(unsigned row=0;row<3;++row)
            {
                const float local=VRGrip::Spring(VRMath::Dot3(rotationError,actual[row]),VRMath::Dot3(angular,actual[row]),
                    VRMath::Dot3(targetAngular,actual[row]),grab.inertia[row],12.f,dt);
                for(unsigned axis=0;axis<3;++axis) torque[axis]+=local*actual[row][axis];
            }
            torque=VRGrip::Bounded(torque,strength*std::min(grab.radius,.35f));
            Natives::SetObjectDynamic(grab.handle,true);
            Natives::ApplyForceToObject(grab.handle,0,force[0],force[1],force[2],0,0,0,0,0,0,0);
            // Three couples generate torque without net linear force. Native
            // mode0 keeps world axes and leaves solver contacts authoritative.
            for(unsigned axis=0;axis<3;++axis)
            {
                VRMath::Vector lever{},push{};lever[axis]=.1f;
                push[(axis+1)%3]=torque[(axis+2)%3]/.4f;
                push[(axis+2)%3]=-torque[(axis+1)%3]/.4f;
                Natives::ApplyForceToObject(grab.handle,0,push[0],push[1],push[2],lever[0],lever[1],lever[2],0,0,0,0);
                Natives::ApplyForceToObject(grab.handle,0,-push[0],-push[1],-push[2],-lever[0],-lever[1],-lever[2],0,0,0,0);
            }
            positionError=std::max(positionError,distance);angleError=std::max(angleError,VRMath::Length3(rotationError));
            angularSpeed=std::max(angularSpeed,VRMath::Length3(angular));appliedTorque=std::max(appliedTorque,VRMath::Length3(torque));
            for(unsigned h=0;h<2;++h) if(grabbing[h] && grabs[h].instance==grab.instance)
            {
                result.wrists[h]=VRMath::Multiply(grabs[h].wristInBody,actual);result.rotation[h]=true;result.at[h]=now;
                grabs[h].previousBody=actual;grabs[h].previousTarget=target;grabs[h].at=now;
                grabs[h].motion.Add(target[3],now);
            }
        }
        grabPositionError=positionError;grabAngleError=angleError;grabAngularSpeed=angularSpeed;grabTorque=appliedTorque;
    }

    bool ApplyMeleeHit(const Pose& pose,const VRMath::Vector& origin,void* hit,uint32_t type,float damage,unsigned hand,bool* nativeRejected=nullptr)
    {
        if(nativeRejected) *nativeRejected=false;
        auto* instance=*reinterpret_cast<char**>(hit);
        // Mirror 9FD420: only instances carrying the entity flag own +C.
        auto* entity=instance && (static_cast<unsigned char>(instance[0xA])&0x40) ? *reinterpret_cast<void**>(instance+0xC):nullptr;
        if(!instance || entity==pose.owner) return false;
        const unsigned entityType=entity ? ((*reinterpret_cast<uint32_t*>(static_cast<char*>(entity)+0x28)>>6)&15):0;
        if(entityType!=3)
        {
            if(!weaponDispatch || !weaponCtor || !weaponSetType || !weaponDtor ||
                *reinterpret_cast<uint16_t*>(instance+8)==0xFFFF) return false;
            alignas(16) unsigned char context[0x80]{};
            weaponCtor(context);weaponSetType(context,type,1,false);context[0x50]=1;
            char knifeResult[]="Blade_Med_S_A_Res",clubResult[]="Club_Hook_R_Res",leftResult[]="Jab_Res",rightResult[]="cross_R_Res";
            const uint32_t result=Natives::GetHashKey(type==3 ? knifeResult:((type==1 || type==2) ? clubResult:(hand==0 ? leftResult:rightResult)));
            std::memcpy(context+0x54,&result,4);
            auto frame=VRMath::Identity();frame[3]=origin;frame[3][3]=1;
            // CD5A30 passes one 60-byte hit, vehicle extra=0 and damage
            // multipliers. Unity retains our configured physical strike damage.
            const float modifiers[3]={1,1,1};
            const float applied=weaponDispatch(context,pose.owner,frame[0].data(),hit,1,damage,0,modifiers);
            weaponDtor(context);
            ++worldMeleeCalls;
            if(entityType==2) ++vehicleMeleeCalls;
            if(entityType==4) ++objectMeleeCalls;
            lastWorldMeleeDamage=std::isfinite(applied) ? applied:0;
            // Zero native damage may still produce an impact (e.g. protected
            // objects); never substitute synthetic damage or another hit.
            return true;
        }
        auto* pool=CPed::GetPedPool();
        if(!pool || pool->GetSize()<=0 || pool->GetSize()>4096) return false;
        for(int slot=0;slot<pool->GetSize();++slot)
        {
            auto* storage=pool->GetSlot(slot);
            auto* candidate=storage && pool->m_nStorageSize==4 ? *reinterpret_cast<void**>(storage):storage;
            if(candidate!=entity) continue;
            const Ped victim=pool->GetIndex(storage);
            if(!victim || !Natives::DoesCharExist(victim) || Natives::IsCharDead(victim)) return false;
            if(!weaponCtor || !weaponSetType || !weaponDtor || !weaponImpact) return false;
            // CD5A30 uses a temporary CWeapon context: +50 identifies melee,
            // +54 identifies the attack result. Without it B60C80 selects the
            // generic damage flinch instead of the native melee victim task.
            alignas(16) unsigned char context[0x80]{};
            weaponCtor(context);
            weaponSetType(context,type,1,false);
            context[0x50]=1;
            char knifeResult[]="Blade_Med_S_A_Res",clubResult[]="Club_Hook_R_Res",leftResult[]="Jab_Res",rightResult[]="cross_R_Res";
            const uint32_t result=Natives::GetHashKey(type==3 ? knifeResult:((type==1 || type==2) ? clubResult:(hand==0 ? leftResult:rightResult)));
            std::memcpy(context+0x54,&result,4);
            // No attacker animation owns the response. Native victim reaction,
            // blocking, damage protections and fatal/NM response remain intact.
            const float applied=meleeDamage(pose.owner,entity,type,damage,origin.data(),hit,context,0,false);
            lastPunchDamage=std::isfinite(applied) ? applied:0;lastPunchTarget=victim;
            if(!std::isfinite(applied) || applied<=0)
            {if(nativeRejected) *nativeRejected=true;weaponDtor(context);return false;}
            // B5DF50 invokes this separately after damage. It selects native
            // bloodFx spray/decal/clothing stains and impact effects by material,
            // hit part and weapon group; the damage routine never does this.
            auto frame=VRMath::Identity();frame[3]=origin;frame[3][3]=1;
            weaponImpact(context,pose.owner,frame[0].data(),hit,applied,false);
            ++meleeImpacts;
            weaponDtor(context);
            if(auto* group=globalEventGroup())
            {
                alignas(16) unsigned char event[0x24]{};
                meleeEventCtor(event,pose.owner,entity,true);
                if(addEvent(group,event,false,true)) ++meleeEvents;
                meleeEventDtor(event);
            }
            return true;
        }
        return false;
    }

    void ProcessBlade(const Pose& pose,Ped ped,const OpenXRVR::ControllerInput& input,uint64_t now,bool usable)
    {
        if(!combatEnabled || !meleeDamage || !grabQuery || !meleeEventCtor || !meleeEventDtor || !globalEventGroup || !addEvent) return;
        uint32_t selected=~0u;
        if(!usable || !Natives::GetCurrentCharWeapon(ped,&selected) || !VRMelee::Supported(selected) || selected!=pose.meleeProfile.type ||
            !pose.bladeAt || now-pose.bladeAt>50 || !input.sampledAt || now-input.sampledAt>50 ||
            !input.gripPoseValid[1] || VRHandPhysics::IsGrabbing(1) || !pose.held ||
            pose.held!=pose.bladeEntity || *reinterpret_cast<void**>(static_cast<char*>(pose.owner)+0x2C4)!=pose.held)
        {bladeStrike={};bladeEntity=nullptr;bladeReady=false;bladeSpeed=0;meleeType=0;return;}
        if(bladeEntity!=pose.bladeEntity || meleeType!=selected) {bladeStrike={};bladeEntity=pose.bladeEntity;}
        meleeType=selected;meleeSamples=pose.meleeProfile.samples;meleeRadius=pose.meleeProfile.radius;
        bladeReady=true;
        const bool sweep=bladeStrike.Sample(pose.bladeTracked,pose.bladeWorld,pose.bladeSample,
            selected==3 ? knifeSpeed:clubSpeed,pose.meleeProfile.samples);
        bladeSpeed=bladeStrike.speed;
        if(!sweep)
        {
            if(bladeStrike.gate==VRMelee::Strike::Gate::Rearm) ++bladeRearmBlocks;
            if(bladeStrike.gate==VRMelee::Strike::Gate::Cooldown) ++bladeCooldownBlocks;
            if(bladeStrike.gate==VRMelee::Strike::Gate::Discontinuity) ++bladeTrackingResets;
            return;
        }
        // Every blade sample sweeps against the full world. Keep the earliest
        // contact over the motion interval, including walls and prop blockers.
        alignas(16) unsigned char closest[0x60]{};
        VRMath::Vector origin{};
        float earliest=2.f;
        punchOwner=pose.owner;strikeWeapon=pose.held;
        for(unsigned i=0;i<pose.meleeProfile.samples;++i)
        {
            alignas(16) unsigned char hit[0x60]{};
            const uint32_t invalid=0xffffffff;
            std::memcpy(hit+0x50,&invalid,4);std::memcpy(hit+0x54,&invalid,4);
            ++bladeSweeps;
            if(!grabQuery(bladeStrike.begin[i].data(),pose.bladeWorld[i].data(),pose.meleeProfile.radius,
                pose.owner,hit,0xFF,0xffffffff,1,0)) continue;
            VRMath::Vector point{};std::memcpy(point.data(),hit+0x10,16);
            const float distance=VRMath::Length3(VRMath::Difference3(pose.bladeWorld[i],bladeStrike.begin[i]));
            const float fraction=distance>.0001f ? std::clamp(VRMath::Length3(VRMath::Difference3(point,bladeStrike.begin[i]))/distance,0.f,1.f):0.f;
            if(fraction<earliest) {earliest=fraction;origin=bladeStrike.begin[i];std::memcpy(closest,hit,sizeof(hit));}
        }
        punchOwner=nullptr;strikeWeapon=nullptr;
        if(earliest>1.f) {++bladeMisses;return;}
        bladeStrike.Contact();++bladeContacts;
        bool rejected=false;
        if(ApplyMeleeHit(pose,origin,closest,selected,selected==3 ? knifeDamage:clubDamage,1,&rejected)) ++bladeHits;
        else if(rejected) ++bladeNativeRejects;
        else ++bladeNonLivingContacts;
    }

    void ProcessPunches(const Pose& pose,Ped ped,const OpenXRVR::ControllerInput& input,uint64_t now,bool usable)
    {
        if(!combatEnabled || !meleeDamage || !grabQuery || !meleeEventCtor || !meleeEventDtor || !globalEventGroup || !addEvent) return;
        uint32_t selected=~0u;
        if(!usable || !Natives::GetCurrentCharWeapon(ped,&selected) ||
            !input.sampledAt || now-input.sampledAt>50)
        {
            for(unsigned h=0;h<2;++h) {strikes[h]={};fists[h]=false;fistSpeed[h]=0;}
            return;
        }
        for(unsigned hand=0;hand<2;++hand)
        {
            auto& strike=strikes[hand];
            if(!VRPunch::FreeHand(hand,selected,pose.held!=nullptr,pose.supporting[hand]) ||
                !input.gripPoseValid[hand] || now-pose.at[hand]>50 || VRHandPhysics::IsGrabbing(hand))
            {strike={};fists[hand]=false;fistSpeed[hand]=0;continue;}
            // Same four finger controls as the visible fist. Thumb is free.
            fists[hand]=input.gripActive[hand] && input.gripped[hand] && input.triggerActive[hand] &&
                input.trigger[hand]>=(fists[hand] ? .4f:.7f);
            VRMath::Vector tracked{input.gripPosition[hand][0],input.gripPosition[hand][1],input.gripPosition[hand][2],1};
            auto palm=pose.points[hand][1];palm[3]=1;
            const bool sweep=strike.Sample(tracked,palm,input.sampledAt,fists[hand],punchSpeed);
            fistSpeed[hand]=strike.speed;
            if(!sweep) continue;
            alignas(16) unsigned char hit[0x60]{};
            const uint32_t invalid=0xffffffff;
            std::memcpy(hit+0x50,&invalid,4);std::memcpy(hit+0x54,&invalid,4);
            auto begin=strike.begin;begin[3]=1;
            ++punchSweeps;
            // Sweep the actual fist through the whole world. No candidate-only
            // grab filtering: a nearer wall/prop stops a strike through it.
            punchOwner=pose.owner;
            const bool contact=grabQuery(begin.data(),palm.data(),.075f,pose.owner,hit,0xFF,0xffffffff,1,0);
            punchOwner=nullptr;
            if(!contact) continue;
            strike.Contact();++punchContacts;
            if(ApplyMeleeHit(pose,begin,hit,0,punchDamage,hand)) ++punchHits[hand];
        }
    }

    void Process()
    {
        Pose pose{};
        AcquireSRWLockShared(&poseLock); pose=desired; ReleaseSRWLockShared(&poseLock);
        const auto now=GetTickCount64();
        Ped ped=0; Natives::GetPlayerChar(Natives::GetPlayerId(),&ped);
        if(failed || !pose.owner || pose.owner!=FindPlayerPed(0) || !OpenXRVR::HasFocusedViews() ||
            !ped || Natives::IsCharDead(ped) || Natives::IsCharInAnyCar(ped) ||
            Natives::IsCharGettingInToACar(ped) ||
            !pose.at[0] || !pose.at[1] || now-pose.at[0]>100 || now-pose.at[1]>100)
        {if(currentOwner || active.load()) Clear(); return;}
        if(currentOwner && currentOwner!=pose.owner) Clear();
        if(!model)
        {
            char name[]="CJ_LD_POOLBALL_1";
            model=Natives::GetHashKey(name);
            if(!Natives::IsModelInCdimage(model)) {failed=true; return;}
        }
        if(!requestedModel)
        {Natives::RequestModel(model); Natives::RequestCollisionForModel(model); requestedModel=true; requestedAt=now;}
        if(!Natives::HasModelLoaded(model) || !Natives::HasCollisionForModelLoaded(model))
        {if(now-requestedAt>30000) failed=true; return;}
        currentOwner=pose.owner; player=pose.owner; weapon=pose.held;
        OpenXRVR::ControllerInput input{};
        const bool gameplayInput=OpenXRVR::GetControllerInput(input) && !input.phoneActive &&
            !(CMenuManager::m_MenuActive && *CMenuManager::m_MenuActive);
        const bool grabInput=grabbingEnabled && gameplayInput;
        if(!grabInput) {input.grab[0]=input.grab[1]=false;}
        // A newly closed punching fist must not auto-acquire a prop/ragdoll
        // mid-strike. Existing holds retain their grip until actual release.
        uint32_t selected=~0u;
        if(combatEnabled && gameplayInput && Natives::GetCurrentCharWeapon(ped,&selected))
            for(unsigned h=0;h<2;++h)
                if(VRPunch::FreeHand(h,selected,pose.held!=nullptr,pose.supporting[h]) &&
                    !VRHandPhysics::IsGrabbing(h) && input.gripActive[h] && input.gripped[h] && input.triggerActive[h] &&
                    input.trigger[h]>=(fists[h] ? .4f:.7f)) input.grab[h]=false;
        Feedback result{}; result.owner=pose.owner;
        float dt=0; Natives::GetFrameTime(&dt);
        if(!std::isfinite(dt) || dt<=0) {Clear(); return;}
        dt=std::clamp(dt,1.f/240.f,1.f/20.f);
        // Before either grip is cleared, combine simultaneous two-hand release
        // once. Menu/action loss does not count as a deliberate throw.
        if(grabInput) ReleaseThrows(pose,input,now);
        float error=0,palmError=0,drivePeak=0;unsigned blocked=0;
        for(unsigned hand=0;hand<2;++hand)
        {
            if(haveTarget[hand] && VRMath::Length3(VRMath::Difference3(pose.points[hand][0],lastTarget[hand]))>recoveryDistance)
            {ClearHand(hand); ++recoveries;}
            lastTarget[hand]=pose.points[hand][0]; haveTarget[hand]=true;
            if(grabbingEnabled) ProcessGrab(hand,pose,input);
            const unsigned nodeLimit=VRHandPhysics::ContactNodeCount;
            for(unsigned i=0;i<nodeLimit;++i)
            {
                auto& node=nodes[hand][i];
                auto target=pose.points[hand][i];
                if(!node.handle && !Create(hand,i,target)) {failed=true; Clear(); return;}
                auto* entity=Entity(node.handle);
                if(!entity || *reinterpret_cast<void**>(static_cast<char*>(entity)+0x38)!=node.instance)
                {failed=true; Clear(); return;}
                VRMath::Vector position{};
                Natives::GetObjectCoordinates(node.handle,&position[0],&position[1],&position[2]);
                const float distance=VRMath::Length3(VRMath::Difference3(target,position));
                auto feedbackPosition=position;
                if(!std::isfinite(distance)) {failed=true; Clear(); return;}
                if(distance>recoveryDistance)
                {ClearHand(hand); ++recoveries; result.at[hand]=0; break;}
                error=std::max(error,distance);
                VRMath::Vector velocity{},targetVelocity{};
                Natives::GetObjectVelocity(node.handle,&velocity[0],&velocity[1],&velocity[2]);
                if(now>node.at && now-node.at<=100)
                    for(unsigned axis=0;axis<3;++axis) targetVelocity[axis]=(target[axis]-node.target[axis])*1000.f/float(now-node.at);
                const float speed=VRMath::Length3(targetVelocity);
                if(speed>5.f) for(unsigned axis=0;axis<3;++axis) targetVelocity[axis]*=5.f/speed;
                // Implicit spring motor on OUR rigid body. Force is shared over
                // ten nodes per arm. Never apply forces to a contacted prop;
                // native collision solving supplies its contact response.
                VRMath::Vector force{};
                const float mass=NodeMass,k=Stiffness,d=Damping;
                const float divisor=1.f+d*dt/mass+k*dt*dt/mass;
                for(unsigned axis=0;axis<3;++axis)
                    force[axis]=(k*(target[axis]-position[axis]-velocity[axis]*dt)+d*(targetVelocity[axis]-velocity[axis]))/divisor;
                force[2]+=mass*9.81f;
                const float strength=VRMath::Length3(force);
                if(i==1) palmError=std::max(palmError,distance);
                if(distance>.04f && VRMath::Length3(velocity)<.1f) ++blocked;
                const float limit=(grabbing[hand] || doorGrabbing[hand] || ragdollGrabbing[hand]) ? armForce*.2f/nodeLimit:armForce/nodeLimit;
                if(!std::isfinite(strength)) {failed=true; Clear(); return;}
                if(strength>limit) {for(unsigned axis=0;axis<3;++axis) force[axis]*=limit/strength; ++limitedDrives;}
                drivePeak=std::max(drivePeak,std::min(strength,limit));
                Natives::SetObjectDynamic(node.handle,true);
                // Mode0 -> B23560 -> collider virtual+80: continuous force,
                // world axes/COM offset, no implicit mass multiplication.
                Natives::ApplyForceToObject(node.handle,0,force[0],force[1],force[2],0,0,0,0,0,0,0);
                node.target=target; node.at=now;
                if(i==0) {result.wrists[hand]=pose.wrists[hand];result.wrists[hand][3]=feedbackPosition;result.at[hand]=now;}
            }
        }
        if(grabbingEnabled) DriveGrabs(pose,dt,now,result);
        if(grabbingEnabled) DriveDoors(pose,now,result);
        if(grabbingEnabled) DriveRagdolls(pose,dt,now,result);
        ProcessPunches(pose,ped,input,now,gameplayInput);
        ProcessBlade(pose,ped,input,now,gameplayInput);
        maxError=error;palmColliderError=palmError;contactDrivePeak=drivePeak;blockedContactNodes=blocked; ++updates;
        AcquireSRWLockExclusive(&poseLock); feedback=result; ReleaseSRWLockExclusive(&poseLock);
    }
}

void VRHandPhysics::Publish(void* owner,void* held,unsigned hand,const VRMath::Vector* points,const VRMath::Matrix& wrist,bool supporting)
{
    if(!enabled || hand>1 || !points) return;
    for(unsigned i=0;i<NodeCount;++i) for(unsigned axis=0;axis<3;++axis)
        if(!std::isfinite(points[i][axis])) return;
    for(unsigned row=0;row<4;++row) for(unsigned axis=0;axis<3;++axis)
        if(!std::isfinite(wrist[row][axis])) return;
    for(unsigned row=0;row<3;++row)
        if(VRMath::Length3(wrist[row])<.0001f) return;
    AcquireSRWLockExclusive(&poseLock);
    if(desired.owner!=owner) desired={};
    desired.owner=owner; desired.held=held; desired.at[hand]=GetTickCount64();
    desired.wrists[hand]=wrist;
    desired.supporting[hand]=supporting;
    std::memcpy(desired.points[hand],points,sizeof(desired.points[hand]));
    ReleaseSRWLockExclusive(&poseLock);
}

bool VRHandPhysics::IsGrabbing(unsigned hand) {return hand<2 && (grabbing[hand].load() || doorGrabbing[hand].load() || ragdollGrabbing[hand].load());}
VRHandPhysics::WeaponQueryScope::WeaponQueryScope(void* owner,void* held):previousOwner(shotOwner),previousWeapon(shotWeapon)
{shotOwner=owner;shotWeapon=held;}
VRHandPhysics::WeaponQueryScope::~WeaponQueryScope() {shotOwner=previousOwner;shotWeapon=previousWeapon;}

void VRHandPhysics::PublishBlade(void* owner,void* held,const VRMath::Vector* world,const VRMath::Vector* tracked,uint64_t sampledAt,const VRMelee::Profile& profile)
{
    if(!enabled || !combatEnabled || !owner || !held || !world || !tracked || !sampledAt ||
        !VRMelee::Supported(profile.type) || profile.samples<2 || profile.samples>VRMelee::SampleCount ||
        !std::isfinite(profile.radius) || profile.radius<=0 || profile.radius>.1f) return;
    for(unsigned i=0;i<profile.samples;++i) for(unsigned axis=0;axis<3;++axis)
        if(!std::isfinite(world[i][axis]+tracked[i][axis])) return;
    AcquireSRWLockExclusive(&poseLock);
    if(desired.owner==owner && desired.held==held && desired.at[1] &&
        VRMath::Length3(VRMath::Difference3(world[0],desired.wrists[1][3]))<.7f)
    {
        desired.bladeEntity=held;desired.bladeAt=GetTickCount64();desired.bladeSample=sampledAt;
        desired.meleeProfile=profile;
        std::copy_n(world,VRMelee::SampleCount,desired.bladeWorld.begin());
        std::copy_n(tracked,VRMelee::SampleCount,desired.bladeTracked.begin());
    }
    ReleaseSRWLockExclusive(&poseLock);
}

bool VRHandPhysics::Resolve(void* owner,unsigned hand,VRMath::Matrix& wrist)
{
    if(!enabled || hand>1) return false;
    Feedback pose{};
    AcquireSRWLockShared(&poseLock); pose=feedback; ReleaseSRWLockShared(&poseLock);
    if(pose.owner!=owner || !pose.at[hand] || GetTickCount64()-pose.at[hand]>100 ||
        VRMath::Length3(VRMath::Difference3(wrist[3],pose.wrists[hand][3]))>recoveryDistance) return false;
    if(pose.rotation[hand]) for(unsigned row=0;row<3;++row) for(unsigned axis=0;axis<3;++axis) wrist[row][axis]=pose.wrists[hand][row][axis];
    for(unsigned axis=0;axis<3;++axis) wrist[3][axis]=pose.wrists[hand][3][axis];
    return true;
}

std::string VRHandPhysics::Status()
{
    char value[460];
    sprintf_s(value,"{\"enabled\":%s,\"ready\":%s,\"failed\":%s,\"activeShapes\":%u,\"created\":%u,\"removed\":%u,\"updates\":%u,\"excludedPairs\":%u,\"excludedQueries\":%u,\"maxTargetError\":%.4f,\"armForceLimit\":%.1f,\"recoveryDistance\":%.2f,\"recoveries\":%u,\"limitedDrives\":%u}",
        enabled ? "true":"false",(pairHook && queryHook) ? "true":"false",failed ? "true":"false",active.load(),created.load(),removed.load(),updates.load(),excludedPairs.load(),excludedQueries.load(),maxError.load(),armForce,recoveryDistance,recoveries.load(),limitedDrives.load());
    std::string result=value;result.pop_back();
    return result+",\"shotFilterReady\":"+(rayHook ? "true":"false")+
        ",\"excludedShotQueries\":"+std::to_string(excludedShotQueries.load())+
        ",\"grabEnabled\":"+(grabbingEnabled ? "true":"false")+
        ",\"grabbing\":["+(grabbing[0] ? std::string("true"):std::string("false"))+","+(grabbing[1] ? "true":"false")+
        "],\"grabAttempts\":"+std::to_string(grabAttempts.load())+",\"grabStarts\":"+std::to_string(grabStarts.load())+
        ",\"grabReleases\":"+std::to_string(grabReleases.load())+",\"grabRejected\":"+std::to_string(grabRejected.load())+
        ",\"grabMass\":"+std::to_string(grabMass.load())+",\"grabRadius\":"+std::to_string(grabRadius.load())+
        ",\"grabInertiaEstimate\":"+std::to_string(grabInertia.load())+",\"grabForceLimit\":"+std::to_string(grabForce)+
        ",\"gripSamples\":["+std::to_string(gripSamples[0].load())+","+std::to_string(gripSamples[1].load())+
        "],\"grabHits\":"+std::to_string(grabHits.load())+",\"grabController\":\"full-pose\",\"vehicleGrabs\":false,\"grabPositionError\":"+std::to_string(grabPositionError.load())+
        ",\"grabAngleErrorRadians\":"+std::to_string(grabAngleError.load())+",\"grabAngularSpeed\":"+std::to_string(grabAngularSpeed.load())+
        ",\"grabTorque\":"+std::to_string(grabTorque.load())+",\"twoHandUpdates\":"+std::to_string(twoHandUpdates.load())+
        ",\"grabUnsafeReleases\":"+std::to_string(grabUnsafeReleases.load())+
        ",\"throws\":"+std::to_string(throws.load())+",\"throwSpeed\":"+std::to_string(throwSpeed.load())+
        ",\"doorAdapter\":"+(requestDoor ? "true":"false")+
        ",\"ragdollAdapter\":\"native-component-point-grip\",\"ragdollGrabbing\":["+
        (ragdollGrabbing[0] ? std::string("true"):std::string("false"))+","+(ragdollGrabbing[1] ? "true":"false")+
        "],\"ragdollStarts\":"+std::to_string(ragdollStarts.load())+",\"ragdollUpdates\":"+std::to_string(ragdollUpdates.load())+
        ",\"ragdollCandidates\":"+std::to_string(ragdollCandidates.load())+",\"ragdollHits\":"+std::to_string(ragdollHits.load())+
        ",\"ragdollRejected\":"+std::to_string(ragdollRejected.load())+",\"ragdollPart\":"+std::to_string(ragdollPart.load())+
        ",\"ragdollMass\":"+std::to_string(ragdollMass.load())+",\"ragdollPositionError\":"+std::to_string(ragdollError.load())+
        ",\"ragdollWakeAttempts\":"+std::to_string(ragdollWakeAttempts.load())+",\"ragdollWakeSuccesses\":"+std::to_string(ragdollWakeSuccesses.load())+
        ",\"ragdollLostPhysics\":"+std::to_string(ragdollLostPhysics.load())+",\"ragdollLostFrame\":"+std::to_string(ragdollLostFrame.load())+
        ",\"ragdollActivityReady\":"+(ragdollActivity ? "true":"false")+",\"ragdollKeepAlive\":"+std::to_string(ragdollKeepAlive.load())+
        ",\"ragdollWakeReady\":"+(ragdollPermission && ragdollActivate && ragdollSettleHook ? "true":"false")+
        ",\"ragdollSettleDeferrals\":"+std::to_string(ragdollSettleDeferrals.load())+
        ",\"ragdollDeactivations\":"+std::to_string(ragdollDeactivations.load())+",\"ragdollDeactivateCallerVA\":"+std::to_string(ragdollDeactivateCaller.load())+
        ",\"ragdollDeactivateFlags\":"+std::to_string(ragdollDeactivateFlags.load())+",\"ragdollDeactivateState\":"+std::to_string(ragdollDeactivateState.load())+
        ",\"grabOverlapRejections\":"+std::to_string(grabOverlapRejections.load())+
        ",\"doorGrabbing\":["+(doorGrabbing[0] ? std::string("true"):std::string("false"))+","+(doorGrabbing[1] ? "true":"false")+
        "],\"doorStarts\":"+std::to_string(doorStarts.load())+",\"doorUpdates\":"+std::to_string(doorUpdates.load())+
        ",\"doorLockedBlocks\":"+std::to_string(doorLocked.load())+",\"doorMisses\":"+std::to_string(doorMisses.load())+
        ",\"doorFraction\":"+std::to_string(doorFraction.load())+",\"doorTargetFraction\":"+std::to_string(doorTarget.load())+
        ",\"doorFlagsBeforeRequest\":"+std::to_string(doorFlagsBefore.load())+
        ",\"doorFlagsAfterRequest\":"+std::to_string(doorFlagsAfter.load())+
        ",\"doorLockState\":"+std::to_string(doorLockState.load())+
        ",\"doorHasPhysicsChild\":"+(doorHasPhysicsChild ? "true":"false")+
        ",\"doorLatchClearFailures\":"+std::to_string(doorLatchClearFailures.load())+
        ",\"selectedDoorIndex\":"+std::to_string(selectedDoorIndex.load())+
        ",\"selectedDoorRecordId\":"+std::to_string(selectedDoorRecordId.load())+
        ",\"selectedDoorCollisionPart\":"+std::to_string(selectedDoorCollisionPart.load())+
        ",\"grabProxyQueryRejections\":"+std::to_string(grabProxyQueryRejections.load())+
        ",\"doorQueryNoHit\":"+std::to_string(doorQueryNoHit.load())+
        ",\"doorQueryWrongPart\":"+std::to_string(doorQueryWrongPart.load())+
        ",\"lastDoorQueryPart\":"+std::to_string(lastDoorQueryPart.load())+
        ",\"handSearchHits\":["+std::to_string(handSearchHits[0].load())+","+std::to_string(handSearchHits[1].load())+","+std::to_string(handSearchHits[2].load())+"]"+
        ",\"blockedContactNodes\":"+std::to_string(blockedContactNodes.load())+
        ",\"palmColliderError\":"+std::to_string(palmColliderError.load())+
        ",\"contactDrivePeak\":"+std::to_string(contactDrivePeak.load())+"}";
}

std::string VRHandPhysics::CombatStatus()
{
    char value[700];
    sprintf_s(value,"{\"enabled\":%s,\"ready\":%s,\"speedThreshold\":%.2f,\"damage\":%.1f,\"fists\":[%s,%s],\"speed\":[%.3f,%.3f],\"sweeps\":%u,\"contacts\":%u,\"hits\":[%u,%u],\"lastNativeDamage\":%.2f,\"lastTarget\":%u,\"meleeEvents\":%u,\"nativeImpacts\":%u}",
        combatEnabled ? "true":"false",(enabled && !failed && meleeDamage && grabQuery && meleeEventCtor && meleeEventDtor && globalEventGroup && addEvent && weaponCtor && weaponSetType && weaponDtor && weaponImpact && weaponDispatch) ? "true":"false",punchSpeed,punchDamage,
        fists[0] ? "true":"false",fists[1] ? "true":"false",fistSpeed[0].load(),fistSpeed[1].load(),
        punchSweeps.load(),punchContacts.load(),punchHits[0].load(),punchHits[1].load(),lastPunchDamage.load(),lastPunchTarget.load(),meleeEvents.load(),meleeImpacts.load());
    std::string result=value;result.pop_back();
    char blade[640];
    sprintf_s(blade,",\"knifeReady\":%s,\"knifeSpeed\":%.3f,\"knifeThreshold\":%.2f,\"knifeDamage\":%.1f,\"knifeSweeps\":%u,\"knifeContacts\":%u,\"knifeHits\":%u}",
        bladeReady ? "true":"false",bladeSpeed.load(),knifeSpeed,knifeDamage,bladeSweeps.load(),bladeContacts.load(),bladeHits.load());
    result+=blade;result.pop_back();
    sprintf_s(blade,",\"worldMeleeCalls\":%u,\"vehicleMeleeCalls\":%u,\"objectMeleeCalls\":%u,\"lastWorldMeleeDamage\":%.2f}",
        worldMeleeCalls.load(),vehicleMeleeCalls.load(),objectMeleeCalls.load(),lastWorldMeleeDamage.load());
    result+=blade;result.pop_back();
    sprintf_s(blade,",\"knifeMissSteps\":%u,\"knifeRearmBlocked\":%u,\"knifeCooldownBlocked\":%u,\"knifeTrackingResets\":%u,\"knifeNativeRejected\":%u,\"knifeNonLivingContacts\":%u}",
        bladeMisses.load(),bladeRearmBlocks.load(),bladeCooldownBlocks.load(),bladeTrackingResets.load(),bladeNativeRejects.load(),bladeNonLivingContacts.load());
    result+=blade;result.pop_back();
    sprintf_s(blade,",\"meleeWeaponType\":%u,\"meleeSamples\":%u,\"meleeRadius\":%.3f,\"clubThreshold\":%.2f,\"clubDamage\":%.1f}",
        meleeType.load(),meleeSamples.load(),meleeRadius.load(),clubSpeed,clubDamage);
    return result+blade;
}

void VRHandPhysics::Install()
{
    CIniReader ini("");
    if(!ini.ReadInteger("VR","ExperimentalHandPhysics",0)) return;
    combatEnabled=ini.ReadInteger("VR","ExperimentalHandCombat",0)!=0;
    if(combatEnabled)
    {
        punchSpeed=ini.ReadFloat("VR","HandPunchSpeed",1.6f);
        punchDamage=ini.ReadFloat("VR","HandPunchDamage",25.f);
        punchSpeed=std::isfinite(punchSpeed) ? std::clamp(punchSpeed,.8f,5.f):1.6f;
        punchDamage=std::isfinite(punchDamage) ? std::clamp(punchDamage,1.f,100.f):25.f;
        knifeSpeed=ini.ReadFloat("VR","HandKnifeSpeed",1.2f);
        knifeDamage=ini.ReadFloat("VR","HandKnifeDamage",25.f);
        knifeSpeed=std::isfinite(knifeSpeed) ? std::clamp(knifeSpeed,.5f,5.f):1.2f;
        knifeDamage=std::isfinite(knifeDamage) ? std::clamp(knifeDamage,1.f,100.f):25.f;
        clubSpeed=ini.ReadFloat("VR","HandClubSpeed",1.2f);
        clubDamage=ini.ReadFloat("VR","HandClubDamage",30.f);
        clubSpeed=std::isfinite(clubSpeed) ? std::clamp(clubSpeed,.5f,5.f):1.2f;
        clubDamage=std::isfinite(clubDamage) ? std::clamp(clubDamage,1.f,100.f):30.f;
        auto damage=hook::pattern("55 8B EC 83 E4 F0 81 EC D8 00 00 00 56 57 FF 75 10 E8 ? ? ? ? 83 C4 04 89 44 24 4C 85 C0 75 08 D9 EE 5F");
        if(damage.size()==1) meleeDamage=reinterpret_cast<PedDamage>(damage.get_first());
        auto ctor=hook::pattern("56 8B F1 E8 ? ? ? ? C7 46 5C 00 00 00 00 C7 46 60 00 00 00 00 C7 46 18 00 00 00 00");
        if(ctor.size()==1) weaponCtor=reinterpret_cast<WeaponCtor>(ctor.get_first());
        auto setType=hook::pattern("8B 44 24 04 56 8B F1 B9 FF 7F 00 00 89 46 18 8B 44 24 0C 0F B7 D0 66 3B C8");
        if(setType.size()==1) weaponSetType=reinterpret_cast<WeaponSetType>(setType.get_first());
        auto impact=hook::pattern("55 8B EC 83 E4 F0 83 EC 78 56 57 8B 7D 10 8B F1 FF 37 E8 ? ? ? ? F3 0F 10 47 10 FF 76 18");
        if(impact.size()==1) weaponImpact=reinterpret_cast<WeaponImpact>(impact.get_first());
        auto dispatch=hook::pattern("83 EC 30 83 C8 FF 89 44 24 04 33 C0 56 0F 57 C0 57 89 4C 24 30 C7 44 24 1C 00 00 00 00 F3 0F 11 44 24 34");
        if(dispatch.size()==1) weaponDispatch=reinterpret_cast<WeaponDispatch>(dispatch.get_first());
        auto dtor=hook::pattern("E8 ? ? ? ? DD D8 83 C4 24 8D 4C 24 10 E8 ? ? ? ? 80 4E 5C 80 5F 5E 8B E5 5D C2 04 00");
        if(dtor.size()==1) weaponDtor=reinterpret_cast<decltype(weaponDtor)>(injector::GetBranchDestination(dtor.get_first(14)).as_int());
        // Verified CCF588..CCF5BF: native melee event construction, global
        // broadcast and destruction. Resolve all calls from one unique site.
        auto event=hook::pattern("FF 74 24 40 8D 4C 24 0C FF 77 1C 53 E8 ? ? ? ? 6A 01 8D 44 24 0C 6A 00 50 E8 ? ? ? ? 8B C8 E8 ? ? ? ? 53 6A 16 B9 ? ? ? ? E8 ? ? ? ? 8D 4C 24 08 E8");
        if(event.size()==1)
        {
            meleeEventCtor=reinterpret_cast<MeleeEventCtor>(injector::GetBranchDestination(event.get_first(12)).as_int());
            globalEventGroup=reinterpret_cast<GlobalEventGroup>(injector::GetBranchDestination(event.get_first(26)).as_int());
            addEvent=reinterpret_cast<AddEvent>(injector::GetBranchDestination(event.get_first(33)).as_int());
            meleeEventDtor=reinterpret_cast<EventDtor>(injector::GetBranchDestination(event.get_first(55)).as_int());
        }
    }
    armForce=ini.ReadFloat("VR","HandPhysicsArmForce",250.f);
    grabForce=ini.ReadFloat("VR","HandGrabForce",900.f);
    acquisitionRadius=ini.ReadFloat("VR","HandGrabRadius",.22f);
    recoveryDistance=ini.ReadFloat("VR","HandPhysicsRecoveryDistance",.75f);
    if(!std::isfinite(armForce)) armForce=250.f;
    if(!std::isfinite(grabForce)) grabForce=900.f;
    if(!std::isfinite(acquisitionRadius)) acquisitionRadius=.22f;
    if(!std::isfinite(recoveryDistance)) recoveryDistance=.75f;
    armForce=std::clamp(armForce,25.f,1000.f);
    grabForce=std::clamp(grabForce,25.f,3000.f);
    acquisitionRadius=std::clamp(acquisitionRadius,.05f,.35f);
    recoveryDistance=std::clamp(recoveryDistance,.4f,1.5f);
    const bool wantGrabbing=ini.ReadInteger("VR","ExperimentalHandGrab",0)!=0;
    if(wantGrabbing || combatEnabled)
    {
        auto volume=hook::pattern("8B 4C 24 10 53 8B 5C 24 1C 56 33 F6 85 C9 74 39 8B 41 28 C1 E8 06 83 E0 0F 83 F8 03 75 17 F6 C3");
        if(volume.size()==1) grabQuery=reinterpret_cast<VolumeQuery>(volume.get_first());
    }
    if(wantGrabbing)
    {
        grabbingEnabled=grabQuery!=nullptr;
        auto activity=hook::pattern("8B 44 24 04 89 81 B8 07 00 00 A1 ? ? ? ? 89 81 BC 07 00 00 C2 08 00");
        if(activity.size()==1) ragdollActivity=reinterpret_cast<RagdollActivity>(activity.get_first());
        auto permission=hook::pattern("55 8B EC 83 E4 F0 83 EC 48 56 57 E8 ? ? ? ? 84 C0 74 31 8B 7D 08 8B 75 0C 8B 87 6C 02 00 00");
        auto activate=hook::pattern("83 EC 24 56 57 8B F9 F6 87 6C 02 00 00 04 0F 84 ? ? ? ? 8B 87 30 0B 00 00 85 C0");
        auto settle=hook::pattern("55 8B EC 83 E4 F0 81 EC 18 01 00 00 56 8B 75 08 57 8B F9 6A 00 8D 84 24 84 00 00 00 50 8B CE 89 7C 24 20 E8");
        if(permission.size()==1 && activate.size()==1 && settle.size()==1)
        {
            ragdollSettleHook=safetyhook::create_inline(settle.get_first(),SettleRagdoll);
            if(ragdollSettleHook)
            {
                ragdollPermission=reinterpret_cast<RagdollPermission>(permission.get_first());
                ragdollActivate=reinterpret_cast<RagdollActivate>(activate.get_first());
            }
        }
        auto deactivate=hook::pattern("55 8B EC 83 E4 F0 81 EC B8 00 00 00 56 57 8B F9 89 7C 24 14 83 BF B8 07 00 00 02 8B 87 B0 07 00 00 89 47 38");
        if(deactivate.size()==1) ragdollDeactivateHook=safetyhook::create_inline(deactivate.get_first(),TraceRagdollDeactivate);
        auto door=hook::pattern("8B 41 14 8B 51 10 F3 0F 10 44 24 04 89 41 18 8B C2 C1 E8 12 A8 01 8B 44 24 08 F3 0F 11 41 14 75 44");
        auto doorLayout=hook::pattern("55 8B EC 83 E4 F0 81 EC B8 00 00 00 56 8B 75 08 57 8B F9 81 67 10 FF FF FF FE 8B 86 C4 0D 00 00");
        if(door.size()==1 && doorLayout.size()==1) requestDoor=reinterpret_cast<DoorRequest>(door.get_first());
    }
    auto filter=hook::pattern("53 8B D9 B8 00 01 00 00 66 85 43 0A 74 06 32 C0 5B C2 04 00");
    // Resolve from the dispatcher call: several narrowphase variants share
    // the same prologue, so that prologue alone is not a unique signature.
    auto query=hook::pattern("F3 0F 11 47 70 F3 0F 11 57 74 F3 0F 11 5F 78 8B 46 04 FF 70 0C E8 ? ? ? ? 84 C0 74 11 6A 00 8D 46 10 50 8B CF");
    if(filter.size()!=1 || query.size()!=1) {failed=true; return;}
    pairHook=safetyhook::create_inline(filter.get_first(),CanCollide);
    if(!pairHook) {failed=true; return;}
    auto queryTarget=injector::GetBranchDestination(query.get_first(21)).as_int();
    queryHook=safetyhook::create_inline(reinterpret_cast<void*>(queryTarget),QueryBound);
    if(!queryHook) {pairHook.reset(); failed=true; return;}
    auto ray=hook::pattern("56 FF 75 20 8B CF E8 ? ? ? ? 84 C0 74 11 6A 00 8D 46 10 50 8B CF E8 ? ? ? ? 84 C0");
    if(ray.size()==1) rayHook=safetyhook::create_inline(reinterpret_cast<void*>(injector::GetBranchDestination(ray.get_first(6)).as_int()),RayBound);
    if(grabbingEnabled)
    {
        // Unique caller bytes at CE739E57 resolve the stationary narrowphase;
        // its prologue is shared by other bound-query variants.
        auto stationary=hook::pattern("F3 0F 11 47 74 F3 0F 10 47 78 F3 0F 58 C1 F3 0F 11 47 78 8B 43 04 FF 70 0C E8 ? ? ? ? 84 C0 74 11 6A 00 8D 43 10 50 8B CF");
        if(stationary.size()==1)
        {
            auto target=injector::GetBranchDestination(stationary.get_first(25)).as_int();
            overlapHook=safetyhook::create_inline(reinterpret_cast<void*>(target),GrabOverlapBound);
        }
        if(!overlapHook) {queryHook.reset();pairHook.reset();failed=true;return;}
    }
    enabled=true;
    FusionFix::onGameProcessEvent() += Process;
}
