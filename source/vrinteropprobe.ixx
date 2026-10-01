module;

#include <common.hxx>
#include <d3d9.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <cstdio>
#include <cwchar>
#include <atomic>
#include <cstring>

export module vrinteropprobe;

import common;
import comvars;

export namespace VRInteropProbe { void Install(); }

namespace
{
    using Microsoft::WRL::ComPtr;
    FILE* log = nullptr;
    FILE* bindings = nullptr;
    SRWLOCK bindingLock = SRWLOCK_INIT;
    std::atomic<bool> captureBindings{false};
    SafetyHookInline setTarget{},setDepth{},setTexture{},clearTarget{},stretchTarget{};
    std::atomic<unsigned> renderStage{0};
    std::atomic<unsigned> renderFrame{0};
    std::atomic<unsigned> gameFrame{0};
    void Binding(const char*,unsigned,IDirect3DBaseTexture9*,IDirect3DSurface9*);

    class PhaseMarker : public CBaseDC
    {
        unsigned stage,frame;
    public:
        PhaseMarker(unsigned value,unsigned id):CBaseDC(),stage(value),frame(id) {}
        int32_t GetSize() override {return sizeof(PhaseMarker);}
        void DrawCommand() override
        {
            renderStage=stage;
            renderFrame=frame;
            if(!captureBindings) return;
            AcquireSRWLockExclusive(&bindingLock);
            if(bindings) fprintf(bindings,"marker thread=%lu stage=%u frame=%u\n",
                GetCurrentThreadId(),stage,frame);
            ReleaseSRWLockExclusive(&bindingLock);
            if(stage && captureBindings && RageDirect3DDevice9::m_pRealDevice)
            {
                auto* device=*RageDirect3DDevice9::m_pRealDevice;
                if(device)
                {
                    for(unsigned i=0;i<4;++i)
                    {
                        ComPtr<IDirect3DSurface9> surface;
                        if(SUCCEEDED(device->GetRenderTarget(i,&surface)))
                            Binding("BeginBoundColor",i,nullptr,surface.Get());
                    }
                    ComPtr<IDirect3DSurface9> depth;
                    if(SUCCEEDED(device->GetDepthStencilSurface(&depth)))
                        Binding("BeginBoundDepth",0,nullptr,depth.Get());
                }
            }
        }
    };

    void QueueStage(unsigned stage)
    {
        auto* marker=new PhaseMarker(stage,gameFrame.load());
        if(marker) marker->Append();
    }

    void Binding(const char* operation, unsigned index, IDirect3DBaseTexture9* texture,
        IDirect3DSurface9* surface)
    {
        if(!captureBindings) return;
        AcquireSRWLockExclusive(&bindingLock);
        if(bindings)
        {
            ComPtr<IDirect3DTexture9> container;
            if(surface) surface->GetContainer(__uuidof(IDirect3DTexture9),
                reinterpret_cast<void**>(container.GetAddressOf()));
            const char* name="unknown";
            auto* pointer=texture ? texture : container.Get();
            for(const auto& [key,target]:rage::grcTextureFactoryPC::RTCache)
                if(pointer && target && target->mD3DTexture==pointer) {name=key.c_str(); break;}
            // Material texture traffic is unrelated to target ownership. Keep
            // those draws out of this bounded diagnostic.
            if(std::strcmp(operation,"SetTexture")!=0 || std::strcmp(name,"unknown")!=0)
                fprintf(bindings,"thread=%lu operation=%s index=%u texture=%p surface=%p target=%s phase=%08X stage=%u frame=%u\n",
                    GetCurrentThreadId(),operation,index,pointer,surface,name,
                    CRenderPhase::sm_pCurrent ? *CRenderPhase::sm_pCurrent : 0,renderStage.load(),renderFrame.load());
        }
        ReleaseSRWLockExclusive(&bindingLock);
    }

