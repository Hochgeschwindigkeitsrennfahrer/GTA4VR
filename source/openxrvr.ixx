module;

#include <common.hxx>
#include <d3d11.h>
#include <dxgi1_2.h>
#include "includes/dxvk_vulkan_bridge.hpp"
#define XR_USE_PLATFORM_WIN32
#define XR_USE_GRAPHICS_API_D3D11
#define XR_USE_GRAPHICS_API_VULKAN
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <vector>
#include <string>
#include <wrl/client.h>
#include <atomic>
#include <cmath>
#include <filesystem>

export module openxrvr;

import common;
import vrprofile;
import comvars;
import settings;

export namespace OpenXRVR
{
    struct EyeView
    {
        float position[3]{};
        float orientation[4]{}; // x, y, z, w in OpenXR's reference space.
        float fov[4]{}; // left, right, up, down angles in radians.
        bool floorRelative=false; // STAGE Y=0 is the runtime-calibrated floor.
    };
    struct ControllerInput
    {
        bool active[2]{},clicked[2]{},poseValid[2]{};
        float trackpad[2][2]{},position[2][3]{},orientation[2][4]{};
        unsigned long long sampledAt=0;
    };
    bool GetControllerInput(ControllerInput& input);
    std::string InputStatus();
    void Install();
    void Shutdown(); // Call from the EndScene callback thread, never DllMain.
    std::string Status();
    bool RequestProducerTest(bool enable);
    std::string ProducerTestStatus();
    bool GetEyeViews(EyeView (&views)[2], long long& displayTime);
    bool HasFocusedViews(); // Fresh pose and focus gate for gameplay direction.
    bool CanAcceptEyeImages(); // Render callback thread; false while stopped or recreating.
    bool PacesGameplay(); // Atomic; true while the runtime requests stereo frames.
    bool SetEyeImages(const HANDLE (&shared)[2], unsigned width, unsigned height,
        const EyeView (&views)[2], long long poseTime); // Render callback thread after D3D9 GPU completion.
    bool UsesVulkan();
    bool SetVulkanEyeImages(IDirect3DTexture9* const (&textures)[2],unsigned width,unsigned height,
        const EyeView (&views)[2],long long poseTime);
    void SetEyeImageProvider(void (*provider)()); // Called on EndScene before any D3D11 eye copy.
}

namespace
{
    struct OpenXRApi
    {
        PFN_xrEnumerateInstanceExtensionProperties enumerateExtensions = nullptr;
        PFN_xrCreateInstance createInstance = nullptr;
        PFN_xrGetInstanceProperties getInstanceProperties = nullptr;
        PFN_xrGetSystem getSystem = nullptr;
        PFN_xrGetSystemProperties getSystemProperties = nullptr;
        PFN_xrGetInstanceProcAddr getInstanceProcAddr = nullptr;
        PFN_xrCreateSession createSession = nullptr;
        PFN_xrDestroySession destroySession = nullptr;
        PFN_xrDestroyInstance destroyInstance = nullptr;
        PFN_xrPollEvent pollEvent = nullptr;
        PFN_xrBeginSession beginSession = nullptr;
        PFN_xrEndSession endSession = nullptr;
        PFN_xrWaitFrame waitFrame = nullptr;
        PFN_xrBeginFrame beginFrame = nullptr;
        PFN_xrEndFrame endFrame = nullptr;
        PFN_xrCreateReferenceSpace createReferenceSpace = nullptr;
        PFN_xrDestroySpace destroySpace = nullptr;
        PFN_xrLocateViews locateViews = nullptr;
        PFN_xrEnumerateSwapchainFormats enumerateSwapchainFormats = nullptr;
        PFN_xrCreateSwapchain createSwapchain = nullptr;
        PFN_xrDestroySwapchain destroySwapchain = nullptr;
        PFN_xrEnumerateSwapchainImages enumerateSwapchainImages = nullptr;
        PFN_xrAcquireSwapchainImage acquireSwapchainImage = nullptr;
        PFN_xrWaitSwapchainImage waitSwapchainImage = nullptr;
        PFN_xrReleaseSwapchainImage releaseSwapchainImage = nullptr;
        PFN_xrGetDisplayRefreshRateFB getDisplayRefreshRate = nullptr;
        PFN_xrStringToPath stringToPath=nullptr;
        PFN_xrCreateActionSet createActionSet=nullptr;
        PFN_xrDestroyActionSet destroyActionSet=nullptr;
        PFN_xrCreateAction createAction=nullptr;
        PFN_xrSuggestInteractionProfileBindings suggestBindings=nullptr;
        PFN_xrAttachSessionActionSets attachActionSets=nullptr;
        PFN_xrCreateActionSpace createActionSpace=nullptr;
        PFN_xrSyncActions syncActions=nullptr;
        PFN_xrGetActionStateVector2f getVector2=nullptr;
        PFN_xrGetActionStateBoolean getBoolean=nullptr;
        PFN_xrGetActionStatePose getPose=nullptr;
        PFN_xrLocateSpace locateSpace=nullptr;
    } api;
    HMODULE loader = nullptr;

#define xrEnumerateInstanceExtensionProperties api.enumerateExtensions
#define xrCreateInstance api.createInstance
#define xrGetInstanceProperties api.getInstanceProperties
#define xrGetSystem api.getSystem
#define xrGetSystemProperties api.getSystemProperties
#define xrGetInstanceProcAddr api.getInstanceProcAddr
#define xrCreateSession api.createSession
#define xrDestroySession api.destroySession
#define xrDestroyInstance api.destroyInstance
#define xrPollEvent api.pollEvent
#define xrBeginSession api.beginSession
#define xrEndSession api.endSession
#define xrWaitFrame api.waitFrame
#define xrBeginFrame api.beginFrame
#define xrEndFrame api.endFrame

    HANDLE logFile = INVALID_HANDLE_VALUE;
    XrInstance instance = XR_NULL_HANDLE;
    XrSession session = XR_NULL_HANDLE;
    XrSpace localSpace = XR_NULL_HANDLE;
    bool floorSpace=false;
    SRWLOCK eyeLock = SRWLOCK_INIT;
    OpenXRVR::EyeView eyeViews[2]{};
    long long eyeDisplayTime = 0;
    bool eyeViewsValid = false;
    bool focusedEyeViews=false;
    uint64_t eyeSampledAt=0;
    bool controllerEnabled=false,inputReady=false;
    XrActionSet inputActions=XR_NULL_HANDLE;
    XrAction padAction=XR_NULL_HANDLE,clickAction=XR_NULL_HANDLE,aimAction=XR_NULL_HANDLE;
    XrPath handPaths[2]{};
    XrSpace aimSpaces[2]{XR_NULL_HANDLE,XR_NULL_HANDLE};
    OpenXRVR::ControllerInput controllerInput{}; // Published with eyeLock.
    XrSystemId systemId = XR_NULL_SYSTEM_ID;
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    XrSessionState sessionState = XR_SESSION_STATE_UNKNOWN;
    bool running = false;
    bool enabled = false;
    bool vulkanEnabled=false;
    DxvkVulkan::Bridge* vulkanBridge=nullptr;
    template<class F> auto RuntimeQueueCall(VRProfile::Metric metric,F&& call) {
        if(!vulkanBridge) return call();
        const auto start=VRProfile::Begin();
        return vulkanBridge->WithQueue([&](){
            VRProfile::End(metric,start); // Only lock acquisition, not runtime work.
            return call();
        });
    }
    HMODULE openvrBootstrap=nullptr; // Retained until process exit: DXVK may use it.
    bool pendingRecreate = false;
    bool permanentFailure = false;
    unsigned frameCount = 0;
    std::atomic<float> displayRefreshHz{0};
    std::atomic<unsigned> producerRequest{2};
    std::atomic<bool> producerActive{false};
    std::atomic<ULONGLONG> producerDeadline{0},producerPairs{0};
    std::atomic<bool> gameplayPacing{false};
    std::atomic<float> submittedHz{0};
    ULONGLONG rateWindowStart=0;
    unsigned rateWindowLayers=0;
    struct EyeSwapchain
    {
        XrSwapchain handle=XR_NULL_HANDLE;
        std::vector<XrSwapchainImageD3D11KHR> images;
        std::vector<XrSwapchainImageVulkanKHR> vulkanImages;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> shared;
        HANDLE sharedHandle=nullptr;
    };
    EyeSwapchain* eyeChains=nullptr; // Explicit render-thread cleanup, never loader-lock COM destruction.
    OpenXRVR::EyeView imageViews[2]{};
    unsigned imageWidth=0,imageHeight=0;
    long long imagePoseTime=0;
    bool haveEyeImages=false;
    unsigned submittedFrames=0;
    void (*eyeImageProvider)()=nullptr;

