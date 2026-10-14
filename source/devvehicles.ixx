module;
#include <common.hxx>
#include "includes/vrdevlocation.hpp"
#include <mutex>
#include <chrono>
export module devvehicles;
import common;
import comvars;
import natives;
import vrcamera;
import openxrvr;

export namespace VRDevVehicles
{
    enum class Operation {Anchor,Spawn,Remove,HelicopterPoint,RailExit};
    std::string Queue(Operation operation,unsigned model=0,VRDevLocation::Position position={});
    std::string Status();
    void Install();
}
namespace
{
    std::mutex lock;
    VRDevVehicles::Operation operation=VRDevVehicles::Operation::Anchor;
    VRDevLocation::Position anchor{},requestedAnchor{},spawnPosition{};
    VRDevLocation::Position helicopterPoint{};
    bool helicopterPointValid=false;
    bool anchorValid=false,busy=false,valid=false,requested=false;
    unsigned id=0,model=0,ownedModel=0,episode=0;
    Vehicle owned=0;
    bool ownedRail=false,railAboard=false;unsigned railHealth=0,railWantedLevel=0;
    VRDevLocation::Position railExit{};
    std::atomic<bool> railWantedSuppressed=false;unsigned railPreviousMaxWanted=6;
    SafetyHookInline railWantedHook{},railWantedNoDropHook{};
    void __cdecl RailWanted(NativeContext* context)
    {
        auto* arguments=static_cast<unsigned*>(context->GetArgumentBuffer());const auto level=arguments[1];
        if(railWantedSuppressed)arguments[1]=0;
        railWantedHook.unsafe_ccall(context);arguments[1]=level;
    }
    void __cdecl RailWantedNoDrop(NativeContext* context)
    {
        auto* arguments=static_cast<unsigned*>(context->GetArgumentBuffer());const auto level=arguments[1];
        if(railWantedSuppressed)arguments[1]=0;
        railWantedNoDropHook.unsafe_ccall(context);arguments[1]=level;
    }
    bool EnsureRailProtection()
    {
        if(!railWantedHook)
            if(auto handler=NativeInvoke::GetNativeHandler(std::to_underlying(Natives::NativeHashes::ALTER_WANTED_LEVEL)))
                railWantedHook=safetyhook::create_inline(reinterpret_cast<void*>(handler),RailWanted);
        if(!railWantedNoDropHook)
            if(auto handler=NativeInvoke::GetNativeHandler(std::to_underlying(Natives::NativeHashes::ALTER_WANTED_LEVEL_NO_DROP)))
                railWantedNoDropHook=safetyhook::create_inline(reinterpret_cast<void*>(handler),RailWantedNoDrop);
        return railWantedHook && railWantedNoDropHook;
    }
    void ProtectRailPlayer()
    {
        Natives::SetMaxWantedLevel(0);Natives::ClearWantedLevel(Natives::GetPlayerId());
        Natives::SetPoliceIgnorePlayer(Natives::GetPlayerId(),true);
        Natives::SetPlayerInvincible(Natives::GetPlayerId(),true);
    }
    void RestoreRailWanted()
    {if(railWantedSuppressed){railWantedSuppressed=false;Natives::SetMaxWantedLevel(railPreviousMaxWanted);
        Natives::SetPoliceIgnorePlayer(Natives::GetPlayerId(),false);Natives::SetPlayerInvincible(Natives::GetPlayerId(),false);}}

