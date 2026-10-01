// Standalone x86 SteamVR/OpenXR discovery probe for issue 004.
#define WIN32_LEAN_AND_MEAN
#define XR_USE_PLATFORM_WIN32
#define XR_USE_GRAPHICS_API_D3D11
#include <windows.h>
#include <d3d9.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <cstdio>
#include <cstring>
#include <vector>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3d9.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "user32.lib")

static bool check(const char* step, XrResult result)
{
    std::printf("%s: %d\n", step, result);
    return XR_SUCCEEDED(result);
}

static bool tryD3D11Session(XrInstance instance, XrSystemId system,
    const XrGraphicsRequirementsD3D11KHR& requirements)
{
    IDXGIFactory1* factory = nullptr;
    IDXGIAdapter1* adapter = nullptr;
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    XrSession session = XR_NULL_HANDLE;
    XrSwapchain swapchain = XR_NULL_HANDLE;
    ID3D11Texture2D* source = nullptr;
    ID3D11Texture2D* staging = nullptr;
    IDirect3D9Ex* d3d9 = nullptr;
    IDirect3DDevice9Ex* dev9 = nullptr;
    IDirect3DTexture9* tex9 = nullptr;
    IDirect3DSurface9* surf9 = nullptr;
    IDirect3DQuery9* query9 = nullptr;
    HWND window = nullptr;
    bool success = false;
    do {
        HRESULT hr = CreateDXGIFactory1(__uuidof(IDXGIFactory1),
            reinterpret_cast<void**>(&factory));
        std::printf("CreateDXGIFactory1: 0x%08lX\n", static_cast<unsigned long>(hr));
        if (FAILED(hr)) break;
        for (UINT i = 0; ; ++i) {
            IDXGIAdapter1* candidate = nullptr;
            hr = factory->EnumAdapters1(i, &candidate);
            if (hr == DXGI_ERROR_NOT_FOUND) break;
            if (FAILED(hr)) break;
            DXGI_ADAPTER_DESC1 desc{};
            candidate->GetDesc1(&desc);
            if (!std::memcmp(&desc.AdapterLuid, &requirements.adapterLuid, sizeof(LUID))) {
                adapter = candidate;
                std::printf("Matched OpenXR adapter LUID\n");
                break;
            }
            candidate->Release();
        }
        if (!adapter) { std::puts("OpenXR adapter LUID not found in DXGI"); break; }
        D3D_FEATURE_LEVEL feature{};
        hr = D3D11CreateDevice(adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION,
            &device, &feature, &context);
        std::printf("D3D11CreateDevice: 0x%08lX feature=0x%04X\n",
            static_cast<unsigned long>(hr), static_cast<unsigned>(feature));
        if (FAILED(hr) || feature < requirements.minFeatureLevel) break;
        XrGraphicsBindingD3D11KHR binding{XR_TYPE_GRAPHICS_BINDING_D3D11_KHR};
        binding.device = device;
        XrSessionCreateInfo info{XR_TYPE_SESSION_CREATE_INFO};
        info.next = &binding;
        info.systemId = system;
        if (!check("xrCreateSession(D3D11 binding)",
            xrCreateSession(instance, &info, &session))) break;
        uint32_t count = 0;
        if (!check("xrEnumerateSwapchainFormats(count)",
            xrEnumerateSwapchainFormats(session, 0, &count, nullptr))) break;
        std::vector<int64_t> formats(count);
        if (!check("xrEnumerateSwapchainFormats(list)",
            xrEnumerateSwapchainFormats(session, count, &count, formats.data()))) break;
        bool bgraSrgb = false;
        for (auto format : formats) {
            std::printf("  DXGI format: %lld\n", static_cast<long long>(format));
            if (format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB) bgraSrgb = true;
        }
        std::printf("Swapchain formats: %u; BGRA8_UNORM_SRGB: %s\n",
            count, bgraSrgb ? "available" : "missing");
        if (!bgraSrgb) break;
        XrSwapchainCreateInfo swapInfo{XR_TYPE_SWAPCHAIN_CREATE_INFO};
        swapInfo.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT |
            XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT | XR_SWAPCHAIN_USAGE_TRANSFER_SRC_BIT;
        swapInfo.format = DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
        swapInfo.sampleCount = 1;
        swapInfo.width = 64;
        swapInfo.height = 64;
        swapInfo.faceCount = 1;
        swapInfo.arraySize = 1;
        swapInfo.mipCount = 1;
        if (!check("xrCreateSwapchain(BGRA8 sRGB)",
            xrCreateSwapchain(session, &swapInfo, &swapchain))) break;
        uint32_t imageCount = 0;
        if (!check("xrEnumerateSwapchainImages(count)",
            xrEnumerateSwapchainImages(swapchain, 0, &imageCount, nullptr))) break;
        std::vector<XrSwapchainImageD3D11KHR> images(imageCount);
        for (auto& image : images) image.type = XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR;
        if (!check("xrEnumerateSwapchainImages(list)", xrEnumerateSwapchainImages(
            swapchain, imageCount, &imageCount,
            reinterpret_cast<XrSwapchainImageBaseHeader*>(images.data())))) break;
        uint32_t index = 0;
        XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
        if (!check("xrAcquireSwapchainImage", xrAcquireSwapchainImage(
            swapchain, &acquire, &index))) break;
        XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
        wait.timeout = XR_INFINITE_DURATION;
        if (!check("xrWaitSwapchainImage", xrWaitSwapchainImage(swapchain, &wait))) break;
        ID3D11Texture2D* target = images[index].texture;
        D3D11_TEXTURE2D_DESC desc{};
        target->GetDesc(&desc);
        std::printf("XR image: %ux%u format=%u bind=0x%X\n",
            desc.Width, desc.Height, static_cast<unsigned>(desc.Format), desc.BindFlags);
        if (desc.Width != 64 || desc.Height != 64 || desc.ArraySize != 1 ||
            desc.MipLevels != 1 || desc.SampleDesc.Count != 1) break;
        WNDCLASSW wc{};
        wc.lpfnWndProc = DefWindowProcW;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"GTA4VROpenXRBridgeProbe";
        if (!RegisterClassW(&wc)) break;
        window = CreateWindowW(wc.lpszClassName, L"openxr-bridge-probe", 0,
            0, 0, 64, 64, nullptr, nullptr, wc.hInstance, nullptr);
        if (!window) break;
        hr = Direct3DCreate9Ex(D3D_SDK_VERSION, &d3d9);
        std::printf("Direct3DCreate9Ex: 0x%08lX\n", static_cast<unsigned long>(hr));
        if (FAILED(hr)) break;
        LUID luid9{};
        hr = d3d9->GetAdapterLUID(D3DADAPTER_DEFAULT, &luid9);
        std::printf("D3D9Ex adapter LUID: %08lX:%08lX\n",
            static_cast<unsigned long>(luid9.HighPart),
            static_cast<unsigned long>(luid9.LowPart));
        if (FAILED(hr) || std::memcmp(&luid9, &requirements.adapterLuid, sizeof(LUID))) break;
        D3DPRESENT_PARAMETERS pp{};
        pp.BackBufferWidth = 64;
        pp.BackBufferHeight = 64;
        pp.BackBufferFormat = D3DFMT_A8R8G8B8;
        pp.BackBufferCount = 1;
        pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
        pp.hDeviceWindow = window;
        pp.Windowed = TRUE;
        pp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
        hr = d3d9->CreateDeviceEx(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, window,
            D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, nullptr, &dev9);
        std::printf("D3D9Ex CreateDeviceEx: 0x%08lX\n", static_cast<unsigned long>(hr));
        if (FAILED(hr)) break;
        HANDLE shared = nullptr;
        hr = dev9->CreateTexture(64, 64, 1, D3DUSAGE_RENDERTARGET,
            D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &tex9, &shared);
        std::printf("D3D9Ex CreateTexture(shared): 0x%08lX\n", static_cast<unsigned long>(hr));
        if (FAILED(hr) || !shared) break;
        if (FAILED(tex9->GetSurfaceLevel(0, &surf9))) break;
        if (FAILED(dev9->SetRenderTarget(0, surf9))) break;
        hr = dev9->Clear(0, nullptr, D3DCLEAR_TARGET,
            D3DCOLOR_ARGB(255, 51, 102, 204), 1.0f, 0);
        std::printf("D3D9Ex Clear: 0x%08lX\n", static_cast<unsigned long>(hr));
        if (FAILED(hr)) break;
        if (FAILED(dev9->CreateQuery(D3DQUERYTYPE_EVENT, &query9))) break;
        if (FAILED(query9->Issue(D3DISSUE_END))) break;
        const ULONGLONG deadline = GetTickCount64() + 5000;
        HRESULT queryResult;
        do {
            queryResult = query9->GetData(nullptr, 0, D3DGETDATA_FLUSH);
            if (queryResult == S_FALSE) Sleep(1);
        } while (queryResult == S_FALSE && GetTickCount64() < deadline);
        std::printf("D3D9Ex EVENT completion: 0x%08lX\n",
            static_cast<unsigned long>(queryResult));
        if (queryResult != S_OK) break;
        hr = device->OpenSharedResource(shared, __uuidof(ID3D11Texture2D),
            reinterpret_cast<void**>(&source));
        std::printf("D3D11 OpenSharedResource: 0x%08lX\n", static_cast<unsigned long>(hr));
        if (FAILED(hr)) break;
        D3D11_TEXTURE2D_DESC copyDesc = desc;
        copyDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        copyDesc.BindFlags = 0;
        copyDesc.Usage = D3D11_USAGE_STAGING;
        copyDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        copyDesc.MiscFlags = 0;
        hr = device->CreateTexture2D(&copyDesc, nullptr, &staging);
        std::printf("CreateTexture2D(staging): 0x%08lX\n", static_cast<unsigned long>(hr));
        if (FAILED(hr)) break;
        context->CopyResource(target, source);
        context->CopyResource(staging, target);
        D3D11_MAPPED_SUBRESOURCE mapped{};
        hr = context->Map(staging, 0, D3D11_MAP_READ, 0, &mapped);
        std::printf("Map(XR image readback): 0x%08lX\n", static_cast<unsigned long>(hr));
        if (FAILED(hr)) break;
        const auto* pixel = static_cast<const unsigned char*>(mapped.pData);
        std::printf("XR image readback BGRA: %02X %02X %02X %02X\n",
            pixel[0], pixel[1], pixel[2], pixel[3]);
        success = pixel[0] == 204 && pixel[1] == 102 &&
            pixel[2] == 51 && pixel[3] == 255;
        context->Unmap(staging, 0);
        XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
        if (!check("xrReleaseSwapchainImage", xrReleaseSwapchainImage(
            swapchain, &release))) success = false;
        std::printf("D3D9Ex -> D3D11 -> XR swapchain copy/readback: %s\n",
            success ? "PASS" : "FAIL");
    } while (false);
    if (query9) query9->Release();
    if (surf9) surf9->Release();
    if (tex9) tex9->Release();
    if (dev9) dev9->Release();
    if (d3d9) d3d9->Release();
    if (window) DestroyWindow(window);
    if (staging) staging->Release();
    if (source) source->Release();
    if (swapchain != XR_NULL_HANDLE) check("xrDestroySwapchain", xrDestroySwapchain(swapchain));
    if (session != XR_NULL_HANDLE) check("xrDestroySession", xrDestroySession(session));
    if (context) context->Release();
    if (device) device->Release();
    if (adapter) adapter->Release();
    if (factory) factory->Release();
    return success;
}

