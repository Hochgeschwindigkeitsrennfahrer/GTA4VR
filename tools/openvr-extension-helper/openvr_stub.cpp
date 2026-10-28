#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <string.h>

/* Answers DXVK's OpenVR extension query without starting SteamVR.
   The OpenXR loader keeps using the runtime selected in Windows. */

static uint32_t WriteExtensions(const char* text, char* buffer, uint32_t size)
{
    const uint32_t need = (uint32_t)strlen(text) + 1;
    if (buffer && size >= need)
        memcpy(buffer, text, need);
    return need;
}

static const char kInstanceExtensions[] =
    "VK_KHR_external_memory_capabilities "
    "VK_KHR_external_semaphore_capabilities "
    "VK_KHR_external_fence_capabilities "
    "VK_KHR_get_physical_device_properties2";

static const char kDeviceExtensions[] =
    "VK_KHR_external_memory "
    "VK_KHR_external_memory_win32 "
    "VK_KHR_external_semaphore "
    "VK_KHR_external_semaphore_win32 "
    "VK_KHR_external_fence "
    "VK_KHR_external_fence_win32 "
    "VK_KHR_timeline_semaphore "
    "VK_KHR_dedicated_allocation "
    "VK_KHR_get_memory_requirements2";

class FakeCompositor
{
public:
#define STUB(n) virtual void Unused##n() {}
    STUB(0) STUB(1) STUB(2) STUB(3) STUB(4) STUB(5) STUB(6) STUB(7) STUB(8) STUB(9)
    STUB(10) STUB(11) STUB(12) STUB(13) STUB(14) STUB(15) STUB(16) STUB(17) STUB(18) STUB(19)
    STUB(20) STUB(21) STUB(22) STUB(23) STUB(24) STUB(25) STUB(26) STUB(27) STUB(28) STUB(29)
    STUB(30) STUB(31) STUB(32) STUB(33) STUB(34) STUB(35) STUB(36) STUB(37) STUB(38)
#undef STUB
    virtual uint32_t GetVulkanInstanceExtensionsRequired(char* value, uint32_t size)
    {
        return WriteExtensions(kInstanceExtensions, value, size);
    }
    virtual uint32_t GetVulkanDeviceExtensionsRequired(void*, char* value, uint32_t size)
    {
        return WriteExtensions(kDeviceExtensions, value, size);
    }
};

extern "C" __declspec(dllexport) void* __cdecl VR_GetGenericInterface(const char* version, int* error)
{
    if (version && strncmp(version, "IVRCompositor", 13) == 0)
    {
        static FakeCompositor compositor;
        if (error) *error = 0;
        return &compositor;
    }
    if (error) *error = 105;
    return nullptr;
}

extern "C" __declspec(dllexport) void* __cdecl VR_InitInternal(int* error, int)
{
    if (error) *error = 0;
    return nullptr;
}

extern "C" __declspec(dllexport) void __cdecl VR_ShutdownInternal() {}