    void Log(const char* message)
    {
        OutputDebugStringA(message);
        OutputDebugStringA("\n");
        if (logFile == INVALID_HANDLE_VALUE) return;
        DWORD written = 0;
        WriteFile(logFile, message, static_cast<DWORD>(std::strlen(message)), &written, nullptr);
        WriteFile(logFile, "\r\n", 2, &written, nullptr);
        FlushFileBuffers(logFile);
    }

    bool Check(const char* step, XrResult result)
    {
        const char* name = "OpenXR result";
        switch (result)
        {
        case XR_SUCCESS: name = "XR_SUCCESS"; break;
        case XR_ERROR_RUNTIME_UNAVAILABLE: name = "XR_ERROR_RUNTIME_UNAVAILABLE"; break;
        case XR_ERROR_FORM_FACTOR_UNAVAILABLE: name = "XR_ERROR_FORM_FACTOR_UNAVAILABLE"; break;
        case XR_ERROR_API_VERSION_UNSUPPORTED: name = "XR_ERROR_API_VERSION_UNSUPPORTED"; break;
        case XR_ERROR_INSTANCE_LOST: name = "XR_ERROR_INSTANCE_LOST"; break;
        case XR_ERROR_SESSION_LOST: name = "XR_ERROR_SESSION_LOST"; break;
        case XR_ERROR_GRAPHICS_DEVICE_INVALID: name = "XR_ERROR_GRAPHICS_DEVICE_INVALID"; break;
        default: break;
        }
        char line[192];
        sprintf_s(line, "%s: %s (%d)", step, name, result);
        Log(line);
        return XR_SUCCEEDED(result);
    }

    const char* StateName(XrSessionState state)
    {
        switch (state)
        {
        case XR_SESSION_STATE_IDLE: return "IDLE";
        case XR_SESSION_STATE_READY: return "READY";
        case XR_SESSION_STATE_SYNCHRONIZED: return "SYNCHRONIZED";
        case XR_SESSION_STATE_VISIBLE: return "VISIBLE";
        case XR_SESSION_STATE_FOCUSED: return "FOCUSED";
        case XR_SESSION_STATE_STOPPING: return "STOPPING";
        case XR_SESSION_STATE_LOSS_PENDING: return "LOSS_PENDING";
        case XR_SESSION_STATE_EXITING: return "EXITING";
        default: return "UNKNOWN";
        }
    }

    bool CheckImageStep(const char* step,XrResult result)
    {
        if(result==XR_SUCCESS) return true;
        Check(step,result);
        return false;
    }