    uint64_t startedAt=0;
    const char* error="not requested";
    std::string label;
    bool Occupied(Vehicle car)
    {
        Ped ped=0;Natives::GetDriverOfCar(car,&ped);
        if(ped && Natives::DoesCharExist(ped))return true;
        unsigned passengers=0;Natives::GetMaximumNumberOfPassengers(car,&passengers);
        if(passengers>32)return true; // Invalid seat layout must not trigger unbounded cleanup.
        for(unsigned seat=0;seat<passengers;++seat)
            if(!Natives::IsCarPassengerSeatFree(car,seat))return true;
        return false;
    }
    bool RemoveOwned()
    {
        if(!owned || !Natives::DoesVehicleExist(owned)) {owned=0;ownedRail=false;RestoreRailWanted();return true;}
        unsigned actual=0;Natives::GetCarModel(owned,&actual);
        if(actual!=ownedModel){owned=0;error="test vehicle handle changed; cleanup skipped";return false;}
        if(Occupied(owned)){error="exit the test vehicle before advancing";return false;}
        if(ownedRail)Natives::DeleteMissionTrain(&owned);else Natives::DeleteCar(&owned);ownedRail=false;RestoreRailWanted();return true;
    }
    void Complete(const char* problem="")
    {
        error=problem;valid=!*problem;busy=false;
        if(requested && model && operation==VRDevVehicles::Operation::Spawn)Natives::MarkModelAsNoLongerNeeded(model);
        requested=false;
    }
    void Process()
    {
        std::lock_guard guard(lock);
        if(railWantedSuppressed && !Natives::GetMissionFlag())ProtectRailPlayer();
        // Test-only control of the train created by this helper. Ambient and
        // mission trains are never followed or modified.
        if(ownedRail && owned && Natives::DoesVehicleExist(owned) && !Natives::GetMissionFlag())
        {
            Ped rider=0;Natives::GetPlayerChar(Natives::GetPlayerId(),&rider);
            Vehicle seated=0;if(rider && (Natives::IsCharInAnyCar(rider) || Natives::IsCharInAnyTrain(rider)))Natives::GetCarCharIsUsing(rider,&seated);
            railAboard=seated==owned;
            if(rider)Natives::GetCharHealth(rider,&railHealth);
            Natives::StoreWantedLevel(Natives::GetPlayerId(),&railWantedLevel);
            OpenXRVR::ControllerInput input{};float speed=0;
            if(seated==owned && OpenXRVR::HasFocusedViews() && OpenXRVR::GetControllerInput(input) &&
                !input.phoneActive && !input.scriptMenuActive && !(CTimer::ms_bUserPause && *CTimer::ms_bUserPause))
                speed=(input.accelerate-input.brake)*8.f;
            Natives::SetTrainCruiseSpeed(owned,speed);Natives::SetTrainSpeed(owned,speed);
        }
        if(railWantedSuppressed && (Natives::GetMissionFlag() || !owned || !Natives::DoesVehicleExist(owned)))RestoreRailWanted();
        if(!busy)return;
        using H=Natives::NativeHashes;
        for(auto hash:{H::GET_PLAYER_ID,H::GET_PLAYER_CHAR,H::IS_PLAYER_PLAYING,H::GET_MISSION_FLAG,
            H::HAS_CUTSCENE_LOADED,H::HAS_CUTSCENE_FINISHED,H::DOES_CHAR_EXIST,H::IS_CHAR_DEAD,
            H::IS_CHAR_IN_ANY_CAR,H::IS_CHAR_GETTING_IN_TO_A_CAR,H::IS_MODEL_IN_CDIMAGE,H::IS_THIS_MODEL_A_VEHICLE,
            H::IS_THIS_MODEL_A_TRAIN,H::IS_THIS_MODEL_A_BOAT,H::IS_THIS_MODEL_A_HELI,H::REQUEST_MODEL,H::HAS_MODEL_LOADED,
            H::CREATE_CAR,H::DOES_VEHICLE_EXIST,H::GET_CAR_MODEL,H::GET_CAR_COORDINATES,H::GET_CAR_HEADING,
            H::GET_DRIVER_OF_CAR,H::GET_MAXIMUM_NUMBER_OF_PASSENGERS,H::IS_CAR_PASSENGER_SEAT_FREE,H::DELETE_CAR,
            H::SET_CAR_HEADING,H::SET_CAR_ON_GROUND_PROPERLY,H::LOCK_CAR_DOORS,H::MARK_MODEL_AS_NO_LONGER_NEEDED,
            H::REQUEST_COLLISION_AT_POSN,H::SET_CHAR_COORDINATES_NO_OFFSET,H::SET_CHAR_HEADING,H::GET_CURRENT_EPISODE,
            H::GET_DISPLAY_NAME_FROM_VEHICLE_MODEL,H::SET_TIME_OF_DAY})
            if(!NativeInvoke::GetNativeHandler(std::to_underlying(hash))) {error="native handler unavailable";valid=false;busy=false;return;}
        if(!CTimer::ms_bUserPause || !CTimer::ms_bScriptPause || *CTimer::ms_bUserPause || *CTimer::ms_bScriptPause ||
            !Natives::IsPlayerPlaying(Natives::GetPlayerId()) || Natives::GetMissionFlag() ||
            (Natives::HasCutsceneLoaded() && !Natives::HasCutsceneFinished()))
        {Complete("requires unpaused free roam");return;}
        Ped player=0;Natives::GetPlayerChar(Natives::GetPlayerId(),&player);
        if(!player || !Natives::DoesCharExist(player) || Natives::IsCharDead(player))
        {Complete("requires a living player");return;}
        episode=Natives::GetCurrentEpisode();
        if(operation==VRDevVehicles::Operation::Anchor)
        {anchor=requestedAnchor;anchorValid=true;Complete();return;}
        if(operation==VRDevVehicles::Operation::HelicopterPoint)
        {helicopterPoint=requestedAnchor;helicopterPointValid=true;Complete();return;}
        if(operation==VRDevVehicles::Operation::RailExit)
        {
            Vehicle current=0;if((Natives::IsCharInAnyCar(player) || Natives::IsCharInAnyTrain(player)))Natives::GetCarCharIsUsing(player,&current);
            if(!ownedRail || !owned || (current && current!=owned)){Complete("not at the test train");return;}
            Natives::SetTrainCruiseSpeed(owned,0);Natives::SetTrainSpeed(owned,0);
            Natives::RequestCollisionAtPosn(railExit.x,railExit.y,railExit.z);
            if(current)Natives::WarpCharFromCarToCoord(player,railExit.x,railExit.y,railExit.z);
            else Natives::SetCharCoordinatesNoOffset(player,railExit.x,railExit.y,railExit.z);
            RestoreRailWanted();VRCamera::Recenter();Complete();return;
        }
        if((Natives::IsCharInAnyCar(player) || Natives::IsCharInAnyTrain(player)) || Natives::IsCharGettingInToACar(player))
        {Complete("step out of the vehicle before advancing");return;}
        if(operation==VRDevVehicles::Operation::Remove)
        {if(RemoveOwned())Complete();else {valid=false;busy=false;}return;}
        if(!anchorValid){Complete("set a vehicle test anchor first");return;}
        if(!Natives::IsModelInCdimage(model) || !Natives::IsThisModelAVehicle(model))
        {Complete("vehicle model unavailable in this episode");return;}
        const bool rail=Natives::IsThisModelATrain(model);
        if(rail && !NativeInvoke::GetNativeHandler(std::to_underlying(H::CREATE_MISSION_TRAIN)))
        {Complete("native train creation unavailable");return;}
        if(!requested)
        {
            Natives::RequestModel(model);requested=true;
            const float angle=anchor.heading*.01745329252f;
            const float separation=Natives::IsThisModelAHeli(model) ? 12.f:6.f;
            spawnPosition=anchor;spawnPosition.x+=std::cos(angle)*separation;spawnPosition.y+=std::sin(angle)*separation;
            if(Natives::IsThisModelAHeli(model) && helicopterPointValid)spawnPosition=helicopterPoint;
            if(rail)
            {
                // CE 1.2.0.59 CTrain::CreateMissionTrain native BC9A20 ->
                // C39570. Six route arrays contain 12-byte packed nodes.
                const auto base=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
                const unsigned track=model==0xC6C3242D ? 4:0;
                auto* nodes=reinterpret_cast<int16_t**>(base+0x12D11C8)[track];
                const int count=reinterpret_cast<int*>(base+0x12D1198)[track];
                if(!nodes || count<2 || count>20000){Complete("native rail route unavailable");return;}
                spawnPosition={nodes[0]*.125f,nodes[1]*.125f,nodes[2]*.03125f,0};
            }
            Natives::RequestCollisionAtPosn(spawnPosition.x,spawnPosition.y,spawnPosition.z);
        }
        if(!Natives::HasModelLoaded(model))
        {if(GetTickCount64()-startedAt>15000)Complete("vehicle model streaming timed out");return;}
        if(!RemoveOwned()){valid=false;busy=false;Natives::MarkModelAsNoLongerNeeded(model);requested=false;return;}
        if(rail)
        {
            using H=Natives::NativeHashes;
            for(auto hash:{H::DELETE_MISSION_TRAIN,H::SET_TRAIN_SPEED,H::SET_TRAIN_CRUISE_SPEED,H::SET_TRAIN_STOPS_FOR_STATIONS,H::WARP_CHAR_INTO_CAR,H::WARP_CHAR_FROM_CAR_TO_COORD,H::GET_MAX_WANTED_LEVEL,H::SET_MAX_WANTED_LEVEL,H::CLEAR_WANTED_LEVEL,H::SET_PLAYER_INVINCIBLE,H::SET_POLICE_IGNORE_PLAYER})
                if(!NativeInvoke::GetNativeHandler(std::to_underlying(hash))){Complete("rail test native unavailable");return;}
            if(!EnsureRailProtection()){Complete("rail wanted protection unavailable");return;}
            const auto base=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
            auto* consists=reinterpret_cast<int*>(base+0x12D0D58);
            const unsigned type=model==0xC6C3242D ? 9:12;
            // A scoped singleton definition exercises each catalog subway mesh
            // through the native train constructor. Restore it before returning.
            int* entry=consists+type*16;const int original=*entry;
            if((type==9 && original!=208) || (type==12 && original!=209) || entry[1]!=-1)
            {Complete("unsupported native train consist layout");return;}
            if(type==12)*entry=model==0x8B887FDB ? 210:209;
            Natives::GetMaxWantedLevel(&railPreviousMaxWanted);railWantedSuppressed=true;
            ProtectRailPlayer();
            Natives::CreateMissionTrain(type,spawnPosition.x,spawnPosition.y,spawnPosition.z,true,&owned);
            *entry=original;
            if(!owned || !Natives::DoesVehicleExist(owned)){owned=0;RestoreRailWanted();Complete("native train creation failed");return;}
            ownedRail=true;ownedModel=model;unsigned actual=0;Natives::GetCarModel(owned,&actual);
            if(actual!=model){Natives::DeleteMissionTrain(&owned);ownedRail=false;RestoreRailWanted();Complete("native train model mismatch");return;}
            Natives::SetTrainStopsForStations(owned,false);Natives::SetTrainCruiseSpeed(owned,0);Natives::SetTrainSpeed(owned,0);
            railExit=anchor; // Return to the known safe beach, not beside a moving track.
            Natives::WarpCharIntoCar(player,owned);VRCamera::Recenter();Natives::SetTimeOfDay(12,0);
            Natives::GetCarCoordinates(owned,&spawnPosition.x,&spawnPosition.y,&spawnPosition.z);
            Natives::GetCarHeading(owned,&spawnPosition.heading);
            const auto* text=Natives::GetDisplayNameFromVehicleModel(model);label=text ? text:"";
            Complete();return;
        }
        const bool placedHelicopter=Natives::IsThisModelAHeli(model) && helicopterPointValid;
        if(!placedHelicopter)
        {
            Natives::RequestCollisionAtPosn(anchor.x,anchor.y,anchor.z);
            Natives::SetCharCoordinatesNoOffset(player,anchor.x,anchor.y,anchor.z);Natives::SetCharHeading(player,anchor.heading);
            VRCamera::Recenter();
        }
        // A nearby water point can be used for boats without moving the player.
        const bool boat=Natives::IsThisModelABoat(model);
        if(boat && NativeInvoke::GetNativeHandler(std::to_underlying(H::GET_WATER_HEIGHT_NO_WAVES)))
        {
            bool found=false;
            for(float radius:{8.f,16.f,24.f,40.f,64.f})
            {
                for(unsigned direction=0;direction<16;++direction)
                {
                    const float angle=direction*.3926990817f,x=anchor.x+std::cos(angle)*radius,y=anchor.y+std::sin(angle)*radius;
                    float water=0;
                    if(Natives::GetWaterHeightNoWaves(x,y,anchor.z+10,&water) && std::isfinite(water))
                    {spawnPosition.x=x;spawnPosition.y=y;spawnPosition.z=water+.5f;found=true;break;}
                }
                if(found)break;
            }
            if(!found){Complete("no nearby water; set a boat test anchor at the water edge");return;}
        }
        Natives::CreateCar(model,spawnPosition.x,spawnPosition.y,spawnPosition.z,&owned,true);
        if(!owned || !Natives::DoesVehicleExist(owned)){owned=0;Complete("native vehicle creation failed");return;}
        ownedModel=model;Natives::SetCarHeading(owned,placedHelicopter ? helicopterPoint.heading:anchor.heading);
        if(!boat)Natives::SetCarOnGroundProperly(owned);
        Natives::LockCarDoors(owned,1);
        Natives::SetTimeOfDay(12,0);
        Natives::GetCarCoordinates(owned,&spawnPosition.x,&spawnPosition.y,&spawnPosition.z);
        Natives::GetCarHeading(owned,&spawnPosition.heading);
        const auto* text=Natives::GetDisplayNameFromVehicleModel(model);label=text ? text:"";
        Complete();
    }
}
std::string VRDevVehicles::Queue(Operation next,unsigned hash,VRDevLocation::Position position)
{
    std::lock_guard guard(lock);
    if(busy)return "{\"ok\":false,\"error\":\"vehicle test operation pending\"}";
    if((next==Operation::Anchor || next==Operation::HelicopterPoint) && !VRDevLocation::Valid(position))return "{\"ok\":false,\"error\":\"invalid test anchor\"}";
    operation=next;model=hash;requestedAnchor=position;requested=false;startedAt=GetTickCount64();
    busy=true;valid=false;error="";++id;
    return std::format("{{\"ok\":true,\"accepted\":true,\"id\":{}}}",id);
}
std::string VRDevVehicles::Status()
{
    std::lock_guard guard(lock);
    auto result=std::format("{{\"ok\":true,\"id\":{},\"pending\":{},\"valid\":{},\"error\":\"{}\",\"episode\":{},\"model\":\"{:08X}\",\"label\":\"{}\",\"vehicle\":{},\"anchorValid\":{},\"anchor\":{{\"x\":{},\"y\":{},\"z\":{},\"heading\":{}}},\"position\":{{\"x\":{},\"y\":{},\"z\":{},\"heading\":{}}}}}",
        id,busy,valid,error,episode,model,label,owned,anchorValid,anchor.x,anchor.y,anchor.z,anchor.heading,
        spawnPosition.x,spawnPosition.y,spawnPosition.z,spawnPosition.heading);
    result.pop_back();
    result+=std::format(",\"rail\":{{\"active\":{},\"protected\":{},\"aboard\":{},\"health\":{},\"wanted\":{}}}}}",ownedRail,railWantedSuppressed.load(),railAboard,railHealth,railWantedLevel);
    return result;
}
void VRDevVehicles::Install() {FusionFix::onGameProcessEvent()+=Process;}
