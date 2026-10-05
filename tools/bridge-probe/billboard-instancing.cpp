#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d9.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <array>
#include <vector>
#include <cstdio>
#include <cstring>
#include <stdexcept>

using Microsoft::WRL::ComPtr;
namespace {
    constexpr unsigned size=128,count=16;
    struct Instance {float center[4],right[4],up[4],color[4];};
    struct Vertex {float position[3],color[4];};
    void Check(HRESULT hr) {if(FAILED(hr)) {std::fprintf(stderr,"HRESULT=%08lX\n",hr);throw std::runtime_error("D3D call failed");}}
    ComPtr<ID3DBlob> Shader(const char* entry,const char* target) {
        ComPtr<ID3DBlob> code,errors;
        const auto hr=D3DCompileFromFile(L"tools/bridge-probe/billboard-instancing.hlsl",nullptr,nullptr,entry,target,
            D3DCOMPILE_ENABLE_STRICTNESS|D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&errors);
        if(errors) std::fprintf(stderr,"%s",static_cast<const char*>(errors->GetBufferPointer()));
        Check(hr);return code;
    }
    ComPtr<IDirect3DVertexBuffer9> Buffer(IDirect3DDevice9* device,const void* data,unsigned bytes) {
        ComPtr<IDirect3DVertexBuffer9> buffer;
        Check(device->CreateVertexBuffer(bytes,D3DUSAGE_WRITEONLY,0,D3DPOOL_DEFAULT,&buffer,nullptr));
        void* output=nullptr;Check(buffer->Lock(0,bytes,&output,0));std::memcpy(output,data,bytes);Check(buffer->Unlock());return buffer;
    }
}

