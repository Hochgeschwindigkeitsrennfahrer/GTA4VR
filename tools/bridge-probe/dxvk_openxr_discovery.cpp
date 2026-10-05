// Issue013: standalone x86 discovery. No game/config changes or headset submission.
// Interop slot ABI verified against DXVK v3.1.1 src/d3d9/d3d9_interfaces.h.
// Vulkan opaque handles only: this probe emits no Vulkan commands.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include <openxr/openxr.h>
#include <cstdio>
#include <cstring>
#include <vector>
#include <string>
#include <sstream>
#include <algorithm>
#include <wrl/client.h>
#pragma comment(lib,"user32.lib")
using Microsoft::WRL::ComPtr;
using VkOpaque=void*;
struct VulkanRequirements {
    XrStructureType type=XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN_KHR;
    void* next=nullptr;
    XrVersion minApiVersionSupported=0,maxApiVersionSupported=0;
};
// OpenXR Vulkan binding/image ABI; no Vulkan SDK or commands needed here.
struct VulkanBinding {
    XrStructureType type=XR_TYPE_GRAPHICS_BINDING_VULKAN_KHR;
    const void* next=nullptr;
    VkOpaque instance=nullptr,physicalDevice=nullptr,device=nullptr;
    uint32_t queueFamilyIndex=0,queueIndex=0;
};
struct VulkanSwapchainImage {
    XrStructureType type=XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR;
    void* next=nullptr;
    uint64_t image=0;
};
using GetRequirements=XrResult(XRAPI_PTR*)(XrInstance,XrSystemId,VulkanRequirements*);
using GetExtensions=XrResult(XRAPI_PTR*)(XrInstance,XrSystemId,uint32_t,uint32_t*,char*);
using GetPhysicalDevice=XrResult(XRAPI_PTR*)(XrInstance,XrSystemId,VkOpaque,VkOpaque*);
static_assert(sizeof(void*)==4,"Build this research probe for x86");
static_assert(sizeof(VulkanRequirements)==24);
static_assert(sizeof(VulkanBinding)==28 && sizeof(VulkanSwapchainImage)==16);
template<class Function> Function slot(IUnknown* object,unsigned index) {
    return reinterpret_cast<Function>((*reinterpret_cast<void***>(object))[index]);
}
#ifdef GTA4VR_VULKAN_COPY_PROBE
#include "vulkan_copy_check.h"
#endif
template<class Function> Function xrFunction(XrInstance instance,const char* name) {
    PFN_xrVoidFunction function=nullptr;
    const auto result=xrGetInstanceProcAddr(instance,name,&function);
    std::printf("%s lookup: %d\n",name,result);
    return XR_SUCCEEDED(result) ? reinterpret_cast<Function>(function) : nullptr;
}
static bool query(IUnknown* object,const char* name,const GUID& guid,ComPtr<IUnknown>& output) {
    const auto result=object->QueryInterface(guid,reinterpret_cast<void**>(output.GetAddressOf()));
    std::printf("%s QueryInterface: 0x%08lX\n",name,static_cast<unsigned long>(result));
    return SUCCEEDED(result);
}
static std::string extensions(GetExtensions function,XrInstance instance,XrSystemId system,bool& valid) {
    valid=false;
    uint32_t count=0;
    if(!function) return {};
    const auto result=function(instance,system,0,&count,nullptr);
    std::printf("Required extension count result: %d size=%u\n",result,count);
    if(XR_FAILED(result) || count>65536) return {};
    std::vector<char> text(count+1);
    if(XR_FAILED(function(instance,system,count,&count,text.data()))) return {};
    valid=true; return text.data();
}
int wmain(int argc,wchar_t** argv) {
    if(argc!=2 && argc!=3) {std::puts("Usage: dxvk_openxr_discovery.exe <absolute installed vulkan.dll path> [absolute x86 openvr_api.dll path]");return 1;}
    // DXVK's existing OpenVR provider can request SteamVR extensions before
    // Vulkan instance/device creation. This optional preload tests that route.
    if(argc==3 && !LoadLibraryExW(argv[2],nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS)) {
        std::printf("OpenVR preload failed: %lu\n",GetLastError()); return 2;
    }
    HMODULE module=LoadLibraryExW(argv[1],nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if(!module) {std::printf("DXVK load failed: %lu\n",GetLastError());return 2;}
    // Keep the module loaded until process exit, after all COM owners destruct.
    auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(GetProcAddress(module,"Direct3DCreate9"));
    if(!create) return 2;
    ComPtr<IDirect3D9> factory; factory.Attach(create(D3D_SDK_VERSION));
    if(!factory) return 2;
    D3DADAPTER_IDENTIFIER9 adapter{};
    factory->GetAdapterIdentifier(0,0,&adapter);
    std::printf("Adapter0: %s\n",adapter.Description);
    const GUID interfaceId{0xd6589ed4,0x7a37,0x4096,{0xba,0xc2,0x22,0x3b,0x25,0xae,0x31,0xd2}};
    const GUID deviceId{0x2eaa4b89,0x0107,0x4bdb,{0x87,0xf7,0x0f,0x54,0x1c,0x49,0x3c,0xe0}};
    const GUID textureId{0xd56344f5,0x8d35,0x46fd,{0x80,0x6d,0x94,0xc3,0x51,0xb4,0x72,0xc1}};
    ComPtr<IUnknown> instanceInterop;
    if(!query(factory.Get(),"DXVK instance interop1",interfaceId,instanceInterop)) return 3;
    using InstanceExtensions=HRESULT(STDMETHODCALLTYPE*)(IUnknown*,UINT*,const char**);
    const auto getEnabled=slot<InstanceExtensions>(instanceInterop.Get(),5);
    UINT enabledCount=0;
    if(FAILED(getEnabled(instanceInterop.Get(),&enabledCount,nullptr)) || enabledCount>1024) return 3;
    std::vector<const char*> enabled(enabledCount);
    // DXVK's list implementation may report MORE_DATA when supplied oversized capacity.
    const auto listResult=getEnabled(instanceInterop.Get(),&enabledCount,enabled.data());
    if(FAILED(listResult)) {std::printf("Instance extension list failed: %08lX\n",listResult);return 3;}
    for(unsigned i=0;i<enabledCount;++i) std::printf("DXVK enabled instance extension: %s\n",enabled[i]);

    HWND window=CreateWindowExW(0,L"STATIC",L"GTA4VR DXVK discovery",WS_OVERLAPPEDWINDOW,0,0,64,64,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    if(!window) return 4;
    D3DPRESENT_PARAMETERS parameters{};
    parameters.Windowed=TRUE; parameters.hDeviceWindow=window;
    parameters.BackBufferWidth=64; parameters.BackBufferHeight=64;
    parameters.BackBufferFormat=D3DFMT_A8R8G8B8; parameters.BackBufferCount=1;
    parameters.SwapEffect=D3DSWAPEFFECT_DISCARD; parameters.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
    ComPtr<IDirect3DDevice9> device;
    const auto created=factory->CreateDevice(0,D3DDEVTYPE_HAL,window,
        D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_MULTITHREADED|D3DCREATE_FPU_PRESERVE,&parameters,&device);
    std::printf("DXVK CreateDevice: %08lX\n",created);
    if(FAILED(created)) {DestroyWindow(window); return 4;}
    ComPtr<IUnknown> interop;
    if(!query(device.Get(),"DXVK device interop",deviceId,interop)) return 5;
    using Handles=void(STDMETHODCALLTYPE*)(IUnknown*,VkOpaque*,VkOpaque*,VkOpaque*);
    using Queue=void(STDMETHODCALLTYPE*)(IUnknown*,VkOpaque*,uint32_t*,uint32_t*);
    VkOpaque vkInstance=nullptr,physical=nullptr,vkDevice=nullptr,vkQueue=nullptr;
    uint32_t queueIndex=0,queueFamily=0;
    slot<Handles>(interop.Get(),3)(interop.Get(),&vkInstance,&physical,&vkDevice);
    slot<Queue>(interop.Get(),4)(interop.Get(),&vkQueue,&queueIndex,&queueFamily);
    std::printf("Vulkan handles instance=%p physical=%p device=%p queue=%p family=%u index=%u\n",
        vkInstance,physical,vkDevice,vkQueue,queueFamily,queueIndex);
    if(!vkInstance || !physical || !vkDevice || !vkQueue) return 5;
    ComPtr<IDirect3DTexture9> texture;
    const auto textured=device->CreateTexture(64,64,1,D3DUSAGE_RENDERTARGET,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,&texture,nullptr);
    std::printf("DXVK eye-like texture: %08lX\n",textured);
    if(FAILED(textured)) return 5;
    ComPtr<IUnknown> textureInterop;
    if(!query(texture.Get(),"DXVK texture interop",textureId,textureInterop)) return 5;
    using ImageInfo=HRESULT(STDMETHODCALLTYPE*)(IUnknown*,uint64_t*,int32_t*,void*);
    uint64_t image=0; int32_t layout=0;
    const auto imaged=slot<ImageInfo>(textureInterop.Get(),3)(textureInterop.Get(),&image,&layout,nullptr);
    std::printf("Vulkan image discovery: %08lX image=%016llX layout=%d\n",imaged,image,layout);
    if(FAILED(imaged) || !image) return 5;

    uint32_t count=0;
    if(XR_FAILED(xrEnumerateInstanceExtensionProperties(nullptr,0,&count,nullptr))) return 6;
    std::vector<XrExtensionProperties> xrExtensions(count);
    for(auto& item:xrExtensions) item.type=XR_TYPE_EXTENSION_PROPERTIES;
    if(XR_FAILED(xrEnumerateInstanceExtensionProperties(nullptr,count,&count,xrExtensions.data()))) return 6;
    bool vulkan=false;
    for(const auto& item:xrExtensions) {
        if(std::strstr(item.extensionName,"vulkan")) std::printf("OpenXR extension: %s revision=%u\n",item.extensionName,item.extensionVersion);
        vulkan|=!std::strcmp(item.extensionName,"XR_KHR_vulkan_enable");
    }
    if(!vulkan) {std::puts("Legacy Vulkan graphics binding unavailable");return 6;}
    XrInstanceCreateInfo info{XR_TYPE_INSTANCE_CREATE_INFO};
    strcpy_s(info.applicationInfo.applicationName,"GTA4VR DXVK discovery");
    info.applicationInfo.apiVersion=XR_MAKE_VERSION(1,0,0);
    const char* extension="XR_KHR_vulkan_enable";
    info.enabledExtensionCount=1; info.enabledExtensionNames=&extension;
    XrInstance xrInstance=XR_NULL_HANDLE;
    const auto instanceResult=xrCreateInstance(&info,&xrInstance);
    std::printf("xrCreateInstance: %d\n",instanceResult);
    if(XR_FAILED(instanceResult)) return 6;
    bool passed=false;
    do {
        XrSystemGetInfo systemInfo{XR_TYPE_SYSTEM_GET_INFO}; systemInfo.formFactor=XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
        XrSystemId system=XR_NULL_SYSTEM_ID;
        const auto systemResult=xrGetSystem(xrInstance,&systemInfo,&system);
        std::printf("xrGetSystem: %d\n",systemResult);
        if(XR_FAILED(systemResult)) break;
        auto requirementsFn=xrFunction<GetRequirements>(xrInstance,"xrGetVulkanGraphicsRequirementsKHR");
        auto physicalFn=xrFunction<GetPhysicalDevice>(xrInstance,"xrGetVulkanGraphicsDeviceKHR");
        auto instanceExt=xrFunction<GetExtensions>(xrInstance,"xrGetVulkanInstanceExtensionsKHR");
        auto deviceExt=xrFunction<GetExtensions>(xrInstance,"xrGetVulkanDeviceExtensionsKHR");
        if(!requirementsFn || !physicalFn || !instanceExt || !deviceExt) break;
        VulkanRequirements requirements{};
        const auto required=requirementsFn(xrInstance,system,&requirements);
        std::printf("Vulkan requirements: %d min=%u.%u.%u max=%u.%u.%u\n",required,
            XR_VERSION_MAJOR(requirements.minApiVersionSupported),XR_VERSION_MINOR(requirements.minApiVersionSupported),XR_VERSION_PATCH(requirements.minApiVersionSupported),
            XR_VERSION_MAJOR(requirements.maxApiVersionSupported),XR_VERSION_MINOR(requirements.maxApiVersionSupported),XR_VERSION_PATCH(requirements.maxApiVersionSupported));
        if(XR_FAILED(required)) break;
        bool validInstance=false,validDevice=false;
        const auto requiredInstance=extensions(instanceExt,xrInstance,system,validInstance);
        const auto requiredDevice=extensions(deviceExt,xrInstance,system,validDevice);
        std::printf("OpenXR required instance extensions: %s\nOpenXR required device extensions: %s\n",requiredInstance.c_str(),requiredDevice.c_str());
        std::istringstream names(requiredInstance); std::string name; bool missing=false;
        while(names>>name) {
            const bool found=std::any_of(enabled.begin(),enabled.begin()+enabledCount,[&](const char* item){return name==item;});
            std::printf("Required instance extension %s: %s\n",name.c_str(),found ? "enabled":"MISSING");
            missing|=!found;
        }
        VkOpaque runtimePhysical=nullptr;
        const auto selected=physicalFn(xrInstance,system,vkInstance,&runtimePhysical);
        std::printf("Runtime physical device: %d %p match=%s\n",selected,runtimePhysical,runtimePhysical==physical ? "yes":"no");
        passed=!missing && validInstance && validDevice && XR_SUCCEEDED(selected) && runtimePhysical==physical && SUCCEEDED(imaged);
        if(!passed) break;
        VulkanBinding binding{};
        binding.instance=vkInstance; binding.physicalDevice=physical; binding.device=vkDevice;
        binding.queueFamilyIndex=queueFamily; binding.queueIndex=queueIndex;
        XrSessionCreateInfo sessionInfo{XR_TYPE_SESSION_CREATE_INFO};
        sessionInfo.systemId=system; sessionInfo.next=&binding;
        XrSession session=XR_NULL_HANDLE;
        const auto sessionResult=xrCreateSession(xrInstance,&sessionInfo,&session);
        std::printf("xrCreateSession(DXVK Vulkan device): %d\n",sessionResult);
        passed=XR_SUCCEEDED(sessionResult);
        if(passed) {
            uint32_t formatCount=0;
            auto result=xrEnumerateSwapchainFormats(session,0,&formatCount,nullptr);
            std::vector<int64_t> formats(formatCount);
            if(XR_SUCCEEDED(result)) result=xrEnumerateSwapchainFormats(session,formatCount,&formatCount,formats.data());
            for(auto format:formats) std::printf("OpenXR Vulkan swapchain format: %lld\n",format);
            constexpr int64_t bgraSrgb=50; // VK_FORMAT_B8G8R8A8_SRGB
            passed=XR_SUCCEEDED(result) && std::find(formats.begin(),formats.end(),bgraSrgb)!=formats.end();
            if(passed) {
                XrSwapchainCreateInfo chainInfo{XR_TYPE_SWAPCHAIN_CREATE_INFO};
                chainInfo.usageFlags=XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT|XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
#ifdef GTA4VR_VULKAN_COPY_PROBE
                chainInfo.usageFlags|=XR_SWAPCHAIN_USAGE_TRANSFER_SRC_BIT;
#endif
                chainInfo.format=bgraSrgb; chainInfo.sampleCount=1;
                chainInfo.width=64; chainInfo.height=64; chainInfo.faceCount=1;
                chainInfo.arraySize=1; chainInfo.mipCount=1;
                XrSwapchain chain=XR_NULL_HANDLE;
                result=xrCreateSwapchain(session,&chainInfo,&chain);
                std::printf("xrCreateSwapchain(Vulkan64x64): %d\n",result);
                passed=XR_SUCCEEDED(result);
                if(passed) {
                    uint32_t imageCount=0;
                    result=xrEnumerateSwapchainImages(chain,0,&imageCount,nullptr);
                    std::vector<VulkanSwapchainImage> images(imageCount);
                    if(XR_SUCCEEDED(result)) result=xrEnumerateSwapchainImages(chain,imageCount,&imageCount,reinterpret_cast<XrSwapchainImageBaseHeader*>(images.data()));
                    passed=XR_SUCCEEDED(result) && imageCount && std::all_of(images.begin(),images.end(),[](const auto& item){return item.image!=0;});
                    std::printf("Vulkan swapchain image enumeration: %d count=%u nonzero=%s\n",result,imageCount,passed ? "yes":"no");
#ifdef GTA4VR_VULKAN_COPY_PROBE
                    if(passed) passed=verifyVulkanCopies(device.Get(),texture.Get(),interop.Get(),textureInterop.Get(),
                        vkInstance,physical,vkDevice,vkQueue,queueFamily,chain,images);
#endif
                    const auto destroyed=xrDestroySwapchain(chain);
                    std::printf("xrDestroySwapchain: %d\n",destroyed);
                    passed=passed && XR_SUCCEEDED(destroyed);
                }
            }
            const auto destroyed=xrDestroySession(session);
            std::printf("xrDestroySession: %d\n",destroyed);
            passed=passed && XR_SUCCEEDED(destroyed);
        }
#ifdef GTA4VR_VULKAN_COPY_PROBE
        std::puts("Copy/readback probe only: headset frames and game performance remain unverified.");
#else
        std::puts("Discovery only: copies, GPU ownership/barriers, headset frames and game performance remain unverified.");
#endif
    } while(false);
    const auto destroyed=xrDestroyInstance(xrInstance);
    std::printf("xrDestroyInstance: %d\n",destroyed);
    passed=passed && XR_SUCCEEDED(destroyed);
    textureInterop.Reset(); texture.Reset(); interop.Reset(); device.Reset();
    DestroyWindow(window);
    std::puts(passed ? "PASS discovery prerequisites":"FAIL discovery prerequisites");
    return passed ? 0:7;
}
