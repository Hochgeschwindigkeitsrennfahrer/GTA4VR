module;

#include <common.hxx>
#include <d3d9.h>
#include <cstdio>
#include <cwchar>

export module vrd3d9ex;
import common;
import settings;

export namespace VRD3D9Ex { void Install(); void InstallDiagnostics(); }

namespace
{
    using Factory = IDirect3D9* (WINAPI*)(UINT);
    using FactoryEx = HRESULT (WINAPI*)(UINT, IDirect3D9Ex**);
    Factory originalFactory = nullptr;
    FactoryEx factoryEx = nullptr;
    SafetyHookInline createDeviceHook{};
    SafetyHookInline textureHook{};
    SafetyHookInline cubeHook{};
    SafetyHookInline vertexHook{};
    SafetyHookInline indexHook{};
    SafetyHookInline resetHook{};
    bool remapManagedBuffers = false;
    bool runtimePacing = false;

    void Log(const char* step, HRESULT result)
    {
        wchar_t path[MAX_PATH]{};
        if (!GetModuleFileNameW(nullptr, path, MAX_PATH)) return;
        auto* name = wcsrchr(path, L'\\');
        if (!name) return;
        name[1] = L'\0';
        if (wcscat_s(path, L"plugins\\GTAIV.VR.d3d9ex.log")) return;
        FILE* file = nullptr;
        if (!_wfopen_s(&file, path, L"a") && file)
        {
            fprintf(file, "%s: 0x%08lX\n", step, static_cast<unsigned long>(result));
            fclose(file);
        }
    }

    HRESULT WINAPI CreateTexture(IDirect3DDevice9* self, UINT width, UINT height,
        UINT levels, DWORD usage, D3DFORMAT format, D3DPOOL pool,
        IDirect3DTexture9** output, HANDLE* shared)
    {
        const auto result = textureHook.unsafe_stdcall<HRESULT>(self, width, height,
            levels, usage, format, pool, output, shared);
        if (FAILED(result) || pool == D3DPOOL_MANAGED) Log("CreateTexture (managed or failed)", result);
        return result;
    }

    HRESULT WINAPI CreateCubeTexture(IDirect3DDevice9* self, UINT size, UINT levels,
        DWORD usage, D3DFORMAT format, D3DPOOL pool, IDirect3DCubeTexture9** output, HANDLE* shared)
    {
        const auto result = cubeHook.unsafe_stdcall<HRESULT>(self, size, levels, usage,
            format, pool, output, shared);
        if (FAILED(result) || pool == D3DPOOL_MANAGED) Log("CreateCubeTexture (managed or failed)", result);
        return result;
    }

    HRESULT WINAPI CreateVertexBuffer(IDirect3DDevice9* self, UINT length, DWORD usage,
        DWORD fvf, D3DPOOL pool, IDirect3DVertexBuffer9** output, HANDLE* shared)
    {
        if (pool == D3DPOOL_MANAGED)
        {
            char line[128];
            sprintf_s(line, "CreateVertexBuffer requested managed pool length=%u usage=0x%lX remap=%u",
                length, usage, remapManagedBuffers);
            Log(line, S_OK);
            if (remapManagedBuffers) pool = D3DPOOL_DEFAULT;
        }
        const auto result = vertexHook.unsafe_stdcall<HRESULT>(self, length, usage,
            fvf, pool, output, shared);
        if (FAILED(result) || pool == D3DPOOL_MANAGED) Log("CreateVertexBuffer (managed or failed)", result);
        return result;
    }

    HRESULT WINAPI CreateIndexBuffer(IDirect3DDevice9* self, UINT length, DWORD usage,
        D3DFORMAT format, D3DPOOL pool, IDirect3DIndexBuffer9** output, HANDLE* shared)
    {
        if (pool == D3DPOOL_MANAGED)
        {
            char line[128];
            sprintf_s(line, "CreateIndexBuffer requested managed pool length=%u usage=0x%lX remap=%u",
                length, usage, remapManagedBuffers);
            Log(line, S_OK);
            if (remapManagedBuffers) pool = D3DPOOL_DEFAULT;
        }
        const auto result = indexHook.unsafe_stdcall<HRESULT>(self, length, usage,
            format, pool, output, shared);
        if (FAILED(result) || pool == D3DPOOL_MANAGED) Log("CreateIndexBuffer (managed or failed)", result);
        return result;
    }

    LONG CALLBACK ObserveException(EXCEPTION_POINTERS* exception)
    {
        if (exception->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION)
        {
            MEMORY_BASIC_INFORMATION memory{};
            VirtualQuery(exception->ExceptionRecord->ExceptionAddress, &memory, sizeof(memory));
            char line[180];
            sprintf_s(line, "Access violation address=%p allocationBase=%p offset=0x%lX",
                exception->ExceptionRecord->ExceptionAddress, memory.AllocationBase,
                static_cast<unsigned long>(reinterpret_cast<uintptr_t>(exception->ExceptionRecord->ExceptionAddress) -
                    reinterpret_cast<uintptr_t>(memory.AllocationBase)));
            Log(line, E_FAIL);
            const auto* context=exception->ContextRecord;
            sprintf_s(line,"AV access=%lu target=%08lX eax=%08lX ecx=%08lX edx=%08lX esp=%08lX ebp=%08lX",
                static_cast<unsigned long>(exception->ExceptionRecord->ExceptionInformation[0]),
                static_cast<unsigned long>(exception->ExceptionRecord->ExceptionInformation[1]),
                context->Eax,context->Ecx,context->Edx,context->Esp,context->Ebp);
            Log(line,E_FAIL);
            unsigned char code[24]{};
            SIZE_T count=0;
            if(ReadProcessMemory(GetCurrentProcess(),exception->ExceptionRecord->ExceptionAddress,code,sizeof(code),&count))
            {
                char bytes[100]="AV instruction bytes:";
                for(unsigned i=0;i<count;++i) {char part[5]; sprintf_s(part," %02X",code[i]); strcat_s(bytes,part);}
                Log(bytes,E_FAIL);
            }
        }
        return EXCEPTION_CONTINUE_SEARCH;
    }


