module;

#include <common.hxx>
#include <sddl.h>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <string>
#include <vector>
#include <atomic>
#include "includes/vrdevlocation.hpp"

export module devcontrol;

import common;
import comvars;
import openxrvr;
import vrstereo;
import vrcamera;
import vrhands;
import vrhandphysics;
import vrhud;
import vrprofile;
import natives;

export namespace DevControl { void Install(); }

namespace
{
    HANDLE pipe = INVALID_HANDLE_VALUE;
    HANDLE ioEvent = nullptr;
    OVERLAPPED io{};
    enum class Stage { Connecting, Command, Response, Acknowledge };
    Stage stage = Stage::Connecting;
    bool complete = false;
    DWORD transferred = 0;
    DWORD immediateError = 0;
    ULONGLONG operationStarted = 0;
    char input[128]{};
    std::string response;
    HWND quitWindow = nullptr;
    // RPC runs on EndScene; player natives run only on the simulation callback.
    SRWLOCK locationLock=SRWLOCK_INIT;
    enum class LocationOperation { None, Capture, Restore };
    LocationOperation locationOperation=LocationOperation::None;
    VRDevLocation::Position locationRequest{},locationResult{};
    unsigned locationId=0;
    bool locationBusy=false,locationValid=false;
    std::atomic<bool> locationQueued{false};
    const char* locationError="not captured";
    SRWLOCK meleeTestLock=SRWLOCK_INIT;
    unsigned meleeTestType=0;
    bool meleeTestBusy=false,meleeTestValid=false;
    uint32_t meleeTestModel=0;
    uint64_t meleeTestStarted=0;
    const char* meleeTestError="not requested";
    std::string MeleeTestStatus()
    {
        AcquireSRWLockShared(&meleeTestLock);
        const auto value="{\"ok\":true,\"pending\":"+std::string(meleeTestBusy ? "true":"false")+
            ",\"valid\":"+(meleeTestValid ? "true":"false")+",\"weaponType\":"+std::to_string(meleeTestType)+
            ",\"error\":\""+meleeTestError+"\"}";
        ReleaseSRWLockShared(&meleeTestLock);return value;
    }
    std::string QueueMeleeTest(unsigned type)
    {
        AcquireSRWLockExclusive(&meleeTestLock);
        if(meleeTestBusy) {ReleaseSRWLockExclusive(&meleeTestLock);return "{\"ok\":false,\"error\":\"equipment request pending\"}";}
        meleeTestType=type;meleeTestModel=0;meleeTestStarted=GetTickCount64();
        meleeTestBusy=true;meleeTestValid=false;meleeTestError="";
        ReleaseSRWLockExclusive(&meleeTestLock);
        return "{\"ok\":true,\"accepted\":true}";
    }
    void ProcessMeleeTest()
    {
        AcquireSRWLockExclusive(&meleeTestLock);
        if(!meleeTestBusy) {ReleaseSRWLockExclusive(&meleeTestLock);return;}
        Ped ped=0;
        Natives::GetPlayerChar(Natives::GetPlayerId(),&ped);
        const char* error="";
        if(!CTimer::ms_bUserPause || !CTimer::ms_bScriptPause || *CTimer::ms_bUserPause || *CTimer::ms_bScriptPause ||
            !Natives::IsPlayerPlaying(Natives::GetPlayerId()) || Natives::GetMissionFlag() ||
            (Natives::HasCutsceneLoaded() && !Natives::HasCutsceneFinished()) ||
            !ped || !Natives::DoesCharExist(ped) || Natives::IsCharDead(ped) || Natives::IsCharInAnyCar(ped))
            error="requires living unpaused on-foot free roam";
        else if(GetTickCount64()-meleeTestStarted>30000) error="weapon model load timed out";
        else
        {
            if(!meleeTestModel)
            {
                Natives::GetWeapontypeModel(meleeTestType,&meleeTestModel);
                if(!meleeTestModel || !Natives::IsModelInCdimage(meleeTestModel)) error="weapon model unavailable";
                else Natives::RequestModel(meleeTestModel);
            }
            if(!*error && !Natives::HasModelLoaded(meleeTestModel))
            {ReleaseSRWLockExclusive(&meleeTestLock);return;}
            if(!*error)
            {
                // Fixed melee types only, no address/script execution or save.
                // This replaces the native melee inventory slot for testing.
                Natives::GiveWeaponToChar(ped,meleeTestType,1,false);
                Natives::SetCurrentCharWeapon(ped,meleeTestType,true);
                uint32_t selected=~0u;
                if(!Natives::GetCurrentCharWeapon(ped,&selected) || selected!=meleeTestType) error="native equip readback failed";
            }
        }
        if(meleeTestModel) Natives::MarkModelAsNoLongerNeeded(meleeTestModel);
        meleeTestValid=!*error;meleeTestError=error;meleeTestBusy=false;
        ReleaseSRWLockExclusive(&meleeTestLock);
    }

