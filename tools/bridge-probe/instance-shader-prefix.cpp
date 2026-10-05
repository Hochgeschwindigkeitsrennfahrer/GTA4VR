#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include <d3dx9shader.h>
#include <wrl/client.h>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <cstdio>
#include <array>
#include <vector>
#include <cstring>
#include <stdexcept>
#include <cmath>
#include "../../source/includes/vrbillboardshader.hpp"

using Microsoft::WRL::ComPtr;
namespace
{
    void Check(HRESULT hr) {if(FAILED(hr)) {std::fprintf(stderr,"HRESULT=%08lX\n",hr);throw std::runtime_error("D3D equivalence call failed");}}
    struct Instance {float center[3],right[3],up[3];DWORD color;};
    struct Vertex {float position[3],center[3];DWORD color;float uv[2];};
    struct CompactInstance {float center[3],radius,brightness,push;DWORD color,flags;};
    struct UnitCorner {float corner[4],right[3],up[3];};
    static_assert(sizeof(CompactInstance)==32);
    ComPtr<IDirect3DVertexBuffer9> Buffer(IDirect3DDevice9* device,const void* data,UINT bytes)
    {
        ComPtr<IDirect3DVertexBuffer9> buffer;
        Check(device->CreateVertexBuffer(bytes,D3DUSAGE_WRITEONLY,0,D3DPOOL_DEFAULT,&buffer,nullptr));
        void* mapped=nullptr;Check(buffer->Lock(0,bytes,&mapped,0));std::memcpy(mapped,data,bytes);Check(buffer->Unlock());return buffer;
    }
    // Captured native shaders/constants, synthetic colored atlas and controlled depth.
    // Verify both occluded and visible cases without embedding proprietary assets.
    bool PixelCheck(IDirect3DDevice9* device,IDirect3DVertexShader9* nativeVS,IDirect3DVertexShader9* instanceVS,
        const wchar_t* pixelPath,const wchar_t* constantsPath,bool compact)
    {
        std::ifstream pixelFile(std::filesystem::path(pixelPath),std::ios::binary);
        if(!pixelFile) return false;
        const std::string text((std::istreambuf_iterator<char>(pixelFile)),{});
        ComPtr<ID3DXBuffer> code,errors;Check(D3DXAssembleShader(text.data(),static_cast<UINT>(text.size()),nullptr,nullptr,0,&code,&errors));
        ComPtr<IDirect3DPixelShader9> ps;Check(device->CreatePixelShader(static_cast<const DWORD*>(code->GetBufferPointer()),&ps));
        std::ifstream constantsFile{std::filesystem::path(constantsPath)};
        float constants[2][480][4]{};
        for(auto& eye:constants) for(auto& row:eye) for(auto& value:row)
        {
            std::string token;
            if(!(constantsFile>>token)) return false;
            value=std::stof(token); // Handles the native dump's unused NaN constants.
        }
        constexpr UINT size=128,n=16;
        ComPtr<IDirect3DSurface9> target,readback;
        Check(device->CreateRenderTarget(size,size,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,&target,nullptr));
        Check(device->CreateOffscreenPlainSurface(size,size,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&readback,nullptr));
        Check(device->SetRenderTarget(0,target.Get()));Check(device->SetDepthStencilSurface(nullptr));
        const D3DVIEWPORT9 viewport{0,0,size,size,0,1};Check(device->SetViewport(&viewport));
        Check(device->SetRenderState(D3DRS_ZENABLE,FALSE));Check(device->SetRenderState(D3DRS_ZWRITEENABLE,FALSE));
        Check(device->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));Check(device->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE));
        Check(device->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_ONE));Check(device->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_ONE));
        Check(device->SetRenderState(D3DRS_SRGBWRITEENABLE,FALSE));Check(device->SetPixelShader(ps.Get()));
        ComPtr<IDirect3DTexture9> atlas,depth;
        Check(device->CreateTexture(2,2,1,D3DUSAGE_DYNAMIC,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,&atlas,nullptr));
        D3DLOCKED_RECT lock{};Check(atlas->LockRect(0,&lock,nullptr,0));
        const DWORD atlasPixels[2][2]{{0xFFFFFFFF,0xFFFFFF00},{0xFF00FFFF,0xFFFF00FF}};
        for(unsigned y=0;y<2;++y)std::memcpy(static_cast<char*>(lock.pBits)+y*lock.Pitch,atlasPixels[y],sizeof(atlasPixels[y]));
        Check(atlas->UnlockRect(0));
        Check(device->CreateTexture(1,1,1,D3DUSAGE_DYNAMIC,D3DFMT_R32F,D3DPOOL_DEFAULT,&depth,nullptr));
        Check(device->SetTexture(0,atlas.Get()));Check(device->SetTexture(D3DVERTEXTEXTURESAMPLER0,depth.Get()));
        for(DWORD slot:std::array<DWORD,2>{0,static_cast<DWORD>(D3DVERTEXTEXTURESAMPLER0)})
        {Check(device->SetSamplerState(slot,D3DSAMP_MINFILTER,D3DTEXF_POINT));Check(device->SetSamplerState(slot,D3DSAMP_MAGFILTER,D3DTEXF_POINT));Check(device->SetSamplerState(slot,D3DSAMP_MIPFILTER,D3DTEXF_NONE));Check(device->SetSamplerState(slot,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP));Check(device->SetSamplerState(slot,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP));}
        const D3DVERTEXELEMENT9 originalDecl[]{
            {0,0,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_POSITION,0},{0,12,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_NORMAL,0},
            {0,24,D3DDECLTYPE_D3DCOLOR,0,D3DDECLUSAGE_COLOR,0},{0,28,D3DDECLTYPE_FLOAT2,0,D3DDECLUSAGE_TEXCOORD,0},D3DDECL_END()};
        const D3DVERTEXELEMENT9 instancedDecl[]{
            {0,0,D3DDECLTYPE_FLOAT2,0,D3DDECLUSAGE_POSITION,0},{0,8,D3DDECLTYPE_FLOAT2,0,D3DDECLUSAGE_TEXCOORD,0},
            {1,0,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_NORMAL,0},{1,12,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_TEXCOORD,1},
            {1,24,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_TEXCOORD,2},{1,36,D3DDECLTYPE_D3DCOLOR,0,D3DDECLUSAGE_COLOR,0},D3DDECL_END()};
        const D3DVERTEXELEMENT9 compactDecl[]{
            {0,0,D3DDECLTYPE_FLOAT4,0,D3DDECLUSAGE_POSITION,0},{0,16,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_TEXCOORD,1},
            {0,28,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_TEXCOORD,2},{1,0,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_NORMAL,0},
            {1,12,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_TEXCOORD,0},{1,24,D3DDECLTYPE_D3DCOLOR,0,D3DDECLUSAGE_COLOR,0},D3DDECL_END()};
        ComPtr<IDirect3DVertexDeclaration9> oldDecl,newDecl;Check(device->CreateVertexDeclaration(originalDecl,&oldDecl));Check(device->CreateVertexDeclaration(compact ? compactDecl:instancedDecl,&newDecl));
        const float corners[4][4]{{-1,1,0,1},{1,1,1,1},{1,-1,1,0},{-1,-1,0,0}};
        const WORD indices[]{0,1,2,2,3,0};
        const auto ordinaryUnit=Buffer(device,corners,sizeof(corners));
        ComPtr<IDirect3DIndexBuffer9> ib;Check(device->CreateIndexBuffer(sizeof(indices),D3DUSAGE_WRITEONLY,D3DFMT_INDEX16,D3DPOOL_DEFAULT,&ib,nullptr));
        void* indexData=nullptr;Check(ib->Lock(0,sizeof(indices),&indexData,0));std::memcpy(indexData,indices,sizeof(indices));Check(ib->Unlock());
        bool passed=true;unsigned totalLit=0;
        for(unsigned eye=0;eye<2;++eye)
        {
            const auto* c=constants[eye];Check(device->SetVertexShaderConstantF(0,c[0],256));Check(device->SetPixelShaderConstantF(0,c[256],224));
            std::array<Instance,n> instances{};std::array<CompactInstance,n> records{};std::vector<Vertex> expanded;
            std::array<UnitCorner,4> unitCorners{};
            for(unsigned i=0;i<4;++i)
            {std::memcpy(unitCorners[i].corner,corners[i],sizeof(corners[i]));for(unsigned j=0;j<3;++j){unitCorners[i].right[j]=c[12][j];unitCorners[i].up[j]=-c[13][j];}}
            const auto compactUnit=Buffer(device,unitCorners.data(),sizeof(unitCorners));
            for(unsigned i=0;i<n;++i)
            {
                auto& v=instances[i];v.color=0xFF804020;
                const float x=(static_cast<int>(i%4)-1.5f)*3,y=(static_cast<int>(i/4)-1.5f)*3;
                for(unsigned j=0;j<3;++j)
                {v.center[j]=c[15][j]-c[14][j]*30+c[12][j]*x+c[13][j]*y;v.right[j]=c[12][j];v.up[j]=c[13][j];}
                auto& record=records[i];std::memcpy(record.center,v.center,sizeof(v.center));
                record.radius=1;record.brightness=.5f+(i%3)*.25f;record.push=2.5f;record.color=0x00204080;
                if(compact)
                {
                    float delta[3]{};for(unsigned j=0;j<3;++j)delta[j]=v.center[j]-c[15][j];
                    const float inv=1/std::sqrt(delta[0]*delta[0]+delta[1]*delta[1]+delta[2]*delta[2]);
                    for(unsigned j=0;j<3;++j){v.center[j]-=delta[j]*inv*record.push;v.up[j]=-v.up[j];}
                }
                for(auto index:indices)
                {
                    Vertex vertex{};vertex.color=compact ? ((v.color&0xFFFFFF)|(corners[index][3]>0 ? 0xFF000000:0)):v.color;std::memcpy(vertex.center,v.center,sizeof(v.center));
                    for(unsigned j=0;j<3;++j) vertex.position[j]=v.center[j]+corners[index][0]*v.right[j]+corners[index][1]*v.up[j];
                    vertex.uv[0]=corners[index][2];vertex.uv[1]=compact ? record.brightness:1;expanded.push_back(vertex);
                }
            }
            const auto cpu=Buffer(device,expanded.data(),static_cast<UINT>(expanded.size()*sizeof(Vertex)));
            const auto instance=compact ? Buffer(device,records.data(),sizeof(records)):Buffer(device,instances.data(),sizeof(instances));
            for(unsigned occluded=0;occluded<2;++occluded)
            {
                Check(depth->LockRect(0,&lock,nullptr,0));*static_cast<float*>(lock.pBits)=occluded ? -100.0f:100.0f;Check(depth->UnlockRect(0));
                std::vector<unsigned char> images[2];
                for(unsigned mode=0;mode<2;++mode)
                {
                    Check(device->Clear(0,nullptr,D3DCLEAR_TARGET,0,1,0));Check(device->BeginScene());
                    Check(device->SetStreamSourceFreq(0,1));Check(device->SetStreamSourceFreq(1,1));
                    if(mode==0)
                    {Check(device->SetVertexDeclaration(oldDecl.Get()));Check(device->SetVertexShader(nativeVS));Check(device->SetStreamSource(0,cpu.Get(),0,sizeof(Vertex)));Check(device->DrawPrimitive(D3DPT_TRIANGLELIST,0,n*2));}
                    else
                    {
                        Check(device->SetVertexDeclaration(newDecl.Get()));Check(device->SetVertexShader(instanceVS));
                        Check(device->SetStreamSource(0,compact ? compactUnit.Get():ordinaryUnit.Get(),0,compact ? sizeof(UnitCorner):sizeof(corners[0])));Check(device->SetStreamSource(1,instance.Get(),0,compact ? sizeof(CompactInstance):sizeof(Instance)));
                        Check(device->SetStreamSourceFreq(0,D3DSTREAMSOURCE_INDEXEDDATA|n));Check(device->SetStreamSourceFreq(1,static_cast<UINT>(D3DSTREAMSOURCE_INSTANCEDATA)|1u));
                        Check(device->SetIndices(ib.Get()));Check(device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,0,0,4,0,2));
                        Check(device->SetStreamSourceFreq(0,1));Check(device->SetStreamSourceFreq(1,1));
                    }
                    Check(device->EndScene());Check(device->GetRenderTargetData(target.Get(),readback.Get()));Check(readback->LockRect(&lock,nullptr,D3DLOCK_READONLY));
                    images[mode].resize(size*size*4);
                    for(unsigned y=0;y<size;++y) std::memcpy(images[mode].data()+y*size*4,static_cast<const char*>(lock.pBits)+y*lock.Pitch,size*4);
                    Check(readback->UnlockRect());
                }
                unsigned mismatches=0,lit=0;
                for(unsigned p=0;p<size*size;++p)
                {mismatches+=std::memcmp(images[0].data()+p*4,images[1].data()+p*4,4)!=0;lit+=(images[1][p*4]|images[1][p*4+1]|images[1][p*4+2])!=0;}
                std::printf("native shader eye=%u occluded=%u mismatchedPixels=%u litPixels=%u\n",eye,occluded,mismatches,lit);
                passed=passed && !mismatches && (occluded ? !lit:lit>0);totalLit+=lit;
            }
        }
        return passed && totalLit>0;
    }
}
int wmain(int argc,wchar_t** argv)
{
    // Local native disassembly is an input, never an embedded/distributed asset.
    if(argc!=3 && argc!=5 && argc!=6) {std::fprintf(stderr,"Usage: instance-shader-prefix backend.dll native-vs.asm [native-ps.asm constants.txt [compact]]\n");return 2;}
    const bool compact=argc==6 && std::wstring(argv[5])==L"compact";
    std::ifstream input(std::filesystem::path(argv[2]),std::ios::binary);
    if(!input) return 2;
    const std::string original((std::istreambuf_iterator<char>(input)),{});
    std::string instanced;
    if(!VRBillboardShader::AddInstanceExpansion(original,instanced,compact))
    {std::fprintf(stderr,"Unsupported shader shape; preserve native fallback\n");return 1;}
    ComPtr<ID3DXBuffer> originalCode,instanceCode,errors;
    for(unsigned mode=0;mode<2;++mode)
    {
        const auto& text=mode ? instanced:original;
        auto& code=mode ? instanceCode:originalCode;
        const auto hr=D3DXAssembleShader(text.data(),static_cast<UINT>(text.size()),nullptr,nullptr,0,&code,&errors);
        if(FAILED(hr))
        {if(errors) std::fprintf(stderr,"%s",static_cast<const char*>(errors->GetBufferPointer()));return 1;}
        errors.Reset();
    }
    const auto dll=LoadLibraryW(argv[1]);
    const auto create=dll ? reinterpret_cast<HRESULT(WINAPI*)(UINT,IDirect3D9Ex**)>(GetProcAddress(dll,"Direct3DCreate9Ex")):nullptr;
    if(!create) return 1;
    ComPtr<IDirect3D9Ex> d3d; if(FAILED(create(D3D_SDK_VERSION,&d3d))) return 1;
    WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"GTA4VRInstanceShaderPrefix";
    RegisterClassW(&wc);
    const auto window=CreateWindowW(wc.lpszClassName,L"GTA4VR instance shader probe",WS_POPUP,0,0,32,32,nullptr,nullptr,wc.hInstance,nullptr);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.hDeviceWindow=window;
    pp.BackBufferWidth=32;pp.BackBufferHeight=32;pp.BackBufferFormat=D3DFMT_A8R8G8B8;
    ComPtr<IDirect3DDevice9Ex> device;
    if(FAILED(d3d->CreateDeviceEx(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING,&pp,nullptr,&device))) return 1;
    ComPtr<IDirect3DVertexShader9> originalShader,instanceShader;
    if(FAILED(device->CreateVertexShader(static_cast<const DWORD*>(originalCode->GetBufferPointer()),&originalShader)) ||
       FAILED(device->CreateVertexShader(static_cast<const DWORD*>(instanceCode->GetBufferPointer()),&instanceShader))) return 1;
    std::printf("PASS creation originalBytes=%zu instanceBytes=%zu; native shader retained with GPU corner prefix\n",
        originalCode->GetBufferSize(),instanceCode->GetBufferSize());
    bool passed=true;
    if(argc>=5) try {passed=PixelCheck(device.Get(),originalShader.Get(),instanceShader.Get(),argv[3],argv[4],compact);}
    catch(const std::exception& error) {std::fprintf(stderr,"%s\n",error.what());passed=false;}
    std::printf("%s; synthetic shader equivalence only, no game speedup claim\n",passed ? "PASS":"FAIL");
    instanceShader.Reset();originalShader.Reset();device.Reset();d3d.Reset();DestroyWindow(window);
    return passed ? 0:1;
}