    HRESULT WINAPI Reset(IDirect3DDevice9* self,D3DPRESENT_PARAMETERS* parameters)
    {
        if(runtimePacing && parameters && parameters->Windowed)
            parameters->PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
        // The executable's reset hook has already released VR resources.
        // The deprecated D3D9 parameter event is not dispatched by this fork.
        return resetHook.unsafe_stdcall<HRESULT>(self,parameters);
    }

    HRESULT WINAPI CreateDevice(IDirect3D9* self, UINT adapter, D3DDEVTYPE type,
        HWND window, DWORD flags, D3DPRESENT_PARAMETERS* parameters, IDirect3DDevice9** output)
    {
        IDirect3D9Ex* ex = nullptr;
        if (parameters && output && parameters->Windowed &&
            SUCCEEDED(self->QueryInterface(__uuidof(IDirect3D9Ex), reinterpret_cast<void**>(&ex))))
        {
            IDirect3DDevice9Ex* device = nullptr;
            auto saved = *parameters;
            // The headset runtime supplies the stereo cadence. Desktop VSync
            // must not add a second wait to the same render thread.
            if(runtimePacing && type!=D3DDEVTYPE_NULLREF)
                parameters->PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
            const auto result = ex->CreateDeviceEx(adapter, type, window, flags,
                parameters, nullptr, &device);
            ex->Release();
            Log("CreateDeviceEx(windowed)", result);
            if (SUCCEEDED(result))
            {
                auto** vtable = *reinterpret_cast<void***>(device);
                if(runtimePacing && !resetHook) resetHook=safetyhook::create_inline(vtable[16],Reset);
                if (!textureHook) textureHook = safetyhook::create_inline(vtable[23], CreateTexture);
                if (!cubeHook) cubeHook = safetyhook::create_inline(vtable[25], CreateCubeTexture);
                if (!vertexHook) vertexHook = safetyhook::create_inline(vtable[26], CreateVertexBuffer);
                if (!indexHook) indexHook = safetyhook::create_inline(vtable[27], CreateIndexBuffer);
                *output = device;
                return result;
            }
            *parameters = saved;
        }
        Log("Using ordinary CreateDevice fallback", S_OK);
        return createDeviceHook.unsafe_stdcall<HRESULT>(self, adapter, type, window,
            flags, parameters, output);
    }

    IDirect3D9* WINAPI CreateFactory(UINT version)
    {
        IDirect3D9Ex* ex = nullptr;
        const auto result = factoryEx(version, &ex);
        Log("Direct3DCreate9Ex", result);
        if (SUCCEEDED(result) && ex)
        {
            if (!createDeviceHook)
            {
                auto** vtable = *reinterpret_cast<void***>(ex);
                createDeviceHook = safetyhook::create_inline(vtable[16], CreateDevice);
            }
            if (createDeviceHook) return ex;
            ex->Release();
        }
        return originalFactory(version);
    }
}

void VRD3D9Ex::Install()
{
    CIniReader ini("");
    if (!ini.ReadInteger("DEVELOPMENT", "ExperimentalD3D9Ex", 0)) return;
    // FusionFix's backend switch lives in d3d9.cfg, not the ASI INI.
    if(CIniReader(CSettings::d3d9cfgPath).ReadInteger("MAIN","API",0)!=0)
    {Log("Ex bridge requires system D3D9; DXVK selected",E_NOTIMPL); return;}
    runtimePacing=ini.ReadInteger("VR","EnableOpenXR",0)!=0 &&
        ini.ReadInteger("VR","ExperimentalStereo",0)!=0 &&
        ini.ReadInteger("VR","ExperimentalSubmit",0)!=0 &&
        ini.ReadInteger("VR","MatchHeadsetRefresh",1)!=0;
    remapManagedBuffers = ini.ReadInteger("DEVELOPMENT", "ExRemapManagedBuffers", 0) != 0;
    auto module = GetModuleHandleW(L"d3d9.dll");
    if (!module) return;
    factoryEx = reinterpret_cast<FactoryEx>(GetProcAddress(module, "Direct3DCreate9Ex"));
    if (!factoryEx) return;
    originalFactory = reinterpret_cast<Factory>(GetProcAddress(module, "Direct3DCreate9"));
    if (!originalFactory) return;
    IATHook::Replace(GetModuleHandleW(nullptr), "d3d9.dll",
        std::forward_as_tuple("Direct3DCreate9", CreateFactory));
    Log("Experimental Ex factory hook installed; windowed only", S_OK);
}

void VRD3D9Ex::InstallDiagnostics()
{
    CIniReader ini("");
    if (!ini.ReadInteger("DEVELOPMENT", "ExperimentalD3D9Ex", 0) ||
        !ini.ReadInteger("DEVELOPMENT", "ProbeGraphicsExceptions", 0)) return;
    AddVectoredExceptionHandler(1, ObserveException);
}