    HRESULT WINAPI SetTarget(IDirect3DDevice9* self,DWORD index,IDirect3DSurface9* surface)
    {
        Binding("SetRenderTarget",index,nullptr,surface);
        return setTarget.unsafe_stdcall<HRESULT>(self,index,surface);
    }
    HRESULT WINAPI SetDepth(IDirect3DDevice9* self,IDirect3DSurface9* surface)
    {
        Binding("SetDepthStencilSurface",0,nullptr,surface);
        return setDepth.unsafe_stdcall<HRESULT>(self,surface);
    }
    HRESULT WINAPI SetTexture(IDirect3DDevice9* self,DWORD stage,IDirect3DBaseTexture9* texture)
    {
        Binding("SetTexture",stage,texture,nullptr);
        return setTexture.unsafe_stdcall<HRESULT>(self,stage,texture);
    }
    HRESULT WINAPI ClearTarget(IDirect3DDevice9* self,DWORD count,const D3DRECT* rectangles,
        DWORD flags,D3DCOLOR color,float z,DWORD stencil)
    {
        if(captureBindings)
        {
            AcquireSRWLockExclusive(&bindingLock);
            if(bindings) fprintf(bindings,"clear thread=%lu stage=%u frame=%u flags=0x%lX color=%08lX z=%g stencil=%lu rectCount=%lu\n",
                GetCurrentThreadId(),renderStage.load(),renderFrame.load(),flags,color,z,stencil,count);
            ReleaseSRWLockExclusive(&bindingLock);
        }
        return clearTarget.unsafe_stdcall<HRESULT>(self,count,rectangles,flags,color,z,stencil);
    }
    HRESULT WINAPI StretchTarget(IDirect3DDevice9* self,IDirect3DSurface9* source,const RECT* sourceRect,
        IDirect3DSurface9* destination,const RECT* destinationRect,D3DTEXTUREFILTERTYPE filter)
    {
        Binding("StretchRectSource",0,nullptr,source);
        Binding("StretchRectDestination",0,nullptr,destination);
        return stretchTarget.unsafe_stdcall<HRESULT>(self,source,sourceRect,destination,destinationRect,filter);
    }

    void StartBindings(IDirect3DDevice9* device)
    {
        wchar_t path[MAX_PATH]{};
        if(!GetModuleFileNameW(nullptr,path,MAX_PATH)) return;
        auto* name=wcsrchr(path,L'\\');
        if(!name) return;
        name[1]=0;
        if(wcscat_s(path,L"plugins\\GTAIV.VR.bindings.log")) return;
        if(_wfopen_s(&bindings,path,L"w") || !bindings) return;
        auto** table=*reinterpret_cast<void***>(device);
        setTarget=safetyhook::create_inline(table[37],SetTarget);
        setDepth=safetyhook::create_inline(table[39],SetDepth);
        setTexture=safetyhook::create_inline(table[65],SetTexture);
        clearTarget=safetyhook::create_inline(table[43],ClearTarget);
        stretchTarget=safetyhook::create_inline(table[34],StretchTarget);
        captureBindings=true;
    }

    void StopBindings()
    {
        captureBindings=false;
        AcquireSRWLockExclusive(&bindingLock);
        if(bindings) {fclose(bindings); bindings=nullptr;}
        ReleaseSRWLockExclusive(&bindingLock);
    }

    void Result(const char* operation, HRESULT result)
    {
        fprintf(log, "%s: 0x%08lX\n", operation, static_cast<unsigned long>(result));
    }

    void Describe(const char* label, IDirect3DSurface9* surface)
    {
        D3DSURFACE_DESC desc{};
        if (!surface) return;
        const auto result = surface->GetDesc(&desc);
        Result(label, result);
        if (SUCCEEDED(result))
            fprintf(log, "  size=%ux%u format=%u usage=0x%lX pool=%u msaa=%u quality=%lu\n",
                desc.Width, desc.Height, desc.Format, desc.Usage, desc.Pool,
                desc.MultiSampleType, desc.MultiSampleQuality);
    }

    unsigned long long HashRows(const void* data, UINT width, UINT height, UINT pitch)
    {
        unsigned long long hash = 14695981039346656037ULL;
        for (UINT y = 0; y < height; ++y)
            for (UINT x = 0; x < width * 4; ++x)
            {
                hash ^= static_cast<const unsigned char*>(data)[y * pitch + x];
                hash *= 1099511628211ULL;
            }
        return hash;
    }