    std::string LocationStatus()
    {
        char buffer[384];
        AcquireSRWLockShared(&locationLock);
        sprintf_s(buffer,"{\"ok\":true,\"id\":%u,\"pending\":%s,\"valid\":%s,\"error\":\"%s\",\"position\":{\"x\":%.6f,\"y\":%.6f,\"z\":%.6f,\"heading\":%.6f}}",
            locationId,locationBusy ? "true":"false",locationValid ? "true":"false",locationError,
            locationResult.x,locationResult.y,locationResult.z,locationResult.heading);
        ReleaseSRWLockShared(&locationLock);
        return buffer;
    }
    std::string QueueLocation(LocationOperation operation,const VRDevLocation::Position& value={})
    {
        AcquireSRWLockExclusive(&locationLock);
        if(locationBusy)
        {
            ReleaseSRWLockExclusive(&locationLock);
            return "{\"ok\":false,\"error\":\"location operation pending\"}";
        }
        ++locationId; locationBusy=true; locationValid=false; locationError="";
        locationRequest=value; locationOperation=operation;
        const auto id=locationId;
        ReleaseSRWLockExclusive(&locationLock);
        locationQueued.store(true,std::memory_order_release);
        return "{\"ok\":true,\"accepted\":true,\"id\":"+std::to_string(id)+"}";
    }
    void ProcessLocation()
    {
        if(!locationQueued.exchange(false,std::memory_order_acquire)) return;
        AcquireSRWLockExclusive(&locationLock);
        const auto operation=locationOperation;
        auto position=locationRequest;
        locationOperation=LocationOperation::None;
        ReleaseSRWLockExclusive(&locationLock);
        if(operation==LocationOperation::None) return;
        Ped ped=0;
        const char* error="";
        if(!CTimer::ms_bUserPause || !CTimer::ms_bScriptPause ||
            *CTimer::ms_bUserPause || *CTimer::ms_bScriptPause ||
            !Natives::IsPlayerPlaying(Natives::GetPlayerId()) || Natives::GetMissionFlag() ||
            (Natives::HasCutsceneLoaded() && !Natives::HasCutsceneFinished()))
            error="requires unpaused free roam";
        else
        {
            Natives::GetPlayerChar(Natives::GetPlayerId(),&ped);
            if(!ped || !Natives::DoesCharExist(ped) || Natives::IsCharDead(ped) ||
                Natives::IsCharInAnyCar(ped) || Natives::IsCharInAir(ped))
                error="requires a living grounded player on foot";
            else
            {
                if(operation==LocationOperation::Restore)
                {
                    Natives::RequestCollisionAtPosn(position.x,position.y,position.z);
                    Natives::SetCharCoordinatesNoOffset(ped,position.x,position.y,position.z);
                    Natives::SetCharHeading(ped,position.heading);
                    VRCamera::Recenter();
                }
                Natives::GetCharCoordinates(ped,&position.x,&position.y,&position.z);
                Natives::GetCharHeading(ped,&position.heading);
                if(!VRDevLocation::Valid(position)) error="invalid native position";
            }
        }
        AcquireSRWLockExclusive(&locationLock);
        if(!*error) locationResult=position;
        locationError=error; locationValid=!*error; locationBusy=false;
        ReleaseSRWLockExclusive(&locationLock);
    }

    void Prepare(Stage next)
    {
        stage = next;
        complete = false;
        transferred = 0;
        immediateError = 0;
        operationStarted = GetTickCount64();
        ResetEvent(ioEvent);
        io = {};
        io.hEvent = ioEvent;
    }

