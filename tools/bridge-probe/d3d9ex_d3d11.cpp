// Standalone Win32 probe for issue 004. Does not load GTA IV or OpenXR.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <cstdio>
#include <cwchar>
#include <cstring>

#pragma comment(lib, "d3d9.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "user32.lib")

template <class T> void release(T*& p) { if (p) { p->Release(); p = nullptr; } }

static bool ok(const char* step, HRESULT hr)
{
    std::printf("%s: 0x%08lX\n", step, static_cast<unsigned long>(hr));
    return SUCCEEDED(hr);
}

int main()
{
    WNDCLASSW wc{};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"GTA4VRBridgeProbe";
    if (!RegisterClassW(&wc)) return 1;
    HWND window = CreateWindowW(wc.lpszClassName, L"bridge-probe", 0,
        0, 0, 64, 64, nullptr, nullptr, wc.hInstance, nullptr);
    if (!window) return 1;

    IDirect3D9Ex* d3d9 = nullptr;
    IDirect3DDevice9Ex* dev9 = nullptr;
    IDirect3DTexture9* tex9 = nullptr;
    IDirect3DSurface9* surf9 = nullptr;
    IDirect3DQuery9* query9 = nullptr;
    IDXGIFactory1* factory = nullptr;
    IDXGIAdapter1* adapter = nullptr;
    ID3D11Device* dev11 = nullptr;
    ID3D11DeviceContext* ctx11 = nullptr;
    ID3D11Texture2D* tex11 = nullptr;
    ID3D11Texture2D* staging = nullptr;
    HANDLE shared = nullptr;
    bool success = false;

    do {
        if (!ok("Direct3DCreate9Ex", Direct3DCreate9Ex(D3D_SDK_VERSION, &d3d9))) break;
        D3DPRESENT_PARAMETERS pp{};
        pp.BackBufferWidth = 64;
        pp.BackBufferHeight = 64;
        pp.BackBufferFormat = D3DFMT_A8R8G8B8;
        pp.BackBufferCount = 1;
        pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
        pp.hDeviceWindow = window;
        pp.Windowed = TRUE;
        pp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
        if (!ok("CreateDeviceEx", d3d9->CreateDeviceEx(D3DADAPTER_DEFAULT,
            D3DDEVTYPE_HAL, window, D3DCREATE_HARDWARE_VERTEXPROCESSING,
            &pp, nullptr, &dev9))) break;

        LUID luid9{};
        if (!ok("GetAdapterLUID", d3d9->GetAdapterLUID(D3DADAPTER_DEFAULT, &luid9))) break;
        std::printf("D3D9Ex adapter LUID: %08lX:%08lX\n",
            static_cast<unsigned long>(luid9.HighPart),
            static_cast<unsigned long>(luid9.LowPart));
        if (!ok("CreateDXGIFactory1", CreateDXGIFactory1(__uuidof(IDXGIFactory1),
            reinterpret_cast<void**>(&factory)))) break;
        for (UINT i = 0; ; ++i) {
            IDXGIAdapter1* candidate = nullptr;
            HRESULT hr = factory->EnumAdapters1(i, &candidate);
            if (hr == DXGI_ERROR_NOT_FOUND) break;
            if (FAILED(hr)) { ok("EnumAdapters1", hr); break; }
            DXGI_ADAPTER_DESC1 desc{};
            candidate->GetDesc1(&desc);
            if (!std::memcmp(&luid9, &desc.AdapterLuid, sizeof(LUID))) {
                adapter = candidate;
                std::wprintf(L"Adapter: %ls\n", desc.Description);
                break;
            }
            release(candidate);
        }
        if (!adapter) { std::puts("No DXGI adapter matched the D3D9Ex LUID"); break; }
        D3D_FEATURE_LEVEL feature{};
        if (!ok("D3D11CreateDevice", D3D11CreateDevice(adapter, D3D_DRIVER_TYPE_UNKNOWN,
            nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION,
            &dev11, &feature, &ctx11))) break;
        std::printf("D3D11 feature level: 0x%04X\n", static_cast<unsigned>(feature));

        if (!ok("D3D9Ex CreateTexture(shared)", dev9->CreateTexture(64, 64, 1,
            D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT,
            &tex9, &shared))) break;
        if (!shared) { std::puts("D3D9Ex returned no shared handle"); break; }
        if (!ok("GetSurfaceLevel", tex9->GetSurfaceLevel(0, &surf9))) break;
        if (!ok("SetRenderTarget", dev9->SetRenderTarget(0, surf9))) break;
        if (!ok("D3D9Ex Clear", dev9->Clear(0, nullptr, D3DCLEAR_TARGET,
            D3DCOLOR_ARGB(255, 51, 102, 204), 1.0f, 0))) break;
        if (!ok("CreateQuery(EVENT)", dev9->CreateQuery(D3DQUERYTYPE_EVENT, &query9))) break;
        if (!ok("Issue(EVENT)", query9->Issue(D3DISSUE_END))) break;
        const ULONGLONG deadline = GetTickCount64() + 5000;
        HRESULT queryResult;
        do {
            queryResult = query9->GetData(nullptr, 0, D3DGETDATA_FLUSH);
            if (queryResult == S_FALSE) Sleep(1);
        } while (queryResult == S_FALSE && GetTickCount64() < deadline);
        if (!ok("D3D9Ex EVENT completion", queryResult) || queryResult != S_OK) break;

        if (!ok("D3D11 OpenSharedResource", dev11->OpenSharedResource(shared,
            __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&tex11)))) break;
        D3D11_TEXTURE2D_DESC desc{};
        tex11->GetDesc(&desc);
        std::printf("Shared D3D11 texture: %ux%u format=%u bind=0x%X misc=0x%X\n",
            desc.Width, desc.Height, static_cast<unsigned>(desc.Format),
            desc.BindFlags, desc.MiscFlags);
        desc.Usage = D3D11_USAGE_STAGING;
        desc.BindFlags = 0;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        desc.MiscFlags = 0;
        if (!ok("D3D11 CreateTexture2D(staging)", dev11->CreateTexture2D(&desc, nullptr,
            &staging))) break;
        ctx11->CopyResource(staging, tex11);
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (!ok("D3D11 Map(staging)", ctx11->Map(staging, 0, D3D11_MAP_READ, 0,
            &mapped))) break;
        const auto* pixel = static_cast<const unsigned char*>(mapped.pData);
        std::printf("Pixel BGRA: %02X %02X %02X %02X\n",
            pixel[0], pixel[1], pixel[2], pixel[3]);
        success = pixel[0] == 204 && pixel[1] == 102 && pixel[2] == 51 && pixel[3] == 255;
        ctx11->Unmap(staging, 0);
        std::printf("D3D9Ex -> D3D11 color transfer: %s\n", success ? "PASS" : "FAIL");
    } while (false);

    release(staging);
    release(tex11);
    release(ctx11);
    release(dev11);
    release(adapter);
    release(factory);
    release(query9);
    release(surf9);
    release(tex9);
    release(dev9);
    release(d3d9);
    DestroyWindow(window);
    return success ? 0 : 1;
}
