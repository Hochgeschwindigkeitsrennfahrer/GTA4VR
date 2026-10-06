module;
#include <common.hxx>
#include "includes/vrmath.hpp"
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
    constexpr unsigned NodeCount=10;
    void Install();
    void Publish(void* owner,void* held,unsigned hand,const VRMath::Vector* points);
    bool Resolve(void* owner,unsigned hand,VRMath::Matrix& wrist);
    std::string Status();
}

namespace
{
    SafetyHookInline pairHook{},queryHook{};
    bool enabled=false;
    std::atomic<bool> failed=false;
    uint32_t model=0;
    bool requestedModel=false;
    uint64_t requestedAt=0;
    SRWLOCK poseLock=SRWLOCK_INIT;
    struct Pose {void* owner=nullptr; void* held=nullptr; uint64_t at[2]{}; VRMath::Vector points[2][VRHandPhysics::NodeCount]{};} desired;
    struct Feedback {void* owner=nullptr; uint64_t at[2]{}; VRMath::Vector wrists[2]{};} feedback;
    struct Node {Object handle=0; void* instance=nullptr; VRMath::Vector target{}; uint64_t at=0;} nodes[2][VRHandPhysics::NodeCount];
    std::atomic<void*> instances[2][VRHandPhysics::NodeCount]{};
    std::atomic<void*> player=nullptr,weapon=nullptr;
    std::atomic<unsigned> created=0,removed=0,active=0,updates=0,excludedPairs=0,recoveries=0,limitedDrives=0;
    std::atomic<unsigned> excludedQueries=0;
    float armForce=250.f,recoveryDistance=.75f;
    constexpr float NodeMass=.15f,Stiffness=400.f,Damping=12.f;
    std::atomic<float> maxError=0;
    void* currentOwner=nullptr;
    VRMath::Vector lastTarget[2]{};
    bool haveTarget[2]{};