    void Connect()
    {
        quitWindow = nullptr;
        DisconnectNamedPipe(pipe);
        Prepare(Stage::Connecting);
        complete = ConnectNamedPipe(pipe, &io) != FALSE;
        if (!complete)
        {
            const auto error = GetLastError();
            if (error == ERROR_PIPE_CONNECTED) complete = true;
            else if (error != ERROR_IO_PENDING) immediateError = error;
        }
    }

    void Read(Stage next)
    {
        Prepare(next);
        complete = ReadFile(pipe, input, sizeof(input), &transferred, &io) != FALSE;
        if (!complete && GetLastError() != ERROR_IO_PENDING) immediateError = GetLastError();
    }

    HWND GameWindow()
    {
        HWND window = gWnd;
        if (!IsWindow(window) && RageDirect3DDevice9::m_pRealDevice &&
            *RageDirect3DDevice9::m_pRealDevice)
        {
            D3DDEVICE_CREATION_PARAMETERS parameters{};
            if (SUCCEEDED((*RageDirect3DDevice9::m_pRealDevice)->GetCreationParameters(&parameters)))
                window = parameters.hFocusWindow;
        }
        DWORD process = 0;
        GetWindowThreadProcessId(window, &process);
        return IsWindow(window) && process == GetCurrentProcessId() ? window : nullptr;
    }