    void VerifyShared(IDirect3DDevice9* game, IDirect3DSurface9* source,
        HANDLE shared, UINT width, UINT height)
    {
        LARGE_INTEGER start{}, finish{}, frequency{};
        QueryPerformanceFrequency(&frequency);
        QueryPerformanceCounter(&start);
        auto checked = [](const char* step, HRESULT result) { Result(step, result); return SUCCEEDED(result); };
        ComPtr<IDirect3DQuery9> completion;
        if (!checked("CreateQuery(EVENT)", game->CreateQuery(D3DQUERYTYPE_EVENT, &completion)) ||
            !checked("Issue(EVENT)", completion->Issue(D3DISSUE_END))) return;
        const auto deadline = GetTickCount64() + 1000;
        HRESULT result;
        while ((result = completion->GetData(nullptr, 0, D3DGETDATA_FLUSH)) == S_FALSE &&
            GetTickCount64() < deadline) Sleep(1);
        Result("D3D9 completion query", result);
        if (result != S_OK) return;

        ComPtr<IDirect3D9> factory9;
        ComPtr<IDirect3D9Ex> ex;
        D3DDEVICE_CREATION_PARAMETERS parameters{};
        LUID luid{};
        if (!checked("GetDirect3D", game->GetDirect3D(&factory9)) ||
            !checked("Factory QueryInterface(Ex)", factory9.As(&ex)) ||
            !checked("GetCreationParameters(bridge)", game->GetCreationParameters(&parameters)) ||
            !checked("GetAdapterLUID", ex->GetAdapterLUID(parameters.AdapterOrdinal, &luid))) return;
        fprintf(log, "gameAdapterLUID=%08lX:%08lX\n", static_cast<unsigned long>(luid.HighPart), luid.LowPart);
        ComPtr<IDXGIFactory1> factory11;
        if (!checked("CreateDXGIFactory1", CreateDXGIFactory1(__uuidof(IDXGIFactory1),
            reinterpret_cast<void**>(factory11.GetAddressOf())))) return;
        ComPtr<IDXGIAdapter1> adapter;
        for (UINT i = 0; ; ++i)
        {
            ComPtr<IDXGIAdapter1> candidate;
            if (FAILED(factory11->EnumAdapters1(i, &candidate))) break;
            DXGI_ADAPTER_DESC1 desc{};
            if (SUCCEEDED(candidate->GetDesc1(&desc)) && desc.AdapterLuid.HighPart == luid.HighPart &&
                desc.AdapterLuid.LowPart == luid.LowPart) { adapter = candidate; break; }
        }
        if (!adapter) { fprintf(log, "No D3D11 adapter matching game LUID\n"); return; }
        ComPtr<ID3D11Device> device11;
        ComPtr<ID3D11DeviceContext> context11;
        if (!checked("D3D11CreateDevice(game adapter)", D3D11CreateDevice(adapter.Get(),
            D3D_DRIVER_TYPE_UNKNOWN, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
            D3D11_SDK_VERSION, &device11, nullptr, &context11))) return;
        ComPtr<ID3D11Texture2D> shared11;
        if (!checked("OpenSharedResource(game texture)", device11->OpenSharedResource(shared,
            __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(shared11.GetAddressOf())))) return;
        D3D11_TEXTURE2D_DESC desc{};
        shared11->GetDesc(&desc);
        fprintf(log, "sharedD3D11 size=%ux%u format=%u\n", desc.Width, desc.Height, desc.Format);
        if (desc.Width != width || desc.Height != height || desc.Format != DXGI_FORMAT_B8G8R8A8_UNORM)
            return;
        desc.Usage = D3D11_USAGE_STAGING;
        desc.BindFlags = 0;
        desc.MiscFlags = 0;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Texture2D> staging;
        if (!checked("CreateTexture2D(bridge readback)", device11->CreateTexture2D(&desc, nullptr, &staging))) return;
        context11->CopyResource(staging.Get(), shared11.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (!checked("Map(D3D11 readback)", context11->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) return;
        const auto hash11 = HashRows(mapped.pData, width, height, mapped.RowPitch);
        context11->Unmap(staging.Get(), 0);

        ComPtr<IDirect3DSurface9> readback9;
        if (!checked("CreateOffscreenPlainSurface(shared readback)", game->CreateOffscreenPlainSurface(
            width, height, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &readback9, nullptr)) ||
            !checked("GetRenderTargetData(shared)", game->GetRenderTargetData(source, readback9.Get()))) return;
        D3DLOCKED_RECT locked{};
        if (!checked("LockRect(shared readback)", readback9->LockRect(&locked, nullptr, D3DLOCK_READONLY))) return;
        const auto hash9 = HashRows(locked.pBits, width, height, locked.Pitch);
        readback9->UnlockRect();
        QueryPerformanceCounter(&finish);
        fprintf(log, "D3D9 hash=%016llX D3D11 hash=%016llX equal=%u\n", hash9, hash11, hash9 == hash11);
        fprintf(log, "oneShotVerificationMs=%.3f (includes device creation, sync, two readbacks and hashing)\n",
            1000.0 * (finish.QuadPart - start.QuadPart) / frequency.QuadPart);
    }

    void Probe(IDirect3DDevice9* device)
    {
        wchar_t path[MAX_PATH]{};
        if (!GetModuleFileNameW(nullptr, path, MAX_PATH)) return;
        auto* name = wcsrchr(path, L'\\');
        if (!name) return;
        name[1] = L'\0';
        if (wcscat_s(path, L"plugins\\GTAIV.VR.interop.log")) return;
        if (_wfopen_s(&log, path, L"w") || !log) return;
        fprintf(log, "Issue 006 live-device probe; no camera or target bindings changed\n");
        for (const auto& [targetName, target] : rage::grcTextureFactoryPC::RTCache)
        {
            if (!target) continue;
            fprintf(log, "target=%s object=%p texture=%p surface=%p\n", targetName.c_str(),
                target, target->mD3DTexture, target->mD3DSurface);
            if (target->mD3DTexture)
            {
                D3DSURFACE_DESC targetDesc{};
                const auto targetResult = target->mD3DTexture->GetLevelDesc(0, &targetDesc);
                Result(targetName.c_str(), targetResult);
                if (SUCCEEDED(targetResult))
                    fprintf(log, "  size=%ux%u format=%u usage=0x%lX pool=%u msaa=%u\n",
                        targetDesc.Width, targetDesc.Height, targetDesc.Format,
                        targetDesc.Usage, targetDesc.Pool, targetDesc.MultiSampleType);
            }
        }

        IDirect3DDevice9Ex* ex = nullptr;
        Result("QueryInterface(IDirect3DDevice9Ex)", device->QueryInterface(
            __uuidof(IDirect3DDevice9Ex), reinterpret_cast<void**>(&ex)));
        if (ex) ex->Release();
        D3DDEVICE_CREATION_PARAMETERS parameters{};
        Result("GetCreationParameters", device->GetCreationParameters(&parameters));
        fprintf(log, "adapter=%u deviceType=%u behavior=0x%lX\n",
            parameters.AdapterOrdinal, parameters.DeviceType, parameters.BehaviorFlags);

        IDirect3DSurface9* color = nullptr;
        IDirect3DSurface9* depth = nullptr;
        IDirect3DSurface9* back = nullptr;
        Result("GetRenderTarget(0)", device->GetRenderTarget(0, &color));
        Result("GetDepthStencilSurface", device->GetDepthStencilSurface(&depth));
        Result("GetBackBuffer", device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &back));
        Describe("EndScene color", color);
        Describe("EndScene depth", depth);
        Describe("Back buffer", back);

        D3DSURFACE_DESC desc{};
        if (back && SUCCEEDED(back->GetDesc(&desc)))
        {
            IDirect3DTexture9* shared = nullptr;
            HANDLE handle = nullptr;
            auto result = device->CreateTexture(desc.Width, desc.Height, 1,
                D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &shared, &handle);
            Result("CreateTexture(shared A8R8G8B8)", result);
            fprintf(log, "sharedHandlePresent=%u\n", handle != nullptr);
            if (shared)
            {
                IDirect3DSurface9* destination = nullptr;
                if (SUCCEEDED(shared->GetSurfaceLevel(0, &destination)))
                {
                    const auto copyResult = device->StretchRect(
                        back, nullptr, destination, nullptr, D3DTEXF_NONE);
                    Result("StretchRect(back buffer -> shared)", copyResult);
                    if (SUCCEEDED(copyResult) && handle)
                        VerifyShared(device, destination, handle, desc.Width, desc.Height);
                    destination->Release();
                }
                shared->Release();
            }

            // One-shot CPU readback tests a diagnostic fallback, not a VR transport.
            IDirect3DSurface9* readback = nullptr;
            result = device->CreateOffscreenPlainSurface(desc.Width, desc.Height,
                desc.Format, D3DPOOL_SYSTEMMEM, &readback, nullptr);
            Result("CreateOffscreenPlainSurface(readback)", result);
            if (readback)
            {
                result = device->GetRenderTargetData(back, readback);
                Result("GetRenderTargetData(back buffer)", result);
                if (SUCCEEDED(result))
                {
                    D3DLOCKED_RECT locked{};
                    result = readback->LockRect(&locked, nullptr, D3DLOCK_READONLY);
                    Result("LockRect(readback)", result);
                    if (SUCCEEDED(result))
                    {
                        fprintf(log, "pitch=%d firstBytes=%02X %02X %02X %02X\n", locked.Pitch,
                            static_cast<unsigned char*>(locked.pBits)[0],
                            static_cast<unsigned char*>(locked.pBits)[1],
                            static_cast<unsigned char*>(locked.pBits)[2],
                            static_cast<unsigned char*>(locked.pBits)[3]);
                        readback->UnlockRect();
                    }
                }
                readback->Release();
            }
        }
        if (back) back->Release();
        if (depth) depth->Release();
        if (color) color->Release();
        fclose(log);
        log = nullptr;
    }
}

void VRInteropProbe::Install()
{
    CIniReader ini("");
    if (!ini.ReadInteger("DEVELOPMENT", "ProbeGraphicsInterop", 0)) return;
    if(ini.ReadInteger("DEVELOPMENT","ProbeRenderBindings",0))
    {
        FusionFix::onGameProcessEvent() += [](){++gameFrame;};
        CRenderPhaseDeferredLighting_SceneToGBuffer::OnBuildRenderList() += [](){QueueStage(1);};
        CRenderPhaseDeferredLighting_SceneToGBuffer::OnAfterBuildRenderList() += [](){QueueStage(0);};
        CRenderPhaseDeferredLighting_LightsToScreen::OnBuildRenderList() += [](){QueueStage(2);};
        CRenderPhaseDeferredLighting_LightsToScreen::OnAfterBuildRenderList() += [](){QueueStage(0);};
        CRenderPhaseDrawScene::OnBuildRenderList() += [](){QueueStage(3);};
        CRenderPhaseDrawScene::OnAfterBuildRenderList() += [](){QueueStage(0);};
    }
    FusionFix::onEndScene() += []()
    {
        static unsigned frames = 0;
        static bool done = false;
        if(captureBindings) StopBindings();
        if (done || !RageDirect3DDevice9::m_pRealDevice ||
            !*RageDirect3DDevice9::m_pRealDevice) return;
        // Wait for free roam rather than spending the one-shot on a loading screen.
        if (!CTimer::ms_bUserPause || !CTimer::ms_bScriptPause ||
            *CTimer::ms_bUserPause || *CTimer::ms_bScriptPause) return;
        if (++frames < 180) return;
        done = true;
        Probe(*RageDirect3DDevice9::m_pRealDevice);
        CIniReader probeIni("");
        if(probeIni.ReadInteger("DEVELOPMENT","ProbeRenderBindings",0))
            StartBindings(*RageDirect3DDevice9::m_pRealDevice);
    };
    FusionFix::onShutdownEvent() += [](){StopBindings();};
}