    void* Entity(Object handle)
    {
        auto* pool=CObject::GetObjectsPool();
        const int slot=handle>>8;
        if(!pool || !handle || slot<0 || slot>=pool->GetSize() || !pool->m_aStorage || !pool->m_aFlags) return nullptr;
        if(pool->m_nStorageSize<4 || pool->m_nStorageSize>16384) return nullptr;
        auto* storage=pool->GetAt(handle);
        return storage && pool->m_nStorageSize==4 ? *reinterpret_cast<void**>(storage):storage;
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
        const bool selfProxy=IsProxy(self),otherProxy=IsProxy(other);
        if(selfProxy || otherProxy)
        {
            auto* counterpart=selfProxy ? other:self;
            auto* entity=counterpart ? *reinterpret_cast<void**>(static_cast<char*>(counterpart)+0xC):nullptr;
            const bool ped=entity && ((*reinterpret_cast<uint32_t*>(static_cast<char*>(entity)+0x28)>>6)&15)==3;
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
        if(IsProxy(instance)) {++excludedQueries; return false;}
        return queryHook.thiscall<bool>(query,bound,instance);
    }

    void ClearHand(unsigned hand)
    {
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
        AcquireSRWLockExclusive(&poseLock); feedback.at[hand]=0; ReleaseSRWLockExclusive(&poseLock);
    }

    void Clear()
    {
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
        Natives::SetObjectPhysicsParams(node.handle,NodeMass,-1.f,-1.f,-1.f,-1.f,-1.f,-1.f,-1.f,0xbf800000,0xbf800000);
        if(*reinterpret_cast<uint16_t*>(static_cast<char*>(node.instance)+8)==0xffff) return false;
        node.target=target; node.at=GetTickCount64();
        ++created;
        return true;
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
        Feedback result{}; result.owner=pose.owner;
        float dt=0; Natives::GetFrameTime(&dt);
        if(!std::isfinite(dt) || dt<=0) {Clear(); return;}
        dt=std::clamp(dt,1.f/240.f,1.f/20.f);
        float error=0;
        for(unsigned hand=0;hand<2;++hand)
        {
            if(haveTarget[hand] && VRMath::Length3(VRMath::Difference3(pose.points[hand][0],lastTarget[hand]))>recoveryDistance)
            {ClearHand(hand); ++recoveries;}
            lastTarget[hand]=pose.points[hand][0]; haveTarget[hand]=true;
            for(unsigned i=0;i<VRHandPhysics::NodeCount;++i)
            {
                auto& node=nodes[hand][i];
                const auto& target=pose.points[hand][i];
                if(!node.handle && !Create(hand,i,target)) {failed=true; Clear(); return;}
                auto* entity=Entity(node.handle);
                if(!entity || *reinterpret_cast<void**>(static_cast<char*>(entity)+0x38)!=node.instance)
                {failed=true; Clear(); return;}
                VRMath::Vector position{};
                Natives::GetObjectCoordinates(node.handle,&position[0],&position[1],&position[2]);
                const float distance=VRMath::Length3(VRMath::Difference3(target,position));
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
                const float divisor=1.f+Damping*dt/NodeMass+Stiffness*dt*dt/NodeMass;
                for(unsigned axis=0;axis<3;++axis)
                    force[axis]=(Stiffness*(target[axis]-position[axis]-velocity[axis]*dt)+Damping*(targetVelocity[axis]-velocity[axis]))/divisor;
                force[2]+=NodeMass*9.81f;
                const float strength=VRMath::Length3(force),limit=armForce/VRHandPhysics::NodeCount;
                if(!std::isfinite(strength)) {failed=true; Clear(); return;}
                if(strength>limit) {for(unsigned axis=0;axis<3;++axis) force[axis]*=limit/strength; ++limitedDrives;}
                Natives::SetObjectDynamic(node.handle,true);
                // Mode0 -> B23560 -> collider virtual+80: continuous force,
                // world axes/COM offset, no implicit mass multiplication.
                Natives::ApplyForceToObject(node.handle,0,force[0],force[1],force[2],0,0,0,0,0,0,0);
                node.target=target; node.at=now;
                if(i==0) {result.wrists[hand]=position; result.at[hand]=now;}
            }
        }
        maxError=error; ++updates;
        AcquireSRWLockExclusive(&poseLock); feedback=result; ReleaseSRWLockExclusive(&poseLock);
    }
}

void VRHandPhysics::Publish(void* owner,void* held,unsigned hand,const VRMath::Vector* points)
{
    if(!enabled || hand>1 || !points) return;
    for(unsigned i=0;i<NodeCount;++i) for(unsigned axis=0;axis<3;++axis)
        if(!std::isfinite(points[i][axis])) return;
    AcquireSRWLockExclusive(&poseLock);
    if(desired.owner!=owner) desired={};
    desired.owner=owner; desired.held=held; desired.at[hand]=GetTickCount64();
    std::memcpy(desired.points[hand],points,sizeof(desired.points[hand]));
    ReleaseSRWLockExclusive(&poseLock);
}

bool VRHandPhysics::Resolve(void* owner,unsigned hand,VRMath::Matrix& wrist)
{
    if(!enabled || hand>1) return false;
    Feedback pose{};
    AcquireSRWLockShared(&poseLock); pose=feedback; ReleaseSRWLockShared(&poseLock);
    if(pose.owner!=owner || !pose.at[hand] || GetTickCount64()-pose.at[hand]>100 ||
        VRMath::Length3(VRMath::Difference3(wrist[3],pose.wrists[hand]))>recoveryDistance) return false;
    for(unsigned axis=0;axis<3;++axis) wrist[3][axis]=pose.wrists[hand][axis];
    return true;
}

std::string VRHandPhysics::Status()
{
    char value[460];
    sprintf_s(value,"{\"enabled\":%s,\"ready\":%s,\"failed\":%s,\"activeShapes\":%u,\"created\":%u,\"removed\":%u,\"updates\":%u,\"excludedPairs\":%u,\"excludedQueries\":%u,\"maxTargetError\":%.4f,\"armForceLimit\":%.1f,\"recoveryDistance\":%.2f,\"recoveries\":%u,\"limitedDrives\":%u}",
        enabled ? "true":"false",(pairHook && queryHook) ? "true":"false",failed ? "true":"false",active.load(),created.load(),removed.load(),updates.load(),excludedPairs.load(),excludedQueries.load(),maxError.load(),armForce,recoveryDistance,recoveries.load(),limitedDrives.load());
    return value;
}

void VRHandPhysics::Install()
{
    CIniReader ini("");
    if(!ini.ReadInteger("VR","ExperimentalHandPhysics",0)) return;
    armForce=ini.ReadFloat("VR","HandPhysicsArmForce",250.f);
    recoveryDistance=ini.ReadFloat("VR","HandPhysicsRecoveryDistance",.75f);
    if(!std::isfinite(armForce)) armForce=250.f;
    if(!std::isfinite(recoveryDistance)) recoveryDistance=.75f;
    armForce=std::clamp(armForce,25.f,1000.f);
    recoveryDistance=std::clamp(recoveryDistance,.4f,1.5f);
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
    enabled=true;
    FusionFix::onGameProcessEvent() += Process;
}