    void ExecuteCommand()
    {
        std::string command(input, transferred);
        while (!command.empty() && (command.back() == '\r' || command.back() == '\n'))
            command.pop_back();
        if (command == "status")
        {
            response = "{\"ok\":true,\"protocol\":1,\"pid\":" +
                std::to_string(GetCurrentProcessId()) + ",\"openxr\":" +
                OpenXRVR::Status() + ",\"stereo\":" + VRStereo::Status() + "}";
        }
        else if(command=="profile-start" || command=="profile-start-cpu" || command=="profile-start-cpu-noflush" || command=="profile-start-cpu-noao" || command=="profile-start-cpu-constants" || command=="profile-start-cpu-draws" || command=="profile-start-cpu-states" || command=="profile-start-cpu-drawforwarding" || command=="profile-start-cpu-texturelinear" || command=="profile-start-cpu-broadvisibility" || command.rfind("profile-states ",0)==0 || command.rfind("profile-draws ",0)==0 || command=="profile-stop" || command=="profile-status" || command=="profile-constants" || command.rfind("profile-metric ",0)==0)
        {
            response=VRProfile::Command(command);
        }
        else if(command=="producer-test-status")response=OpenXRVR::ProducerTestStatus();
        else if(command=="producer-test on" || command=="producer-test off")
        {
            response=OpenXRVR::RequestProducerTest(command=="producer-test on") ?
                "{\"ok\":true,\"accepted\":true,\"automaticTimeoutSeconds\":15,\"cachedPose\":true}" : "{\"ok\":false,\"error\":\"Vulkan stereo session unavailable\"}";
        }
        else if(command=="input-status")
        {
            response="{\"ok\":true,\"input\":"+OpenXRVR::InputStatus()+"}";
        }
        else if(command=="hand-status") response="{\"ok\":true,\"hands\":"+VRHands::Status()+"}";
        else if(command=="combat-status") response="{\"ok\":true,\"combat\":"+VRHandPhysics::CombatStatus()+"}";
        else if(command=="melee-test-status") response=MeleeTestStatus();
        else if(command=="melee-test bat" || command=="melee-test cue" || command=="melee-test knife")
            response=QueueMeleeTest(command=="melee-test bat" ? 1u:(command=="melee-test cue" ? 2u:3u));
        else if(command=="hud-capture") {VRHud::Capture();response="{\"ok\":true,\"accepted\":true}";}
        else if(command=="hud-status") response="{\"ok\":true,\"hud\":"+VRHud::Status()+"}";
        else if(command=="hand-calibrate left" || command=="hand-calibrate right")
        {
            response=VRHands::Calibrate(command=="hand-calibrate left" ? 0u : 1u)
                ? "{\"ok\":true,\"hands\":"+VRHands::Status()+"}"
                : "{\"ok\":false,\"error\":\"fresh focused headset and selected grip pose required\"}";
        }
        else if(command.rfind("hand-trim ",0)==0)
        {
            char side[6]{},trailing=0; int x=0,y=0,z=0;
            const bool parsed=sscanf_s(command.c_str()+10,"%5s %d %d %d %c",side,6u,&x,&y,&z,&trailing,1u)==4;
            response=parsed && (strcmp(side,"left")==0 || strcmp(side,"right")==0) &&
                VRHands::Trim(strcmp(side,"left")==0 ? 0u : 1u,x,y,z)
                ? "{\"ok\":true,\"hands\":"+VRHands::Status()+"}"
                : "{\"ok\":false,\"error\":\"expected enabled hand IK, left/right and integer x y z trim in [-45,45]\"}";
        }
        else if(command.rfind("hand-rotation ",0)==0)
        {
            int x=0,y=0,z=0; char trailing=0;
            response=sscanf_s(command.c_str()+14,"%d %d %d %c",&x,&y,&z,&trailing,1u)==3 && VRHands::SetRotation(x,y,z)
                ? "{\"ok\":true,\"hands\":"+VRHands::Status()+"}"
                : "{\"ok\":false,\"error\":\"expected enabled hand IK and integer x y z rotations in [-180,180]\"}";
        }
        else if(command=="location-status") response=LocationStatus();
        else if(command=="location-capture") response=QueueLocation(LocationOperation::Capture);
        else if(command.rfind("location-restore ",0)==0)
        {
            VRDevLocation::Position value;
            response=VRDevLocation::Parse(std::string_view(command).substr(17),value)
                ? QueueLocation(LocationOperation::Restore,value)
                : "{\"ok\":false,\"error\":\"expected bounded finite x y z heading\"}";
        }
        else if(command=="locomotion-status")
        {
            response="{\"ok\":true,\"locomotion\":"+VRCamera::ControllerMovementStatus()+"}";
        }
        else if(command=="movement-status")
        {
            response="{\"ok\":true,\"movement\":"+VRCamera::MovementStatus()+"}";
        }
        else if(command=="aim-status")
        {
            response="{\"ok\":true,\"aim\":"+VRCamera::AimStatus()+"}";
        }
        else if(command=="visibility-test on" || command=="visibility-test off")
        {
            const bool value=command=="visibility-test on";
            response=VRStereo::SetVisibilityTest(value) ?
                std::string("{\"ok\":true,\"accepted\":true,\"tightVisibility\":")+(value ? "true}" : "false}") :
                "{\"ok\":false,\"error\":\"stereo not ready\"}";
        }
        else if(command=="sphere-classification-test on" || command=="sphere-classification-test off")
        {
            const bool value=command=="sphere-classification-test on";
            response=VRStereo::SetSphereClassificationTest(value) ?
                std::string("{\"ok\":true,\"accepted\":true,\"fastClassification\":")+(value ? "true}" : "false}") :
                "{\"ok\":false,\"error\":\"stereo not ready\"}";
        }
        else if(command=="sphere-classification-probe")
        {
            response=VRStereo::RequestSphereClassificationProbe() ?
                "{\"ok\":true,\"accepted\":true,\"decisionsChanged\":false}" :
                "{\"ok\":false,\"error\":\"stereo not ready\"}";
        }
        else if(command=="vertex-emission-test on" || command=="vertex-emission-test off")
        {
            const bool value=command=="vertex-emission-test on";
            response=VRStereo::SetVertexEmissionTest(value) ?
                std::string("{\"ok\":true,\"accepted\":true,\"replacementRequested\":")+(value ? "true}" : "false}") :
                "{\"ok\":false,\"error\":\"stereo not ready\"}";
        }
        else if(command=="billboard-instancing-test on" || command=="billboard-instancing-test off")
        {
            const bool value=command=="billboard-instancing-test on";
            response=VRStereo::SetBillboardInstancingTest(value) ?
                std::string("{\"ok\":true,\"accepted\":true,\"replacementRequested\":")+(value ? "true}" : "false}") :
                "{\"ok\":false,\"error\":\"stereo not ready\"}";
        }
        else if(command=="rigid-instancing-test on" || command=="rigid-instancing-test off")
        {
            const bool value=command=="rigid-instancing-test on";
            response=VRStereo::SetRigidInstancingTest(value) ?
                std::string("{\"ok\":true,\"accepted\":true,\"replacementRequested\":")+(value ? "true}" : "false}") :
                "{\"ok\":false,\"error\":\"stereo not ready\"}";
        }
        else if(command=="constant-batch-test on" || command=="constant-batch-test off")
        {
            const bool value=command=="constant-batch-test on";
            response=VRStereo::SetConstantBatchTest(value) ?
                std::string("{\"ok\":true,\"accepted\":true,\"batchRequested\":")+(value ? "true}" : "false}") :
                "{\"ok\":false,\"error\":\"stereo not ready\"}";
        }
        else if(command=="cpu-omit-status")response=VRStereo::CpuOmissionStatus();
        else if(command=="cpu-omit-test off" || command=="cpu-omit-test materials" || command=="cpu-omit-test models" ||
            command=="cpu-omit-test gbuffer" || command=="cpu-omit-test scene" || command=="cpu-omit-test lighting" || command=="cpu-omit-test eyes")
        {
            const unsigned mode=command=="cpu-omit-test models" ? 2:command=="cpu-omit-test materials" ? 1:
                command=="cpu-omit-test gbuffer" ? 3:command=="cpu-omit-test scene" ? 4:
                command=="cpu-omit-test lighting" ? 5:command=="cpu-omit-test eyes" ? 6:0;
            response=VRStereo::RequestCpuOmission(mode) ?
                "{\"ok\":true,\"accepted\":true,\"upperBoundOnly\":true,\"gpuReplacement\":false,\"automaticTimeoutSeconds\":90}" :
                "{\"ok\":false,\"error\":\"stereo not ready\"}";
        }
        else if(command=="mesh-live-probe" || command=="mesh-native-probe")
        {
            response=VRStereo::RequestLiveMeshProbe(command=="mesh-live-probe") ?
                "{\"ok\":true,\"accepted\":true,\"scratchOnly\":false,\"eyePairs\":1}" :
                "{\"ok\":false,\"error\":\"stereo not ready\"}";
        }
        else if(command=="mesh-packet-probe" || command=="mesh-order-probe" || command=="mesh-replay-probe" || command=="mesh-benchmark-probe")
        {
            const bool accepted=command=="mesh-benchmark-probe" ? VRStereo::RequestMeshBenchmarkProbe():
                command=="mesh-replay-probe" ? VRStereo::RequestMeshReplayProbe():
                command=="mesh-order-probe" ? VRStereo::RequestMeshOrderProbe():VRStereo::RequestMeshPacketProbe();
            response=accepted ? "{\"ok\":true,\"accepted\":true,\"scratchOnly\":true}" :
                "{\"ok\":false,\"error\":\"stereo not ready\"}";
        }
        else if(command=="model-command-probe")
        {
            response=VRStereo::RequestModelCommandProbe() ? "{\"ok\":true,\"command\":\"model-command-probe\",\"accepted\":true}" :
                "{\"ok\":false,\"error\":\"stereo not ready\"}";
        }
        else if(command=="vertex-caller-probe")
        {
            response=VRStereo::RequestVertexCallerProbe() ?
                "{\"ok\":true,\"accepted\":true,\"countsOnly\":true,\"durationSeconds\":5}" :
                "{\"ok\":false,\"error\":\"stereo not ready\"}";
        }
        else if(command=="stereo-test")
        {
            response=VRStereo::RestartExperiment() ? "{\"ok\":true,\"command\":\"stereo-test\",\"accepted\":true}" :
                "{\"ok\":false,\"error\":\"experimental stereo not ready\"}";
        }
        else if(command=="lod-probe")
        {
            response=VRStereo::RestartLodProbe() ? "{\"ok\":true,\"command\":\"lod-probe\",\"accepted\":true}" :
                "{\"ok\":false,\"error\":\"development LOD probe unavailable or stereo not ready\"}";
        }
        else if(command=="recenter")
        {
            VRCamera::Recenter();
            response="{\"ok\":true,\"command\":\"recenter\",\"accepted\":true}";
        }
        else if(command=="camera-probe")
        {
            response=VRCamera::RestartAnchorProbe() ? "{\"ok\":true,\"command\":\"camera-probe\",\"accepted\":true}" :
                "{\"ok\":false,\"error\":\"anchor probe disabled in development configuration\"}";
        }
        else if (command == "quit")
        {
            quitWindow = GameWindow();
            response = quitWindow ? "{\"ok\":true,\"command\":\"quit\",\"accepted\":true}" :
                "{\"ok\":false,\"error\":\"game window unavailable\"}";
        }
        else response = "{\"ok\":false,\"error\":\"unsupported command\"}";
        Prepare(Stage::Response);
        complete = WriteFile(pipe, response.data(), static_cast<DWORD>(response.size()),
            &transferred, &io) != FALSE;
        if (!complete && GetLastError() != ERROR_IO_PENDING) immediateError = GetLastError();
    }

