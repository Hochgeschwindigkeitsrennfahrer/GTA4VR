module;

#include <common.hxx>
#include <d3d11.h>
#include <dxgi1_2.h>
#define XR_USE_PLATFORM_WIN32
#define XR_USE_GRAPHICS_API_D3D11
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

export module openxrvr;

import common;

export namespace OpenXRVR
{
    struct EyeView
    {
        float position[3]{};
        float orientation[4]{}; // x, y, z, w in OpenXR's reference space.
        float fov[4]{}; // left, right, up, down angles in radians.
    };
    void Install();
    void Shutdown(); // Call from the EndScene callback thread, never DllMain.
    std::string Status();
    bool GetEyeViews(EyeView (&views)[2], long long& displayTime);
    bool CanAcceptEyeImages(); // Render callback thread; false while stopped or recreating.
    bool PacesGameplay(); // Atomic; true while the runtime requests stereo frames.
    bool SetEyeImages(const HANDLE (&shared)[2], unsigned width, unsigned height,
        const EyeView (&views)[2], long long poseTime); // Render callback thread after D3D9 GPU completion.
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
    SRWLOCK eyeLock = SRWLOCK_INIT;
    OpenXRVR::EyeView eyeViews[2]{};
    long long eyeDisplayTime = 0;
    bool eyeViewsValid = false;
    XrSystemId systemId = XR_NULL_SYSTEM_ID;
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    XrSessionState sessionState = XR_SESSION_STATE_UNKNOWN;
    bool running = false;
    bool enabled = false;
    bool pendingRecreate = false;
    bool permanentFailure = false;
    unsigned frameCount = 0;
    std::atomic<float> displayRefreshHz{0};
    std::atomic<bool> gameplayPacing{false};
    std::atomic<float> submittedHz{0};
    ULONGLONG rateWindowStart=0;
    unsigned rateWindowLayers=0;
    struct EyeSwapchain
    {
        XrSwapchain handle=XR_NULL_HANDLE;
        std::vector<XrSwapchainImageD3D11KHR> images;
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
#undef LOAD_XR
        return true;
    }

    void DestroyRuntime()
    {
        displayRefreshHz=0;
        gameplayPacing=false;
        submittedHz=0;
        rateWindowStart=0;
        rateWindowLayers=0;
        haveEyeImages=false;
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
        imageWidth=imageHeight=submittedFrames=0;
        AcquireSRWLockExclusive(&eyeLock);
        eyeViewsValid = false;
        ReleaseSRWLockExclusive(&eyeLock);
        running = false;
        sessionState = XR_SESSION_STATE_UNKNOWN;
        if (localSpace != XR_NULL_HANDLE)
        {
            Check("xrDestroySpace(LOCAL)", api.destroySpace(localSpace));
            localSpace = XR_NULL_HANDLE;
        }
        if (session != XR_NULL_HANDLE)
        {
            Check("xrDestroySession", xrDestroySession(session));
            session = XR_NULL_HANDLE;
        }
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
            hasD3D11 |= std::strcmp(item.extensionName, XR_KHR_D3D11_ENABLE_EXTENSION_NAME) == 0;
            hasRefreshRate |= std::strcmp(item.extensionName, XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME) == 0;
        }
        if (!hasD3D11) { Log("OpenXR fallback: XR_KHR_D3D11_enable unavailable"); return false; }