int wmain(int argc,wchar_t** argv) try {
    // Optional explicit D3D9 backend DLL, e.g. the installed x86 DXVK vulkan.dll.
    // No game hooks, OpenXR session, installed-file writes or save access.
    const wchar_t* backend=argc==2?argv[1]:L"d3d9.dll";
    const auto dll=LoadLibraryW(backend);
    if(!dll) throw std::runtime_error("Could not load D3D9 backend");
    const auto create=reinterpret_cast<HRESULT(WINAPI*)(UINT,IDirect3D9Ex**)>(GetProcAddress(dll,"Direct3DCreate9Ex"));
    if(!create) throw std::runtime_error("Backend has no Direct3DCreate9Ex export");
    WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"GTA4VRBillboardProbe";
    if(!RegisterClassW(&wc)) throw std::runtime_error("RegisterClass failed");
    const auto window=CreateWindowW(wc.lpszClassName,L"GTA4VR billboard probe",WS_POPUP,0,0,size,size,nullptr,nullptr,wc.hInstance,nullptr);
    if(!window) throw std::runtime_error("CreateWindow failed");
    ComPtr<IDirect3D9Ex> api;Check(create(D3D_SDK_VERSION,&api));
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;
    pp.BackBufferWidth=size;pp.BackBufferHeight=size;pp.BackBufferFormat=D3DFMT_A8R8G8B8;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
    ComPtr<IDirect3DDevice9Ex> device;
    Check(api->CreateDeviceEx(D3DADAPTER_DEFAULT,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING,&pp,nullptr,&device));
    D3DADAPTER_IDENTIFIER9 adapter{};Check(api->GetAdapterIdentifier(0,0,&adapter));
    std::printf("Backend=%ls GPU=%s\n",backend,adapter.Description);
    const auto instancedCode=Shader("InstancedVS","vs_3_0"),expandedCode=Shader("ExpandedVS","vs_3_0"),pixelCode=Shader("ColorPS","ps_3_0");
    ComPtr<IDirect3DVertexShader9> instancedVS,expandedVS;ComPtr<IDirect3DPixelShader9> ps;
    Check(device->CreateVertexShader(static_cast<const DWORD*>(instancedCode->GetBufferPointer()),&instancedVS));
    Check(device->CreateVertexShader(static_cast<const DWORD*>(expandedCode->GetBufferPointer()),&expandedVS));
    Check(device->CreatePixelShader(static_cast<const DWORD*>(pixelCode->GetBufferPointer()),&ps));
    const D3DVERTEXELEMENT9 instanceDecl[]{
        {0,0,D3DDECLTYPE_FLOAT2,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_POSITION,0},
        {1,0,D3DDECLTYPE_FLOAT4,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TEXCOORD,0},
        {1,16,D3DDECLTYPE_FLOAT4,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TEXCOORD,1},
        {1,32,D3DDECLTYPE_FLOAT4,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TEXCOORD,2},
        {1,48,D3DDECLTYPE_FLOAT4,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_COLOR,0},D3DDECL_END()};
    const D3DVERTEXELEMENT9 expandedDecl[]{
        {0,0,D3DDECLTYPE_FLOAT3,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_POSITION,0},
        {0,12,D3DDECLTYPE_FLOAT4,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_COLOR,0},D3DDECL_END()};
    ComPtr<IDirect3DVertexDeclaration9> idecl,edecl;
    Check(device->CreateVertexDeclaration(instanceDecl,&idecl));Check(device->CreateVertexDeclaration(expandedDecl,&edecl));
    const float corners[4][2]{{-1,-1},{1,-1},{-1,1},{1,1}};
    const WORD indices[]{0,1,2,2,1,3};
    std::array<Instance,count> instances{};std::vector<Vertex> expanded;
    for(unsigned i=0;i<count;++i) {
        auto& v=instances[i];v.center[0]=-.75f+.5f*(i%4);v.center[1]=-.75f+.5f*(i/4);v.center[2]=.5f;
        v.right[0]=.125f;v.right[1]=(i&1)?.0625f:0;v.up[1]=.125f;
        v.color[0]=(i&1)?.5f:1;v.color[1]=(i&2)?.5f:1;v.color[2]=.25f;v.color[3]=1;
        for(auto index:indices) {
            Vertex vertex{};
            for(unsigned j=0;j<3;++j) vertex.position[j]=v.center[j]+corners[index][0]*v.right[j]+corners[index][1]*v.up[j];
            std::memcpy(vertex.color,v.color,sizeof(v.color));expanded.push_back(vertex);
        }
    }
    auto unit=Buffer(device.Get(),corners,sizeof(corners)),instance=Buffer(device.Get(),instances.data(),sizeof(instances));
    auto cpu=Buffer(device.Get(),expanded.data(),static_cast<unsigned>(expanded.size()*sizeof(Vertex)));
    ComPtr<IDirect3DIndexBuffer9> ib;
    Check(device->CreateIndexBuffer(sizeof(indices),D3DUSAGE_WRITEONLY,D3DFMT_INDEX16,D3DPOOL_DEFAULT,&ib,nullptr));
    void* output=nullptr;Check(ib->Lock(0,sizeof(indices),&output,0));std::memcpy(output,indices,sizeof(indices));Check(ib->Unlock());
    ComPtr<IDirect3DSurface9> target,readback;
    Check(device->CreateRenderTarget(size,size,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,&target,nullptr));
    Check(device->CreateOffscreenPlainSurface(size,size,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&readback,nullptr));
    Check(device->SetRenderTarget(0,target.Get()));Check(device->SetDepthStencilSurface(nullptr));
    Check(device->SetRenderState(D3DRS_ZENABLE,FALSE));Check(device->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));
    Check(device->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE));Check(device->SetRenderState(D3DRS_SRGBWRITEENABLE,FALSE));
    Check(device->SetPixelShader(ps.Get()));
    unsigned lit=0;
    for(unsigned eye=0;eye<2;++eye) {
        const float matrix[]{1,0,0,0,0,1,0,0,0,0,1,0,eye?.0625f:-.0625f,0,0,1};
        Check(device->SetVertexShaderConstantF(0,matrix,4));
        std::vector<unsigned char> images[2];
        for(unsigned mode=0;mode<2;++mode) {
            Check(device->Clear(0,nullptr,D3DCLEAR_TARGET,0,1,0));Check(device->BeginScene());
            Check(device->SetStreamSourceFreq(0,1));Check(device->SetStreamSourceFreq(1,1));
            if(mode==0) {
                Check(device->SetVertexDeclaration(edecl.Get()));Check(device->SetVertexShader(expandedVS.Get()));
                Check(device->SetStreamSource(0,cpu.Get(),0,sizeof(Vertex)));
                Check(device->DrawPrimitive(D3DPT_TRIANGLELIST,0,count*2));
            } else {
                Check(device->SetVertexDeclaration(idecl.Get()));Check(device->SetVertexShader(instancedVS.Get()));
                Check(device->SetStreamSource(0,unit.Get(),0,sizeof(corners[0])));
                Check(device->SetStreamSource(1,instance.Get(),0,sizeof(Instance)));
                Check(device->SetStreamSourceFreq(0,D3DSTREAMSOURCE_INDEXEDDATA|count));
                Check(device->SetStreamSourceFreq(1,static_cast<UINT>(D3DSTREAMSOURCE_INSTANCEDATA)|1u));
                Check(device->SetIndices(ib.Get()));Check(device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,0,0,4,0,2));
                Check(device->SetStreamSourceFreq(0,1));Check(device->SetStreamSourceFreq(1,1));
            }
            Check(device->EndScene());Check(device->GetRenderTargetData(target.Get(),readback.Get()));
            D3DLOCKED_RECT mapped{};Check(readback->LockRect(&mapped,nullptr,D3DLOCK_READONLY));
            images[mode].resize(size*size*4);
            for(unsigned y=0;y<size;++y) std::memcpy(images[mode].data()+y*size*4,static_cast<const char*>(mapped.pBits)+y*mapped.Pitch,size*4);
            Check(readback->UnlockRect());
        }
        unsigned mismatches=0;
        for(unsigned pixel=0;pixel<size*size;++pixel) {
            const auto* a=images[0].data()+pixel*4;const auto* b=images[1].data()+pixel*4;
            if(std::memcmp(a,b,4)) ++mismatches;
            if(a[0]||a[1]||a[2]) ++lit;
        }
        std::printf("eye=%u instances=%u mismatchedPixels=%u\n",eye,count,mismatches);
        if(mismatches) return 2;
    }
    if(!lit) throw std::runtime_error("Empty output cannot validate instancing");
    std::printf("PASS litPixels=%u; same instance buffer, two eye matrices, no production readback requirement\n",lit);
    return 0;
} catch(const std::exception& error) {std::fprintf(stderr,"FAIL: %s\n",error.what());return 1;}