    bool LoadApi()
    {
        wchar_t path[MAX_PATH]{};
        if (!GetModuleFileNameW(nullptr, path, MAX_PATH)) return false;
        wchar_t* name = wcsrchr(path, L'\\');
        if (!name) return false;
        name[1] = L'\0';
        if (wcscat_s(path, L"plugins\\openxr_loader.dll") != 0) return false;
        loader = LoadLibraryW(path);
        if (!loader)
        {
            Log("OpenXR fallback: x86 plugins/openxr_loader.dll missing or unloadable");
            return false;
        }
#define LOAD_XR(member, name) \
        api.member = reinterpret_cast<PFN_##name>(GetProcAddress(loader, #name)); \
        if (!api.member) { Log("OpenXR fallback: missing loader export " #name); return false; }
        LOAD_XR(enumerateExtensions, xrEnumerateInstanceExtensionProperties)
        LOAD_XR(createInstance, xrCreateInstance)
        LOAD_XR(getInstanceProperties, xrGetInstanceProperties)
        LOAD_XR(getSystem, xrGetSystem)
        LOAD_XR(getSystemProperties, xrGetSystemProperties)
        LOAD_XR(getInstanceProcAddr, xrGetInstanceProcAddr)
        LOAD_XR(createSession, xrCreateSession)
        LOAD_XR(destroySession, xrDestroySession)
        LOAD_XR(destroyInstance, xrDestroyInstance)
        LOAD_XR(pollEvent, xrPollEvent)
        LOAD_XR(beginSession, xrBeginSession)
        LOAD_XR(endSession, xrEndSession)
        LOAD_XR(waitFrame, xrWaitFrame)
        LOAD_XR(beginFrame, xrBeginFrame)
        LOAD_XR(endFrame, xrEndFrame)
        LOAD_XR(createReferenceSpace, xrCreateReferenceSpace)
        LOAD_XR(destroySpace, xrDestroySpace)
        LOAD_XR(locateViews, xrLocateViews)
        LOAD_XR(enumerateSwapchainFormats, xrEnumerateSwapchainFormats)
        LOAD_XR(createSwapchain, xrCreateSwapchain)
        LOAD_XR(destroySwapchain, xrDestroySwapchain)
        LOAD_XR(enumerateSwapchainImages, xrEnumerateSwapchainImages)
        LOAD_XR(acquireSwapchainImage, xrAcquireSwapchainImage)
        LOAD_XR(waitSwapchainImage, xrWaitSwapchainImage)
        LOAD_XR(releaseSwapchainImage, xrReleaseSwapchainImage)
        if(controllerEnabled)
        {
            LOAD_XR(stringToPath,xrStringToPath)
            LOAD_XR(createActionSet,xrCreateActionSet)
            LOAD_XR(destroyActionSet,xrDestroyActionSet)
            LOAD_XR(createAction,xrCreateAction)
            LOAD_XR(suggestBindings,xrSuggestInteractionProfileBindings)
            LOAD_XR(attachActionSets,xrAttachSessionActionSets)
            LOAD_XR(createActionSpace,xrCreateActionSpace)
            LOAD_XR(syncActions,xrSyncActions)
            LOAD_XR(getVector2,xrGetActionStateVector2f)
            LOAD_XR(getBoolean,xrGetActionStateBoolean)
            LOAD_XR(getPose,xrGetActionStatePose)
            LOAD_XR(locateSpace,xrLocateSpace)
        }
#undef LOAD_XR
        return true;
    }

    void DestroyRuntime()
    {
        producerRequest=2;producerActive=false;producerDeadline=0;
        displayRefreshHz=0;
        gameplayPacing=false;
        submittedHz=0;
        rateWindowStart=0;
        rateWindowLayers=0;
        haveEyeImages=false;
        if(vulkanBridge && !vulkanBridge->Destroy())
        {
            // Keep session/images alive until the pending GPU work completes.
            // A timed-out command must not leave the runtime holding freed memory.
            running=false; permanentFailure=true;
            AcquireSRWLockExclusive(&eyeLock);
            eyeViewsValid=focusedEyeViews=false;
            ReleaseSRWLockExclusive(&eyeLock);
            Log("Vulkan copy still pending; quarantining runtime resources until process exit");
            return;
        }
        delete vulkanBridge; vulkanBridge=nullptr;
        for(unsigned i=0;eyeChains && i<2;++i)
        {
            auto& eye=eyeChains[i];
            eye.shared.Reset();
            eye.images.clear();
            if(eye.handle!=XR_NULL_HANDLE) api.destroySwapchain(eye.handle);
            eye.handle=XR_NULL_HANDLE;
            eye.sharedHandle=nullptr;
        }
        delete[] eyeChains; eyeChains=nullptr;
        imageWidth=imageHeight=0;submittedFrames=0;
        AcquireSRWLockExclusive(&eyeLock);
        eyeViewsValid = false;
        focusedEyeViews=false;
        controllerInput={};
        ReleaseSRWLockExclusive(&eyeLock);
        running = false;
        sessionState = XR_SESSION_STATE_UNKNOWN;
        inputReady=false;
        for(auto& space:aimSpaces)
        {
            if(space!=XR_NULL_HANDLE) api.destroySpace(space);
            space=XR_NULL_HANDLE;
        }
        if (localSpace != XR_NULL_HANDLE)
        {
            Check("xrDestroySpace(tracking)", api.destroySpace(localSpace));
            localSpace = XR_NULL_HANDLE;
            floorSpace=false;
        }
        if (session != XR_NULL_HANDLE)
        {
            Check("xrDestroySession", xrDestroySession(session));
            session = XR_NULL_HANDLE;
        }
        if(inputActions!=XR_NULL_HANDLE) api.destroyActionSet(inputActions);
        inputActions=XR_NULL_HANDLE;
        padAction=clickAction=aimAction=XR_NULL_HANDLE;
        handPaths[0]=handPaths[1]=XR_NULL_PATH;
        if (context) { context->Release(); context = nullptr; }
        if (device) { device->Release(); device = nullptr; }
        if (instance != XR_NULL_HANDLE)
        {
            Check("xrDestroyInstance", xrDestroyInstance(instance));
            instance = XR_NULL_HANDLE;
        }
        systemId = XR_NULL_SYSTEM_ID;
        if (loader) { FreeLibrary(loader); loader = nullptr; }
        api = {};
    }

    bool CreateControllerActions()
    {
        if(!controllerEnabled) return true;
        XrActionSetCreateInfo set{XR_TYPE_ACTION_SET_CREATE_INFO};
        strcpy_s(set.actionSetName,"gameplay"); strcpy_s(set.localizedActionSetName,"GTA IV gameplay");
        if(!Check("xrCreateActionSet",api.createActionSet(instance,&set,&inputActions))) return false;
        if(!Check("left hand path",api.stringToPath(instance,"/user/hand/left",&handPaths[0])) ||
            !Check("right hand path",api.stringToPath(instance,"/user/hand/right",&handPaths[1]))) return false;
        auto create=[&](const char* name,const char* label,XrActionType type,XrAction& action)
        {
            XrActionCreateInfo info{XR_TYPE_ACTION_CREATE_INFO};
            strcpy_s(info.actionName,name); strcpy_s(info.localizedActionName,label);
            info.actionType=type; info.countSubactionPaths=2; info.subactionPaths=handPaths;
            return Check(name,api.createAction(inputActions,&info,&action));
        };
        if(!create("trackpad","Trackpad",XR_ACTION_TYPE_VECTOR2F_INPUT,padAction) ||
            !create("trackpad_click","Trackpad click",XR_ACTION_TYPE_BOOLEAN_INPUT,clickAction) ||
            !create("aim_pose","Controller aim",XR_ACTION_TYPE_POSE_INPUT,aimAction)) return false;
        std::vector<XrActionSuggestedBinding> bindings;
        for(unsigned hand=0;hand<2;++hand)
            for(auto binding: {std::pair{padAction,"/input/trackpad"},
                std::pair{clickAction,"/input/trackpad/click"},std::pair{aimAction,"/input/aim/pose"}})
            {
                const std::string path=std::string(hand ? "/user/hand/right" : "/user/hand/left")+binding.second;
                XrPath xrPath=XR_NULL_PATH;
                if(!Check("Vive binding path",api.stringToPath(instance,path.c_str(),&xrPath))) return false;
                bindings.push_back({binding.first,xrPath});
            }
        XrInteractionProfileSuggestedBinding suggest{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
        if(!Check("Vive profile path",api.stringToPath(instance,"/interaction_profiles/htc/vive_controller",&suggest.interactionProfile))) return false;
        suggest.countSuggestedBindings=static_cast<uint32_t>(bindings.size()); suggest.suggestedBindings=bindings.data();
        if(!Check("Vive suggested bindings",api.suggestBindings(instance,&suggest))) return false;
        XrSessionActionSetsAttachInfo attach{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
        attach.countActionSets=1; attach.actionSets=&inputActions;
        if(!Check("xrAttachSessionActionSets",api.attachActionSets(session,&attach))) return false;
        for(unsigned hand=0;hand<2;++hand)
        {
            XrActionSpaceCreateInfo space{XR_TYPE_ACTION_SPACE_CREATE_INFO};
            space.action=aimAction; space.subactionPath=handPaths[hand]; space.poseInActionSpace.orientation.w=1;
            if(!Check("xrCreateActionSpace(aim)",api.createActionSpace(session,&space,&aimSpaces[hand]))) return false;
        }
        inputReady=true;
        Log("Vive Wand actions attached; input observation enabled, no game input injection");
        return true;
    }

    void SampleControllerInput(XrTime time)
    {
        OpenXRVR::ControllerInput sample{};
        if(inputReady && sessionState==XR_SESSION_STATE_FOCUSED)
        {
            XrActiveActionSet active{inputActions,XR_NULL_PATH};
            XrActionsSyncInfo sync{XR_TYPE_ACTIONS_SYNC_INFO}; sync.countActiveActionSets=1; sync.activeActionSets=&active;
            if(api.syncActions(session,&sync)==XR_SUCCESS)
                for(unsigned hand=0;hand<2;++hand)
                {
                    XrActionStateGetInfo get{XR_TYPE_ACTION_STATE_GET_INFO}; get.subactionPath=handPaths[hand];
                    XrActionStateVector2f pad{XR_TYPE_ACTION_STATE_VECTOR2F}; get.action=padAction;
                    if(api.getVector2(session,&get,&pad)==XR_SUCCESS && pad.isActive &&
                        std::isfinite(pad.currentState.x) && std::isfinite(pad.currentState.y))
                    {
                        sample.active[hand]=true;
                        sample.trackpad[hand][0]=std::clamp(pad.currentState.x,-1.f,1.f);
                        sample.trackpad[hand][1]=std::clamp(pad.currentState.y,-1.f,1.f);
                    }
                    XrActionStateBoolean click{XR_TYPE_ACTION_STATE_BOOLEAN}; get.action=clickAction;
                    if(api.getBoolean(session,&get,&click)==XR_SUCCESS && click.isActive) sample.clicked[hand]=click.currentState;
                    XrActionStatePose pose{XR_TYPE_ACTION_STATE_POSE}; get.action=aimAction;
                    XrSpaceLocation location{XR_TYPE_SPACE_LOCATION};
                    constexpr auto required=XR_SPACE_LOCATION_ORIENTATION_VALID_BIT|XR_SPACE_LOCATION_POSITION_VALID_BIT;
                    if(api.getPose(session,&get,&pose)==XR_SUCCESS && pose.isActive &&
                        api.locateSpace(aimSpaces[hand],localSpace,time,&location)==XR_SUCCESS &&
                        (location.locationFlags & required)==required)
                    {
                        const auto& p=location.pose;
                        const float finite=p.position.x+p.position.y+p.position.z+p.orientation.x+p.orientation.y+p.orientation.z+p.orientation.w;
                        if(std::isfinite(finite))
                        {
                            sample.poseValid[hand]=true;
                            sample.position[hand][0]=p.position.x; sample.position[hand][1]=p.position.y; sample.position[hand][2]=p.position.z;
                            sample.orientation[hand][0]=p.orientation.x; sample.orientation[hand][1]=p.orientation.y;
                            sample.orientation[hand][2]=p.orientation.z; sample.orientation[hand][3]=p.orientation.w;
                        }
                    }
                }
        }
        sample.sampledAt=GetTickCount64();
        AcquireSRWLockExclusive(&eyeLock); controllerInput=sample; ReleaseSRWLockExclusive(&eyeLock);
    }

    bool CreateRuntime()
    {
        if (!LoadApi()) return false;
        uint32_t count = 0;
        if (!Check("xrEnumerateInstanceExtensionProperties(count)",
            xrEnumerateInstanceExtensionProperties(nullptr, 0, &count, nullptr))) return false;
        std::vector<XrExtensionProperties> extensions(count);
        for (auto& item : extensions) item.type = XR_TYPE_EXTENSION_PROPERTIES;
        if (!Check("xrEnumerateInstanceExtensionProperties(list)",
            xrEnumerateInstanceExtensionProperties(nullptr, count, &count, extensions.data()))) return false;
        bool hasD3D11 = false, hasRefreshRate=false;
        for (const auto& item : extensions)
        {
            hasD3D11 |= std::strcmp(item.extensionName, vulkanEnabled ? XR_KHR_VULKAN_ENABLE_EXTENSION_NAME : XR_KHR_D3D11_ENABLE_EXTENSION_NAME) == 0;
            hasRefreshRate |= std::strcmp(item.extensionName, XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME) == 0;
        }
        if (!hasD3D11) { Log("OpenXR fallback: required graphics binding unavailable"); return false; }

        XrInstanceCreateInfo create{XR_TYPE_INSTANCE_CREATE_INFO};
        strcpy_s(create.applicationInfo.applicationName, "GTA IV VR");
        create.applicationInfo.applicationVersion = 1;
        strcpy_s(create.applicationInfo.engineName, "FusionFix");
        create.applicationInfo.engineVersion = 1;
        create.applicationInfo.apiVersion = XR_MAKE_VERSION(1, 0, 0);
        const char* names[] = {vulkanEnabled ? XR_KHR_VULKAN_ENABLE_EXTENSION_NAME : XR_KHR_D3D11_ENABLE_EXTENSION_NAME,XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME};
        create.enabledExtensionCount = hasRefreshRate ? 2 : 1;
        create.enabledExtensionNames = names;
        if (!Check("xrCreateInstance", xrCreateInstance(&create, &instance))) return false;
        if(hasRefreshRate)
            xrGetInstanceProcAddr(instance,"xrGetDisplayRefreshRateFB",
                reinterpret_cast<PFN_xrVoidFunction*>(&api.getDisplayRefreshRate));
        XrInstanceProperties props{XR_TYPE_INSTANCE_PROPERTIES};
        if (Check("xrGetInstanceProperties", xrGetInstanceProperties(instance, &props)))
        {
            char line[256];
            sprintf_s(line, "OpenXR runtime: %s %u.%u.%u", props.runtimeName,
                XR_VERSION_MAJOR(props.runtimeVersion), XR_VERSION_MINOR(props.runtimeVersion),
                XR_VERSION_PATCH(props.runtimeVersion));
            Log(line);
        }

        XrSystemGetInfo systemInfo{XR_TYPE_SYSTEM_GET_INFO};
        systemInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
        if (!Check("xrGetSystem(HMD)", xrGetSystem(instance, &systemInfo, &systemId))) return false;
        XrSystemProperties systemProps{XR_TYPE_SYSTEM_PROPERTIES};
        if (Check("xrGetSystemProperties", xrGetSystemProperties(instance, systemId, &systemProps)))
        {
            char line[256];
            sprintf_s(line, "OpenXR system: %s", systemProps.systemName);
            Log(line);
        }
        if(vulkanEnabled)
        {
            if(!RageDirect3DDevice9::m_pRealDevice || !*RageDirect3DDevice9::m_pRealDevice) return false;
            vulkanBridge=new DxvkVulkan::Bridge;
            if(!vulkanBridge->Attach(*RageDirect3DDevice9::m_pRealDevice))
                {Log("OpenXR Vulkan: DXVK device interop unavailable"); return false;}
            PFN_xrGetVulkanGraphicsRequirementsKHR getRequirements=nullptr;
            PFN_xrGetVulkanGraphicsDeviceKHR getPhysical=nullptr;
            if(XR_FAILED(xrGetInstanceProcAddr(instance,"xrGetVulkanGraphicsRequirementsKHR",reinterpret_cast<PFN_xrVoidFunction*>(&getRequirements))) ||
                XR_FAILED(xrGetInstanceProcAddr(instance,"xrGetVulkanGraphicsDeviceKHR",reinterpret_cast<PFN_xrVoidFunction*>(&getPhysical))) ||
                !getRequirements || !getPhysical) return false;
            XrGraphicsRequirementsVulkanKHR requirements{XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN_KHR};
            VkPhysicalDevice selected=nullptr;
            if(!Check("xrGetVulkanGraphicsRequirementsKHR",getRequirements(instance,systemId,&requirements)) ||
                !Check("xrGetVulkanGraphicsDeviceKHR",getPhysical(instance,systemId,vulkanBridge->instance,&selected)) ||
                selected!=vulkanBridge->physical) return false;
            XrGraphicsBindingVulkanKHR binding{XR_TYPE_GRAPHICS_BINDING_VULKAN_KHR};
            binding.instance=vulkanBridge->instance; binding.physicalDevice=selected; binding.device=vulkanBridge->device;
            binding.queueFamilyIndex=vulkanBridge->family; binding.queueIndex=vulkanBridge->index;
            XrSessionCreateInfo sessionInfo{XR_TYPE_SESSION_CREATE_INFO}; sessionInfo.next=&binding; sessionInfo.systemId=systemId;
            if(!Check("xrCreateSession(DXVK Vulkan)",xrCreateSession(instance,&sessionInfo,&session))) return false;
        }
        else
        {
        PFN_xrGetD3D11GraphicsRequirementsKHR getRequirements = nullptr;
        if (!Check("xrGetInstanceProcAddr(D3D11 requirements)", xrGetInstanceProcAddr(instance,
            "xrGetD3D11GraphicsRequirementsKHR",
            reinterpret_cast<PFN_xrVoidFunction*>(&getRequirements))) || !getRequirements) return false;
        XrGraphicsRequirementsD3D11KHR requirements{XR_TYPE_GRAPHICS_REQUIREMENTS_D3D11_KHR};
        if (!Check("xrGetD3D11GraphicsRequirementsKHR",
            getRequirements(instance, systemId, &requirements))) return false;

        IDXGIFactory1* factory = nullptr;
        HRESULT hr = CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory));
        if (FAILED(hr)) { Log("OpenXR fallback: DXGI factory creation failed"); return false; }
        IDXGIAdapter1* adapter = nullptr;
        for (UINT index = 0; ; ++index)
        {
            IDXGIAdapter1* candidate = nullptr;
            hr = factory->EnumAdapters1(index, &candidate);
            if (hr == DXGI_ERROR_NOT_FOUND) break;
            if (FAILED(hr)) break;
            DXGI_ADAPTER_DESC1 desc{};
            candidate->GetDesc1(&desc);
            if (!std::memcmp(&desc.AdapterLuid, &requirements.adapterLuid, sizeof(LUID)))
            {
                adapter = candidate;
                break;
            }
            candidate->Release();
        }
        factory->Release();
        if (!adapter) { Log("OpenXR fallback: runtime GPU adapter LUID not found"); return false; }
        D3D_FEATURE_LEVEL feature{};
        hr = D3D11CreateDevice(adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION,
            &device, &feature, &context);
        adapter->Release();
        if (FAILED(hr) || feature < requirements.minFeatureLevel)
        {
            Log("OpenXR fallback: D3D11 device or feature level unavailable");
            return false;
        }
        XrGraphicsBindingD3D11KHR binding{XR_TYPE_GRAPHICS_BINDING_D3D11_KHR};
        binding.device = device;
        XrSessionCreateInfo sessionInfo{XR_TYPE_SESSION_CREATE_INFO};
        sessionInfo.next = &binding;
        sessionInfo.systemId = systemId;
        if (!Check("xrCreateSession(D3D11)", xrCreateSession(instance, &sessionInfo, &session))) return false;
        }
        if(api.getDisplayRefreshRate)
        {
            float hz=0;
            if(XR_SUCCEEDED(api.getDisplayRefreshRate(session,&hz)) && std::isfinite(hz) && hz>=30 && hz<=240)
            {
                displayRefreshHz=hz;
                char line[100]; sprintf_s(line,"Headset display refresh rate %.3f Hz",hz); Log(line);
            }
        }
        XrReferenceSpaceCreateInfo spaceInfo{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
        spaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;
        spaceInfo.poseInReferenceSpace.orientation.w = 1;
        floorSpace=XR_SUCCEEDED(api.createReferenceSpace(session,&spaceInfo,&localSpace));
        if(floorSpace) Log("Tracking reference STAGE: floor-relative eye height");
        else
        {
            spaceInfo.referenceSpaceType=XR_REFERENCE_SPACE_TYPE_LOCAL;
            if(!Check("xrCreateReferenceSpace(LOCAL)",api.createReferenceSpace(session,&spaceInfo,&localSpace))) return false;
            Log("STAGE unavailable: LOCAL fallback has no calibrated floor height");
        }
        if(!CreateControllerActions()) Log("Controller actions unavailable; headset rendering retained without controller input");
        frameCount = 0;
        Log("OpenXR session created; game rendering remains flat-screen");
        return true;
    }

    void Fallback(const char* reason)
    {
        Log(reason);
        DestroyRuntime();
        permanentFailure = true;
        Log("OpenXR disabled for this process; flat-screen game continues");
    }

    void OnEndScene()
    {
        if (!enabled || permanentFailure) return;
        VRProfile::Scope frameProfile(VRProfile::Metric::XRFrameWall);
        if (pendingRecreate)
        {
            if(vulkanEnabled && (!RageDirect3DDevice9::m_pRealDevice || !*RageDirect3DDevice9::m_pRealDevice)) return;
            pendingRecreate = false;
            if (!CreateRuntime()) { Fallback("OpenXR reset recreation failed"); return; }
        }
        XrEventDataBuffer event{XR_TYPE_EVENT_DATA_BUFFER};
        XrResult result;
        while ((result = xrPollEvent(instance, &event)) == XR_SUCCESS)
        {
            if (event.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED)
            {
                const auto& state = *reinterpret_cast<const XrEventDataSessionStateChanged*>(&event);
                sessionState = state.state;
                char line[96];
                sprintf_s(line, "OpenXR session state: %s (%d)", StateName(sessionState),
                    static_cast<int>(sessionState));
                Log(line);
                if (sessionState == XR_SESSION_STATE_READY)
                {
                    XrSessionBeginInfo begin{XR_TYPE_SESSION_BEGIN_INFO};
                    begin.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                    if (!Check("xrBeginSession", xrBeginSession(session, &begin)))
                    { Fallback("OpenXR begin session failed"); return; }
                    running = true;
                }
                else if (sessionState == XR_SESSION_STATE_STOPPING && running)
                {
                    if (!Check("xrEndSession", xrEndSession(session)))
                    { Fallback("OpenXR end session failed"); return; }
                    running = false;
                    gameplayPacing=false;
                    AcquireSRWLockExclusive(&eyeLock);
                    eyeViewsValid = false;
                    focusedEyeViews=false;
                    controllerInput={};
                    ReleaseSRWLockExclusive(&eyeLock);
                }
                else if (sessionState == XR_SESSION_STATE_LOSS_PENDING ||
                    sessionState == XR_SESSION_STATE_EXITING)
                { Fallback("OpenXR session lost or exiting"); return; }
            }
            else if (event.type == XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING)
            { Fallback("OpenXR instance loss pending"); return; }
            else if(event.type==XR_TYPE_EVENT_DATA_DISPLAY_REFRESH_RATE_CHANGED_FB && api.getDisplayRefreshRate)
            {
                const auto hz=reinterpret_cast<const XrEventDataDisplayRefreshRateChangedFB*>(&event)->toDisplayRefreshRate;
                if(std::isfinite(hz) && hz>=30 && hz<=240) displayRefreshHz=hz;
            }
            event = {XR_TYPE_EVENT_DATA_BUFFER};
        }
        if (result != XR_EVENT_UNAVAILABLE)
        { Fallback("xrPollEvent failed"); return; }
        if (!running) return;
        auto diagnostic=producerRequest.exchange(2);
        if(producerActive && (GetTickCount64()>=producerDeadline.load() || sessionState!=XR_SESSION_STATE_FOCUSED || !haveEyeImages))diagnostic=0;
        if(diagnostic!=2)
        {
            producerActive=diagnostic==1 && vulkanEnabled && haveEyeImages && sessionState==XR_SESSION_STATE_FOCUSED;
            producerDeadline=producerActive ? GetTickCount64()+15000:0;
            if(producerActive)producerPairs=0;
            Log(producerActive ? "producer-test on for15s: cached pose, no headset transfer/submission, completed-pair counter":"producer-test off: normal OpenXR restored");
        }
        // The provider fences this frame's D3D9 writes before D3D11 reads.
        // Relying on the preceding frame's fence races a reused eye texture.
        if(eyeImageProvider) eyeImageProvider();
        if(permanentFailure || !running) return;
        XrFrameWaitInfo wait{XR_TYPE_FRAME_WAIT_INFO};
        XrFrameState state{XR_TYPE_FRAME_STATE};
        VRProfile::BeforeXRWait();
        if(producerActive){gameplayPacing=true;return;}
        {
            VRProfile::Scope profile(VRProfile::Metric::XRWait);
            result = xrWaitFrame(session, &wait, &state);
        }
        if (XR_FAILED(result))
        { Check("xrWaitFrame", result); Fallback("OpenXR frame wait failed"); return; }
        SampleControllerInput(state.predictedDisplayTime);
        XrViewLocateInfo locate{XR_TYPE_VIEW_LOCATE_INFO};
        locate.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
        locate.displayTime = state.predictedDisplayTime;
        locate.space = localSpace;
        XrViewState viewState{XR_TYPE_VIEW_STATE};
        XrView views[2]{{XR_TYPE_VIEW}, {XR_TYPE_VIEW}};
        uint32_t viewCount = 0;
        {
            VRProfile::Scope profile(VRProfile::Metric::XRLocateViews);
            result = api.locateViews(session, &locate, &viewState, 2, &viewCount, views);
        }
        if (XR_FAILED(result))
        { Check("xrLocateViews", result); Fallback("OpenXR eye locate failed"); return; }
        const auto required = XR_VIEW_STATE_ORIENTATION_VALID_BIT | XR_VIEW_STATE_POSITION_VALID_BIT;
        AcquireSRWLockExclusive(&eyeLock);
        eyeViewsValid = viewCount == 2 && (viewState.viewStateFlags & required) == required;
        focusedEyeViews=eyeViewsValid && sessionState==XR_SESSION_STATE_FOCUSED;
        eyeSampledAt=GetTickCount64();
        if (eyeViewsValid)
        {
            for (unsigned i = 0; i < 2; ++i)
            {
                eyeViews[i] = {{views[i].pose.position.x, views[i].pose.position.y, views[i].pose.position.z},
                    {views[i].pose.orientation.x, views[i].pose.orientation.y, views[i].pose.orientation.z,
                        views[i].pose.orientation.w},
                    {views[i].fov.angleLeft, views[i].fov.angleRight, views[i].fov.angleUp, views[i].fov.angleDown},floorSpace};
            }
            eyeDisplayTime = state.predictedDisplayTime;
        }
        ReleaseSRWLockExclusive(&eyeLock);
        XrFrameBeginInfo begin{XR_TYPE_FRAME_BEGIN_INFO};
        {
            VRProfile::Scope profile(VRProfile::Metric::XRBegin);
            result = RuntimeQueueCall(VRProfile::Metric::XRQueueBegin,[&](){return xrBeginFrame(session, &begin);});
        }
        if (XR_FAILED(result))
        { Check("xrBeginFrame", result); Fallback("OpenXR frame begin failed"); return; }
        XrFrameEndInfo end{XR_TYPE_FRAME_END_INFO};
        end.displayTime = state.predictedDisplayTime;
        end.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
        XrCompositionLayerProjectionView projectionViews[2]{{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW},
            {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW}};
        XrCompositionLayerProjection layer{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
        const XrCompositionLayerBaseHeader* layers[]{reinterpret_cast<const XrCompositionLayerBaseHeader*>(&layer)};
        bool copied=haveEyeImages && state.shouldRender;
        gameplayPacing=haveEyeImages && state.shouldRender;
        for(unsigned eye=0;eye<2 && copied;++eye)
        {
            VRProfile::Scope profile(eye ? VRProfile::Metric::XRCopy1 : VRProfile::Metric::XRCopy0);
            auto& chain=eyeChains[eye];
            const auto imageWaitStart=VRProfile::Begin();
            XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
            uint32_t index=0;
            if(!CheckImageStep("xrAcquireSwapchainImage(eye)",RuntimeQueueCall(VRProfile::Metric::XRQueueAcquire,[&](){return api.acquireSwapchainImage(chain.handle,&acquire,&index);})))
                {copied=false; break;}
            XrSwapchainImageWaitInfo imageWait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
            imageWait.timeout=1000000000; // One second, including timeout as failure.
            const auto waited=api.waitSwapchainImage(chain.handle,&imageWait);
            VRProfile::End(eye ? VRProfile::Metric::XRImageWait1 : VRProfile::Metric::XRImageWait0,imageWaitStart);
            if(!CheckImageStep("xrWaitSwapchainImage(eye)",waited) || index>=(vulkanEnabled ? chain.vulkanImages.size() : chain.images.size()))
                {copied=false; break;}
            const auto copyFenceStart=VRProfile::Begin();
            bool gpuDone=false;
            if(vulkanEnabled)
            {
                const auto start=VRProfile::Begin();
                gpuDone=vulkanBridge && vulkanBridge->Copy(eye,chain.vulkanImages[index].image);
                VRProfile::End(eye ? VRProfile::Metric::XRCopyIssue1 : VRProfile::Metric::XRCopyIssue0,start);
            }
            else
            {
            const auto copyIssueStart=VRProfile::Begin();
            context->CopyResource(chain.images[index].texture,chain.shared.Get());
            VRProfile::End(eye ? VRProfile::Metric::XRCopyIssue1 : VRProfile::Metric::XRCopyIssue0,copyIssueStart);
            // Complete this diagnostic copy before releasing ownership to the
            // runtime. A bounded event query avoids relying on cross-device
            // driver scheduling for correctness.
            D3D11_QUERY_DESC queryDesc{D3D11_QUERY_EVENT,0};
            Microsoft::WRL::ComPtr<ID3D11Query> query;
            const auto queryStart=VRProfile::Begin();
            const auto created=device->CreateQuery(&queryDesc,&query);
            VRProfile::End(eye ? VRProfile::Metric::XRQueryCreate1 : VRProfile::Metric::XRQueryCreate0,queryStart);
            if(SUCCEEDED(created))
            {
                const auto flushStart=VRProfile::Begin();
                context->End(query.Get()); context->Flush();
                VRProfile::End(eye ? VRProfile::Metric::XRFlush1 : VRProfile::Metric::XRFlush0,flushStart);
                const auto deadline=GetTickCount64()+1000;
                const auto pollStart=VRProfile::Begin();
                HRESULT status=S_FALSE;
                // End/Flush above explicitly submits this event. The bounded
                // profiler experiment suppresses redundant implicit flushing
                // while polling; completion is still required before release.
                const UINT pollFlags=VRProfile::NonflushingPoll() ? D3D11_ASYNC_GETDATA_DONOTFLUSH : 0;
                do {status=context->GetData(query.Get(),nullptr,0,pollFlags); if(status==S_FALSE) Sleep(0);}
                while(status==S_FALSE && GetTickCount64()<deadline);
                gpuDone=status==S_OK;
                VRProfile::End(eye ? VRProfile::Metric::XRPoll1 : VRProfile::Metric::XRPoll0,pollStart);
            }
            }
            VRProfile::End(eye ? VRProfile::Metric::XRCopyFence1 : VRProfile::Metric::XRCopyFence0,copyFenceStart);
            XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
            const bool released=gpuDone && CheckImageStep("xrReleaseSwapchainImage(eye)",RuntimeQueueCall(VRProfile::Metric::XRQueueRelease,[&](){return api.releaseSwapchainImage(chain.handle,&release);}));
            if(!gpuDone || !released) {copied=false; Log("Eye transfer synchronization failed"); break;}
            auto& view=projectionViews[eye];
            const auto& source=imageViews[eye];
            view.pose={{source.orientation[0],source.orientation[1],source.orientation[2],source.orientation[3]},
                {source.position[0],source.position[1],source.position[2]}};
            view.fov={source.fov[0],source.fov[1],source.fov[2],source.fov[3]};
            view.subImage.swapchain=chain.handle;
            view.subImage.imageRect={{0,0},{static_cast<int32_t>(imageWidth),static_cast<int32_t>(imageHeight)}};
            view.subImage.imageArrayIndex=0;
        }
        if(copied)
        {
            layer.space=localSpace;
            layer.viewCount=2;
            layer.views=projectionViews;
            end.layerCount=1; end.layers=layers;
        }
        else if(haveEyeImages && state.shouldRender)
        {
            // Destroying the session releases any outstanding acquisition.
            Fallback("OpenXR eye transfer failed; returning to desktop"); return;
        }
        {
            VRProfile::Scope profile(VRProfile::Metric::XREnd);
            result = RuntimeQueueCall(VRProfile::Metric::XRQueueEnd,[&](){return xrEndFrame(session, &end);});
        }
        if (XR_FAILED(result))
        { Check("xrEndFrame", result); Fallback("OpenXR frame end failed"); return; }
        if(copied)
        {
            if(imagePoseTime>0 && state.predictedDisplayTime>=imagePoseTime)
                VRProfile::Record(VRProfile::Metric::PosePredictionLag,VRProfile::Begin().epoch,
                    static_cast<double>(state.predictedDisplayTime-imagePoseTime)/1'000'000.0);
            VRProfile::Submission();
            const auto now=GetTickCount64();
            if(!rateWindowStart) {rateWindowStart=now; rateWindowLayers=0;}
            ++rateWindowLayers;
            if(now-rateWindowStart>=5000)
            {
                submittedHz=1000.0f*rateWindowLayers/static_cast<float>(now-rateWindowStart);
                rateWindowStart=now; rateWindowLayers=0;
            }
        }
        if(copied && (++submittedFrames==1 || submittedFrames%600==0))
        {
            char line[180];
            sprintf_s(line,"Submitted stereo layer %u sourcePoseTime=%lld displayTime=%lld size=%ux%u",
                submittedFrames,imagePoseTime,static_cast<long long>(state.predictedDisplayTime),imageWidth,imageHeight);
            Log(line);
        }
        if (++frameCount == 1 || frameCount % 600 == 0)
        {
            char line[120];
            sprintf_s(line, "OpenXR frame %u predictedDisplayTime=%lld shouldRender=%u",
                frameCount, static_cast<long long>(state.predictedDisplayTime), state.shouldRender);
            Log(line);
        }
    }
}

void OpenXRVR::Shutdown()
{
    producerRequest=2;producerActive=false;producerDeadline=0;
    enabled = false;
    pendingRecreate = false;
    DestroyRuntime();
    Log("OpenXR explicit shutdown complete");
}

std::string OpenXRVR::Status()
{
    char text[360];
    sprintf_s(text, "{\"enabled\":%s,\"running\":%s,\"failed\":%s,\"sessionState\":\"%s\",\"frames\":%u,\"eyeImages\":%s,\"stereoLayers\":%u,\"displayRefreshHz\":%.3f,\"submittedHz\":%.2f,\"graphicsBackend\":\"%s\"}",
        enabled ? "true" : "false", running ? "true" : "false",
        permanentFailure ? "true" : "false", StateName(sessionState), frameCount,
        haveEyeImages ? "true" : "false",submittedFrames,displayRefreshHz.load(),submittedHz.load(),vulkanEnabled ? "DXVK-Vulkan" : "D3D9Ex-D3D11");
    return text;
}

bool OpenXRVR::RequestProducerTest(bool enable)
{
    if(enable && (!enabled || !running || permanentFailure || !vulkanEnabled || !haveEyeImages))return false;
    producerRequest=enable ? 1:0;return true;
}

std::string OpenXRVR::ProducerTestStatus()
{
    const auto now=GetTickCount64(),deadline=producerDeadline.load();char text[280];
    sprintf_s(text,"{\"ok\":true,\"active\":%s,\"remainingMs\":%llu,\"completedPairs\":%llu,\"cachedPose\":true,\"headsetSubmissions\":false,\"gpuCompletionVerified\":false}",
        producerActive.load() ? "true":"false",deadline>now ? deadline-now:0,producerPairs.load());return text;
}

bool OpenXRVR::PacesGameplay()
{
    // The native renderer can stop executing while simulation continues.
    // xrWaitFrame no longer provides backpressure in that case (#029).
    AcquireSRWLockShared(&eyeLock);
    const bool fresh=eyeSampledAt && GetTickCount64()-eyeSampledAt<=250;
    ReleaseSRWLockShared(&eyeLock);
    return producerActive.load() || (gameplayPacing.load() && fresh);
}

bool OpenXRVR::GetEyeViews(EyeView (&views)[2], long long& displayTime)
{
    AcquireSRWLockShared(&eyeLock);
    // Never keep overriding the game camera from an indefinitely cached pose
    // when the EndScene/OpenXR producer stops. Ordinary rendering may recover
    // while simulation is still advancing; fresh views enable stereo again.
    const auto valid = eyeViewsValid && (producerActive.load() || (eyeSampledAt && GetTickCount64()-eyeSampledAt<=250));
    if (valid)
    {
        views[0] = eyeViews[0];
        views[1] = eyeViews[1];
        displayTime = eyeDisplayTime;
    }
    ReleaseSRWLockShared(&eyeLock);
    return valid;
}

bool OpenXRVR::GetControllerInput(ControllerInput& input)
{
    AcquireSRWLockShared(&eyeLock); input=controllerInput; ReleaseSRWLockShared(&eyeLock);
    if(!input.sampledAt || GetTickCount64()-input.sampledAt>250) {input={}; return false;}
    return true;
}

bool OpenXRVR::HasFocusedViews()
{
    AcquireSRWLockShared(&eyeLock);
    const bool valid=eyeViewsValid && focusedEyeViews && (producerActive.load() || (eyeSampledAt && GetTickCount64()-eyeSampledAt<=250));
    ReleaseSRWLockShared(&eyeLock);
    return valid;
}

std::string OpenXRVR::InputStatus()
{
    ControllerInput input{};
    const bool fresh=GetControllerInput(input);
    char text[400];
    sprintf_s(text,"{\"enabled\":%s,\"ready\":%s,\"fresh\":%s,\"left\":{\"active\":%s,\"poseValid\":%s,\"click\":%s,\"pad\":[%.3f,%.3f]},\"right\":{\"active\":%s,\"poseValid\":%s,\"click\":%s,\"pad\":[%.3f,%.3f]}}",
        controllerEnabled ? "true" : "false",inputReady ? "true" : "false",fresh ? "true" : "false",
        input.active[0] ? "true" : "false",input.poseValid[0] ? "true" : "false",input.clicked[0] ? "true" : "false",input.trackpad[0][0],input.trackpad[0][1],
        input.active[1] ? "true" : "false",input.poseValid[1] ? "true" : "false",input.clicked[1] ? "true" : "false",input.trackpad[1][0],input.trackpad[1][1]);
    return text;
}

bool OpenXRVR::SetEyeImages(const HANDLE (&shared)[2], unsigned width,unsigned height,
    const EyeView (&views)[2],long long poseTime)
{
    if(!enabled || permanentFailure || !running || !device || !width || !height) return false;
    if(haveEyeImages && (imageWidth!=width || imageHeight!=height)) return false;
    if(!haveEyeImages)
    {
        if(!eyeChains) eyeChains=new EyeSwapchain[2];
        uint32_t count=0;
        if(!Check("xrEnumerateSwapchainFormats(count)",api.enumerateSwapchainFormats(session,0,&count,nullptr))) return false;
        std::vector<int64_t> formats(count);
        if(!Check("xrEnumerateSwapchainFormats(list)",api.enumerateSwapchainFormats(session,count,&count,formats.data()))) return false;
        // FusionFix's final color is display encoded. SteamVR's tested format
        // is SRGB; D3D11 permits a resource copy within this format family.
        const auto format=DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
        if(std::find(formats.begin(),formats.end(),format)==formats.end())
            {Log("OpenXR BGRA8 SRGB eye format unavailable"); return false;}
        for(unsigned eye=0;eye<2;++eye)
        {
            auto& chain=eyeChains[eye];
            const auto opened=shared[eye] ? device->OpenSharedResource(shared[eye],__uuidof(ID3D11Texture2D),
                reinterpret_cast<void**>(chain.shared.GetAddressOf())) : E_HANDLE;
            if(FAILED(opened))
            {
                char diagnostic[128];
                sprintf_s(diagnostic,"OpenSharedResource eye=%u HRESULT=0x%08lX",eye,static_cast<unsigned long>(opened));
                Log(diagnostic);
                Fallback("OpenXR could not open game eye texture on runtime GPU"); return false;
            }
            D3D11_TEXTURE2D_DESC desc{};
            chain.shared->GetDesc(&desc);
            if(desc.Width!=width || desc.Height!=height || desc.Format!=DXGI_FORMAT_B8G8R8A8_UNORM || desc.SampleDesc.Count!=1)
                {Fallback("OpenXR eye texture descriptor mismatch"); return false;}
            XrSwapchainCreateInfo create{XR_TYPE_SWAPCHAIN_CREATE_INFO};
            create.usageFlags=XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT|XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
            create.format=format; create.sampleCount=1;
            create.width=width; create.height=height; create.faceCount=1; create.arraySize=1; create.mipCount=1;
            if(!Check("xrCreateSwapchain(eye)",api.createSwapchain(session,&create,&chain.handle)))
                {Fallback("OpenXR eye swapchain creation failed"); return false;}
            uint32_t images=0;
            if(!Check("xrEnumerateSwapchainImages(count)",api.enumerateSwapchainImages(chain.handle,0,&images,nullptr)))
                {Fallback("OpenXR eye image enumeration failed"); return false;}
            chain.images.resize(images);
            for(auto& image:chain.images) image.type=XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR;
            if(!Check("xrEnumerateSwapchainImages(list)",api.enumerateSwapchainImages(chain.handle,images,&images,
                reinterpret_cast<XrSwapchainImageBaseHeader*>(chain.images.data()))))
                {Fallback("OpenXR eye image enumeration failed"); return false;}
            chain.sharedHandle=shared[eye];
        }
        imageWidth=width; imageHeight=height;
        Log("Game eye textures opened; two BGRA8 SRGB OpenXR swapchains created");
    }
    if(eyeChains[0].sharedHandle!=shared[0] || eyeChains[1].sharedHandle!=shared[1]) return false;
    imageViews[0]=views[0]; imageViews[1]=views[1]; imagePoseTime=poseTime;
    haveEyeImages=true;
    return true;
}

bool OpenXRVR::UsesVulkan() {return vulkanEnabled;}

bool OpenXRVR::SetVulkanEyeImages(IDirect3DTexture9* const (&textures)[2],unsigned width,unsigned height,
    const EyeView (&views)[2],long long poseTime)
{
    if(!vulkanEnabled || !enabled || permanentFailure || !running || !vulkanBridge || !width || !height) return false;
    if(haveEyeImages && (imageWidth!=width || imageHeight!=height)) return false;
    if(!haveEyeImages)
    {
        if(!vulkanBridge->SetTextures(textures,width,height)) {Fallback("Vulkan eye texture interop incompatible");return false;}
        if(!eyeChains) eyeChains=new EyeSwapchain[2];
        uint32_t count=0;
        if(XR_FAILED(api.enumerateSwapchainFormats(session,0,&count,nullptr))) return false;
        std::vector<int64_t> formats(count);
        if(XR_FAILED(api.enumerateSwapchainFormats(session,count,&count,formats.data())) ||
            std::find(formats.begin(),formats.end(),VK_FORMAT_B8G8R8A8_SRGB)==formats.end()) return false;
        for(unsigned eye=0;eye<2;++eye)
        {
            auto& chain=eyeChains[eye];
            XrSwapchainCreateInfo create{XR_TYPE_SWAPCHAIN_CREATE_INFO};
            create.usageFlags=XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT|XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
            create.format=VK_FORMAT_B8G8R8A8_SRGB; create.sampleCount=1;
            create.width=width; create.height=height; create.faceCount=1; create.arraySize=1; create.mipCount=1;
            if(!Check("xrCreateSwapchain(Vulkan eye)",api.createSwapchain(session,&create,&chain.handle)))
                {Fallback("Vulkan swapchain creation failed");return false;}
            uint32_t images=0;
            if(XR_FAILED(api.enumerateSwapchainImages(chain.handle,0,&images,nullptr))) return false;
            chain.vulkanImages.resize(images);
            for(auto& image:chain.vulkanImages) image.type=XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR;
            if(!images || XR_FAILED(api.enumerateSwapchainImages(chain.handle,images,&images,
                reinterpret_cast<XrSwapchainImageBaseHeader*>(chain.vulkanImages.data())))) return false;
        }
        imageWidth=width; imageHeight=height; haveEyeImages=true;
        Log("Vulkan eye swapchains ready on game's DXVK device");
    }
    imageViews[0]=views[0]; imageViews[1]=views[1]; imagePoseTime=poseTime;
    if(producerActive.load())++producerPairs;
    return true;
}

bool OpenXRVR::CanAcceptEyeImages()
{
    return enabled && !permanentFailure && running && (vulkanEnabled ? vulkanBridge!=nullptr : device!=nullptr);
}

void OpenXRVR::SetEyeImageProvider(void (*provider)())
{
    eyeImageProvider=provider;
}

void OpenXRVR::Install()
{
    CIniReader ini("");
    enabled = ini.ReadInteger("VR", "EnableOpenXR", 0) != 0;
    vulkanEnabled=enabled && ini.ReadInteger("VR","ExperimentalVulkanBridge",0)!=0;
    controllerEnabled=ini.ReadInteger("VR","ExperimentalControllerInput",0)!=0;
    if (!enabled) return;
    wchar_t path[MAX_PATH]{};
    if (GetModuleFileNameW(nullptr, path, MAX_PATH))
    {
        wchar_t* name = wcsrchr(path, L'\\');
        if (name)
        {
            name[1] = L'\0';
            if (wcscat_s(path, L"plugins\\GTAIV.VR.openxr.log") == 0)
                logFile = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                    CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        }
    }
    Log("OpenXR enabled by [VR] EnableOpenXR=1; initializing x86 session");
    if(vulkanEnabled)
    {
        if(CIniReader(CSettings::d3d9cfgPath).ReadInteger("MAIN","API",0)!=1)
            {Fallback("Experimental Vulkan bridge requires d3d9.cfg API=1"); return;}
        const auto diagnostic=ini.ReadString("DEVELOPMENT","VulkanDebugMode","");
        if(diagnostic=="hang" || diagnostic=="validation" || diagnostic=="markers")
        {
            SetEnvironmentVariableA("DXVK_DEBUG",diagnostic.c_str());
            Log("DXVK diagnostics enabled for this process; do not use for performance comparisons");
        }
        const auto path=ini.ReadString("VR","VulkanOpenVRBootstrap","");
        if(!path.empty())
        {
            const auto absolute=std::filesystem::path(path);
            if(!absolute.is_absolute()) {Fallback("Vulkan bootstrap DLL path must be absolute"); return;}
            openvrBootstrap=LoadLibraryExW(absolute.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
            if(!openvrBootstrap) {Fallback("Vulkan OpenVR bootstrap preload failed"); return;}
        }
        pendingRecreate=true; // Bind the actual game device on first EndScene.
        Log("Experimental Vulkan bridge selected; waiting for game DXVK device");
    }
    else if (!CreateRuntime()) Fallback("OpenXR initialization failed");
    FusionFix::onEndScene() += []() { OnEndScene(); };
    FusionFix::onBeforeReset() += []()
    {
        if (!enabled || permanentFailure || pendingRecreate) return;
        Log("D3D9 reset: recreating OpenXR session on next EndScene");
        DestroyRuntime();
        pendingRecreate = true;
    };
    FusionFix::onShutdownEvent() += []()
    {
        // DllMain runs under the loader lock. Do not call OpenXR or FreeLibrary here;
        // the OS reclaims remaining objects at process exit.
        if (logFile != INVALID_HANDLE_VALUE)
        {
            CloseHandle(logFile);
            logFile = INVALID_HANDLE_VALUE;
        }
    };
}