    void Pump()
    {
        if (pipe == INVALID_HANDLE_VALUE) return;
        if (immediateError) { Connect(); return; }
        if (!complete)
        {
            if (!GetOverlappedResult(pipe, &io, &transferred, FALSE))
            {
                if (GetLastError() == ERROR_IO_INCOMPLETE)
                {
                    if (stage != Stage::Connecting && GetTickCount64() - operationStarted > 5000)
                        CancelIoEx(pipe, &io);
                    return;
                }
                Connect();
                return;
            }
        }
        switch (stage)
        {
        case Stage::Connecting: Read(Stage::Command); break;
        case Stage::Command: ExecuteCommand(); break;
        case Stage::Response: Read(Stage::Acknowledge); break;
        case Stage::Acknowledge:
            if (transferred == 3 && std::memcmp(input, "ack", 3) == 0 && quitWindow)
            {
                OpenXRVR::Shutdown();
                VRStereo::Shutdown();
                PostMessageW(quitWindow, WM_CLOSE, 0, 0);
            }
            Connect();
            break;
        }
    }

    PSECURITY_DESCRIPTOR CurrentUserSecurity()
    {
        HANDLE token = nullptr;
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return nullptr;
        DWORD bytes = 0;
        GetTokenInformation(token, TokenUser, nullptr, 0, &bytes);
        std::vector<unsigned char> data(bytes);
        const BOOL gotUser = GetTokenInformation(token, TokenUser, data.data(), bytes, &bytes);
        CloseHandle(token);
        if (!gotUser) return nullptr;
        LPWSTR sid = nullptr;
        if (!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(data.data())->User.Sid, &sid))
            return nullptr;
        const std::wstring sddl = std::wstring(L"D:P(A;;GA;;;SY)(A;;GA;;;") + sid + L")";
        LocalFree(sid);
        PSECURITY_DESCRIPTOR descriptor = nullptr;
        if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(),
            SDDL_REVISION_1, &descriptor, nullptr)) return nullptr;
        return descriptor;
    }
}