        XrInstanceCreateInfo create{XR_TYPE_INSTANCE_CREATE_INFO};
        strcpy_s(create.applicationInfo.applicationName, "GTA IV VR");
        create.applicationInfo.applicationVersion = 1;
        strcpy_s(create.applicationInfo.engineName, "FusionFix");
        create.applicationInfo.engineVersion = 1;
        create.applicationInfo.apiVersion = XR_MAKE_VERSION(1, 0, 0);
        const char* names[] = {XR_KHR_D3D11_ENABLE_EXTENSION_NAME,XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME};
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
        spaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
        spaceInfo.poseInReferenceSpace.orientation.w = 1;
        if (!Check("xrCreateReferenceSpace(LOCAL)", api.createReferenceSpace(session, &spaceInfo, &localSpace)))
            return false;
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
        if (pendingRecreate)
        {
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
        // The provider fences this frame's D3D9 writes before D3D11 reads.
        // Relying on the preceding frame's fence races a reused eye texture.
        if(eyeImageProvider) eyeImageProvider();
        if(permanentFailure || !running) return;

        XrFrameWaitInfo wait{XR_TYPE_FRAME_WAIT_INFO};
        XrFrameState state{XR_TYPE_FRAME_STATE};
        result = xrWaitFrame(session, &wait, &state);
        if (XR_FAILED(result))
        { Check("xrWaitFrame", result); Fallback("OpenXR frame wait failed"); return; }
        XrViewLocateInfo locate{XR_TYPE_VIEW_LOCATE_INFO};
        locate.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
        locate.displayTime = state.predictedDisplayTime;
        locate.space = localSpace;
        XrViewState viewState{XR_TYPE_VIEW_STATE};
        XrView views[2]{{XR_TYPE_VIEW}, {XR_TYPE_VIEW}};
        uint32_t viewCount = 0;
        result = api.locateViews(session, &locate, &viewState, 2, &viewCount, views);
        if (XR_FAILED(result))
        { Check("xrLocateViews", result); Fallback("OpenXR eye locate failed"); return; }
        const auto required = XR_VIEW_STATE_ORIENTATION_VALID_BIT | XR_VIEW_STATE_POSITION_VALID_BIT;
        AcquireSRWLockExclusive(&eyeLock);
        eyeViewsValid = viewCount == 2 && (viewState.viewStateFlags & required) == required;
        if (eyeViewsValid)
        {
            for (unsigned i = 0; i < 2; ++i)
            {
                eyeViews[i] = {{views[i].pose.position.x, views[i].pose.position.y, views[i].pose.position.z},
                    {views[i].pose.orientation.x, views[i].pose.orientation.y, views[i].pose.orientation.z,
                        views[i].pose.orientation.w},
                    {views[i].fov.angleLeft, views[i].fov.angleRight, views[i].fov.angleUp, views[i].fov.angleDown}};
            }
            eyeDisplayTime = state.predictedDisplayTime;
        }
        ReleaseSRWLockExclusive(&eyeLock);
        XrFrameBeginInfo begin{XR_TYPE_FRAME_BEGIN_INFO};
        result = xrBeginFrame(session, &begin);
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
            auto& chain=eyeChains[eye];
            XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
            uint32_t index=0;
            if(!CheckImageStep("xrAcquireSwapchainImage(eye)",api.acquireSwapchainImage(chain.handle,&acquire,&index)))
                {copied=false; break;}
            XrSwapchainImageWaitInfo imageWait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
            imageWait.timeout=1000000000; // One second, including timeout as failure.
            const auto waited=api.waitSwapchainImage(chain.handle,&imageWait);
            if(!CheckImageStep("xrWaitSwapchainImage(eye)",waited) || index>=chain.images.size())
                {copied=false; break;}
            context->CopyResource(chain.images[index].texture,chain.shared.Get());
            // Complete this diagnostic copy before releasing ownership to the
            // runtime. A bounded event query avoids relying on cross-device
            // driver scheduling for correctness.
            D3D11_QUERY_DESC queryDesc{D3D11_QUERY_EVENT,0};
            Microsoft::WRL::ComPtr<ID3D11Query> query;
            bool gpuDone=false;
            if(SUCCEEDED(device->CreateQuery(&queryDesc,&query)))
            {
                context->End(query.Get()); context->Flush();
                const auto deadline=GetTickCount64()+1000;
                HRESULT status=S_FALSE;
                do {status=context->GetData(query.Get(),nullptr,0,0); if(status==S_FALSE) Sleep(0);}
                while(status==S_FALSE && GetTickCount64()<deadline);
                gpuDone=status==S_OK;
            }
            XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
            const bool released=CheckImageStep("xrReleaseSwapchainImage(eye)",api.releaseSwapchainImage(chain.handle,&release));
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
        result = xrEndFrame(session, &end);
        if (XR_FAILED(result))
        { Check("xrEndFrame", result); Fallback("OpenXR frame end failed"); return; }
        if(copied)
        {
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
    enabled = false;
    pendingRecreate = false;
    DestroyRuntime();
    Log("OpenXR explicit shutdown complete");
}

std::string OpenXRVR::Status()
{
    char text[360];
    sprintf_s(text, "{\"enabled\":%s,\"running\":%s,\"failed\":%s,\"sessionState\":\"%s\",\"frames\":%u,\"eyeImages\":%s,\"stereoLayers\":%u,\"displayRefreshHz\":%.3f,\"submittedHz\":%.2f}",
        enabled ? "true" : "false", running ? "true" : "false",
        permanentFailure ? "true" : "false", StateName(sessionState), frameCount,
        haveEyeImages ? "true" : "false",submittedFrames,displayRefreshHz.load(),submittedHz.load());
    return text;
}

bool OpenXRVR::PacesGameplay()
{
    return gameplayPacing.load();
}

bool OpenXRVR::GetEyeViews(EyeView (&views)[2], long long& displayTime)
{
    AcquireSRWLockShared(&eyeLock);
    const auto valid = eyeViewsValid;
    if (valid)
    {
        views[0] = eyeViews[0];
        views[1] = eyeViews[1];
        displayTime = eyeDisplayTime;
    }
    ReleaseSRWLockShared(&eyeLock);
    return valid;
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

bool OpenXRVR::CanAcceptEyeImages()
{
    return enabled && !permanentFailure && running && device;
}

void OpenXRVR::SetEyeImageProvider(void (*provider)())
{
    eyeImageProvider=provider;
}

void OpenXRVR::Install()
{
    CIniReader ini("");
    enabled = ini.ReadInteger("VR", "EnableOpenXR", 0) != 0;
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
    if (!CreateRuntime()) Fallback("OpenXR initialization failed");
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
