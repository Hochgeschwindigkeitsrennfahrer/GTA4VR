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

export module openxrvr;

import common;

export namespace OpenXRVR
{
    void Install();
    void Shutdown(); // Call from the EndScene callback thread, never DllMain.
    std::string Status();
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
    XrSystemId systemId = XR_NULL_SYSTEM_ID;
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    XrSessionState sessionState = XR_SESSION_STATE_UNKNOWN;
    bool running = false;
    bool enabled = false;
    bool pendingRecreate = false;
    bool permanentFailure = false;
    unsigned frameCount = 0;

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
#undef LOAD_XR
        return true;
    }

    void DestroyRuntime()
    {
        running = false;
        sessionState = XR_SESSION_STATE_UNKNOWN;
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
        bool hasD3D11 = false;
        for (const auto& item : extensions)
            hasD3D11 |= std::strcmp(item.extensionName, XR_KHR_D3D11_ENABLE_EXTENSION_NAME) == 0;
        if (!hasD3D11) { Log("OpenXR fallback: XR_KHR_D3D11_enable unavailable"); return false; }

        XrInstanceCreateInfo create{XR_TYPE_INSTANCE_CREATE_INFO};
        strcpy_s(create.applicationInfo.applicationName, "GTA IV VR");
        create.applicationInfo.applicationVersion = 1;
        strcpy_s(create.applicationInfo.engineName, "FusionFix");
        create.applicationInfo.engineVersion = 1;
        create.applicationInfo.apiVersion = XR_MAKE_VERSION(1, 0, 0);
        const char* names[] = {XR_KHR_D3D11_ENABLE_EXTENSION_NAME};
        create.enabledExtensionCount = 1;
        create.enabledExtensionNames = names;
        if (!Check("xrCreateInstance", xrCreateInstance(&create, &instance))) return false;
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
                }
                else if (sessionState == XR_SESSION_STATE_LOSS_PENDING ||
                    sessionState == XR_SESSION_STATE_EXITING)
                { Fallback("OpenXR session lost or exiting"); return; }
            }
            else if (event.type == XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING)
            { Fallback("OpenXR instance loss pending"); return; }
            event = {XR_TYPE_EVENT_DATA_BUFFER};
        }
        if (result != XR_EVENT_UNAVAILABLE)
        { Fallback("xrPollEvent failed"); return; }
        if (!running) return;

        XrFrameWaitInfo wait{XR_TYPE_FRAME_WAIT_INFO};
        XrFrameState state{XR_TYPE_FRAME_STATE};
        result = xrWaitFrame(session, &wait, &state);
        if (XR_FAILED(result))
        { Check("xrWaitFrame", result); Fallback("OpenXR frame wait failed"); return; }
        XrFrameBeginInfo begin{XR_TYPE_FRAME_BEGIN_INFO};
        result = xrBeginFrame(session, &begin);
        if (XR_FAILED(result))
        { Check("xrBeginFrame", result); Fallback("OpenXR frame begin failed"); return; }
        XrFrameEndInfo end{XR_TYPE_FRAME_END_INFO};
        end.displayTime = state.predictedDisplayTime;
        end.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
        result = xrEndFrame(session, &end);
        if (XR_FAILED(result))
        { Check("xrEndFrame", result); Fallback("OpenXR frame end failed"); return; }
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
    char text[256];
    sprintf_s(text, "{\"enabled\":%s,\"running\":%s,\"failed\":%s,\"sessionState\":\"%s\",\"frames\":%u}",
        enabled ? "true" : "false", running ? "true" : "false",
        permanentFailure ? "true" : "false", StateName(sessionState), frameCount);
    return text;
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