void DevControl::Install()
{
    CIniReader ini("");
    if (ini.ReadInteger("DEVELOPMENT", "EnableCommandBridge", 0) == 0) return;
    auto descriptor = CurrentUserSecurity();
    if (!descriptor) return; // Do not fall back to a broader ACL.
    SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), descriptor, FALSE};
    wchar_t name[128];
    swprintf_s(name, L"\\\\.\\pipe\\GTA4VR.Dev.%lu", GetCurrentProcessId());
    pipe = CreateNamedPipeW(name, PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED |
        FILE_FLAG_FIRST_PIPE_INSTANCE, PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE |
        PIPE_REJECT_REMOTE_CLIENTS, 1, 512, 128, 0, &security);
    LocalFree(descriptor);
    if (pipe == INVALID_HANDLE_VALUE) return;
    ioEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!ioEvent) { CloseHandle(pipe); pipe = INVALID_HANDLE_VALUE; return; }
    Connect();
    FusionFix::onGameProcessEvent() += ProcessLocation;
    FusionFix::onGameProcessEvent() += ProcessMeleeTest;
    FusionFix::onEndScene() += []() { Pump(); };
    FusionFix::onShutdownEvent() += []()
    {
        if (pipe != INVALID_HANDLE_VALUE) { CancelIoEx(pipe, nullptr); CloseHandle(pipe); }
        if (ioEvent) CloseHandle(ioEvent);
    };
}