int main()
{
    uint32_t count = 0;
    if (!check("xrEnumerateInstanceExtensionProperties(count)",
        xrEnumerateInstanceExtensionProperties(nullptr, 0, &count, nullptr))) return 1;
    std::vector<XrExtensionProperties> extensions(count);
    for (auto& extension : extensions) extension.type = XR_TYPE_EXTENSION_PROPERTIES;
    if (!check("xrEnumerateInstanceExtensionProperties(list)",
        xrEnumerateInstanceExtensionProperties(nullptr, count, &count, extensions.data()))) return 1;
    bool d3d11 = false;
    for (const auto& extension : extensions) {
        if (!std::strcmp(extension.extensionName, XR_KHR_D3D11_ENABLE_EXTENSION_NAME))
            d3d11 = true;
    }
    std::printf("XR_KHR_D3D11_enable: %s\n", d3d11 ? "available" : "missing");
    if (!d3d11) return 1;

    XrInstanceCreateInfo create{XR_TYPE_INSTANCE_CREATE_INFO};
    strcpy_s(create.applicationInfo.applicationName, "GTA4VR bridge probe");
    create.applicationInfo.applicationVersion = 1;
    strcpy_s(create.applicationInfo.engineName, "standalone probe");
    create.applicationInfo.engineVersion = 1;
    // SteamVR's installed 32-bit runtime currently accepts OpenXR 1.0.
    create.applicationInfo.apiVersion = XR_MAKE_VERSION(1, 0, 0);
    const char* enabled[] = {XR_KHR_D3D11_ENABLE_EXTENSION_NAME};
    create.enabledExtensionCount = 1;
    create.enabledExtensionNames = enabled;
    XrInstance instance = XR_NULL_HANDLE;
    if (!check("xrCreateInstance(D3D11 extension)", xrCreateInstance(&create, &instance))) return 1;

    XrInstanceProperties properties{XR_TYPE_INSTANCE_PROPERTIES};
    if (check("xrGetInstanceProperties", xrGetInstanceProperties(instance, &properties)))
        std::printf("Runtime: %s version %u.%u.%u\n", properties.runtimeName,
            XR_VERSION_MAJOR(properties.runtimeVersion), XR_VERSION_MINOR(properties.runtimeVersion),
            XR_VERSION_PATCH(properties.runtimeVersion));

    XrSystemGetInfo systemInfo{XR_TYPE_SYSTEM_GET_INFO};
    systemInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    XrSystemId system = XR_NULL_SYSTEM_ID;
    XrResult systemResult = xrGetSystem(instance, &systemInfo, &system);
    check("xrGetSystem(HMD)", systemResult);
    bool sessionResult = false;
    if (XR_SUCCEEDED(systemResult)) {
        XrSystemProperties systemProperties{XR_TYPE_SYSTEM_PROPERTIES};
        if (check("xrGetSystemProperties", xrGetSystemProperties(instance, system,
            &systemProperties)))
            std::printf("System: %s\n", systemProperties.systemName);
        PFN_xrGetD3D11GraphicsRequirementsKHR requirementsFn = nullptr;
        if (check("xrGetInstanceProcAddr(D3D11 requirements)", xrGetInstanceProcAddr(
            instance, "xrGetD3D11GraphicsRequirementsKHR",
            reinterpret_cast<PFN_xrVoidFunction*>(&requirementsFn))) && requirementsFn) {
            XrGraphicsRequirementsD3D11KHR requirements{XR_TYPE_GRAPHICS_REQUIREMENTS_D3D11_KHR};
            if (check("xrGetD3D11GraphicsRequirementsKHR",
                requirementsFn(instance, system, &requirements)))
            {
                std::printf("D3D11 adapter LUID: %08lX:%08lX minFeatureLevel=0x%04X\n",
                    static_cast<unsigned long>(requirements.adapterLuid.HighPart),
                    static_cast<unsigned long>(requirements.adapterLuid.LowPart),
                    static_cast<unsigned>(requirements.minFeatureLevel));
                sessionResult = tryD3D11Session(instance, system, requirements);
            }
        }
    }
    check("xrDestroyInstance", xrDestroyInstance(instance));
    return sessionResult ? 0 : 2;
}
