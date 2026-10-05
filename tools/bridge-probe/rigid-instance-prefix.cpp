#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include <d3dx9shader.h>
#include <wrl/client.h>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <array>
#include <vector>
#include <cstring>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include "../../source/includes/vrrigidinstanceshader.hpp"
#include "../../source/includes/vrrigidinstance.hpp"
#include "../../source/includes/vrmeshpacketprobe.hpp"

using Microsoft::WRL::ComPtr;
namespace
{
    void Check(HRESULT result)
    {if(FAILED(result)){std::fprintf(stderr,"HRESULT=%08lX\n",result);throw std::runtime_error("D3D rigid equivalence call failed");}}
    std::string Read(const wchar_t* path)
    {std::ifstream file(std::filesystem::path(path),std::ios::binary);if(!file)throw std::runtime_error("Local shader input missing");return {(std::istreambuf_iterator<char>(file)),{}};}
    ComPtr<ID3DXBuffer> Assemble(const std::string& text)
    {
        ComPtr<ID3DXBuffer> code,errors;
        const auto result=D3DXAssembleShader(text.data(),static_cast<UINT>(text.size()),nullptr,nullptr,0,&code,&errors);
        if(FAILED(result) && errors)std::fprintf(stderr,"%s\n",static_cast<const char*>(errors->GetBufferPointer()));
        Check(result);return code;
    }
    struct Vertex {float position[3],normal[3];DWORD color;float uv[2];float tangent[4]{1,0,0,1};};
    struct Instance {float matrices[8][4];};
    static_assert(sizeof(Vertex)==52 && sizeof(Instance)==128);
    ComPtr<IDirect3DVertexBuffer9> Buffer(IDirect3DDevice9* device,const void* data,UINT bytes)
    {
        ComPtr<IDirect3DVertexBuffer9> buffer;
        Check(device->CreateVertexBuffer(bytes,D3DUSAGE_WRITEONLY,0,D3DPOOL_DEFAULT,&buffer,nullptr));
        void* output=nullptr;Check(buffer->Lock(0,bytes,&output,0));std::memcpy(output,data,bytes);Check(buffer->Unlock());return buffer;
    }
    bool PixelCheck(IDirect3DDevice9* device,IDirect3DVertexShader9* native,IDirect3DVertexShader9* instanced,IDirect3DPixelShader9* pixel,bool tangent)
    {
        constexpr UINT size=128,count=16;
        ComPtr<IDirect3DSurface9> targets[4],readback,depth;ComPtr<IDirect3DTexture9> depthTexture;
        for(unsigned i=0;i<4;++i)
        {
            Check(device->CreateRenderTarget(size,size,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,&targets[i],nullptr));
            Check(device->SetRenderTarget(i,targets[i].Get()));
        }
        Check(device->CreateOffscreenPlainSurface(size,size,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&readback,nullptr));
        Check(device->CreateTexture(size,size,1,D3DUSAGE_DEPTHSTENCIL,static_cast<D3DFORMAT>(MAKEFOURCC('I','N','T','Z')),D3DPOOL_DEFAULT,&depthTexture,nullptr));
        Check(depthTexture->GetSurfaceLevel(0,&depth));
        Check(device->SetDepthStencilSurface(depth.Get()));const D3DVIEWPORT9 viewport{0,0,size,size,0,1};Check(device->SetViewport(&viewport));
        Check(device->SetRenderState(D3DRS_ZENABLE,TRUE));Check(device->SetRenderState(D3DRS_ZWRITEENABLE,TRUE));
        Check(device->SetRenderState(D3DRS_ZFUNC,D3DCMP_LESS));
        Check(device->SetRenderState(D3DRS_STENCILENABLE,TRUE));
        Check(device->SetRenderState(D3DRS_STENCILFUNC,D3DCMP_ALWAYS));
        Check(device->SetRenderState(D3DRS_STENCILFAIL,D3DSTENCILOP_KEEP));
        Check(device->SetRenderState(D3DRS_STENCILZFAIL,D3DSTENCILOP_KEEP));
        Check(device->SetRenderState(D3DRS_STENCILPASS,D3DSTENCILOP_REPLACE));
        Check(device->SetRenderState(D3DRS_STENCILREF,32));
        Check(device->SetRenderState(D3DRS_STENCILMASK,255));
        Check(device->SetRenderState(D3DRS_STENCILWRITEMASK,255));
        Check(device->SetRenderState(D3DRS_TWOSIDEDSTENCILMODE,FALSE));
        Check(device->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));Check(device->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE));
        Check(device->SetRenderState(D3DRS_SRGBWRITEENABLE,FALSE));Check(device->SetPixelShader(pixel));
        constexpr D3DRENDERSTATETYPE writes[]{D3DRS_COLORWRITEENABLE,D3DRS_COLORWRITEENABLE1,D3DRS_COLORWRITEENABLE2,D3DRS_COLORWRITEENABLE3};
        for(auto state:writes)Check(device->SetRenderState(state,15));
        ComPtr<IDirect3DTexture9> atlas,occluder,atlasUpload,occluderUpload;
        Check(device->CreateTexture(2,2,1,0,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,&atlas,nullptr));
        Check(device->CreateTexture(1,1,1,0,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,&occluder,nullptr));
        Check(device->CreateTexture(2,2,1,0,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&atlasUpload,nullptr));
        Check(device->CreateTexture(1,1,1,0,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&occluderUpload,nullptr));
        D3DLOCKED_RECT solid{};Check(occluderUpload->LockRect(0,&solid,nullptr,0));
        *static_cast<DWORD*>(solid.pBits)=0xFFFF00FF;Check(occluderUpload->UnlockRect(0));
        Check(device->UpdateTexture(occluderUpload.Get(),occluder.Get()));
        Check(device->SetTexture(0,atlas.Get()));
        Check(device->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_POINT));Check(device->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_POINT));
        Check(device->SetSamplerState(0,D3DSAMP_MIPFILTER,D3DTEXF_NONE));Check(device->SetSamplerState(0,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP));
        Check(device->SetSamplerState(0,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP));
        float vs[256][4]{},ps[224][4]{};
        vs[208][0]=vs[209][1]=1;vs[39][2]=vs[40][2]=1;vs[45][0]=vs[45][1]=.5f;
        ps[39][0]=ps[52][0]=ps[209][0]=ps[209][1]=ps[223][2]=1;
        // Defined authored material for the tangent/reflection variant. Its
        // albedo multiplier must be nonzero or parity would compare black.
        for(auto& lane:ps[51])lane=1;
        ps[66][0]=ps[72][0]=ps[73][0]=ps[73][1]=ps[73][2]=ps[74][0]=1;
        for(unsigned slot=1;slot<16;++slot)
        {
            Check(device->SetTexture(slot,occluder.Get()));
            Check(device->SetSamplerState(slot,D3DSAMP_MINFILTER,D3DTEXF_POINT));
            Check(device->SetSamplerState(slot,D3DSAMP_MAGFILTER,D3DTEXF_POINT));
            Check(device->SetSamplerState(slot,D3DSAMP_MIPFILTER,D3DTEXF_NONE));
        }
        Check(device->SetVertexShaderConstantF(0,vs[0],256));Check(device->SetPixelShaderConstantF(0,ps[0],224));
        const Vertex quad[]{
            {{-1,-1,0},{.3f,.6f,.7f},0xFF804020,{0,1}},{{1,-1,0},{.3f,.6f,.7f},0xFF804020,{1,1}},
            {{1,1,0},{.3f,.6f,.7f},0xFF804020,{1,0}},{{-1,1,0},{.3f,.6f,.7f},0xFF804020,{0,0}}};
        const WORD indices[]{0,1,2,2,3,0};
        const auto vertices=Buffer(device,quad,sizeof(quad));
        ComPtr<IDirect3DIndexBuffer9> ib;
        Check(device->CreateIndexBuffer(sizeof(indices),D3DUSAGE_WRITEONLY,D3DFMT_INDEX16,D3DPOOL_DEFAULT,&ib,nullptr));
        void* output=nullptr;Check(ib->Lock(0,sizeof(indices),&output,0));std::memcpy(output,indices,sizeof(indices));Check(ib->Unlock());Check(device->SetIndices(ib.Get()));
        std::vector<D3DVERTEXELEMENT9> original{
            {0,0,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_POSITION,0},{0,12,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_NORMAL,0},
            {0,24,D3DDECLTYPE_D3DCOLOR,0,D3DDECLUSAGE_COLOR,0},{0,28,D3DDECLTYPE_FLOAT2,0,D3DDECLUSAGE_TEXCOORD,0}};
        if(tangent)original.push_back({0,36,D3DDECLTYPE_FLOAT4,0,D3DDECLUSAGE_TANGENT,0});
        std::vector<D3DVERTEXELEMENT9> declaration=original;
        original.push_back(D3DDECL_END());
        for(unsigned i=0;i<8;++i)declaration.push_back({1,static_cast<WORD>(i*16),D3DDECLTYPE_FLOAT4,0,D3DDECLUSAGE_TEXCOORD,static_cast<BYTE>(i+1)});
        declaration.push_back(D3DDECL_END());
        ComPtr<IDirect3DVertexDeclaration9> nativeDecl,instanceDecl;
        Check(device->CreateVertexDeclaration(original.data(),&nativeDecl));Check(device->CreateVertexDeclaration(declaration.data(),&instanceDecl));
        bool passed=true;VRRigidInstance::Renderer compatibility,batched;
        for(unsigned eye=0;eye<2;++eye)
        {
            std::array<Instance,count> instances{};
            for(unsigned i=0;i<count;++i)
            {
                auto& rows=instances[i].matrices;
                const float angle=i*.3f,c=std::cos(angle)*.14f,s=std::sin(angle)*.14f;
                rows[0][0]=c;rows[0][1]=s;rows[1][0]=-s;rows[1][1]=c;rows[2][2]=.14f;
                rows[3][0]=(static_cast<int>(i%4)-1.5f)*.5f;rows[3][1]=(static_cast<int>(i/4)-1.5f)*.5f;rows[3][2]=.5f;rows[3][3]=1;
                std::memcpy(rows+4,rows,64);rows[7][0]+=eye ? .02f:-.02f;rows[7][2]=.2f+(i%4)*.2f;
            }
            std::array<VRRigidInstance::Matrices,count> packets{};
            static_assert(sizeof(packets)==sizeof(instances));
            std::memcpy(packets.data(),instances.data(),sizeof(packets));
            const auto instanceBuffer=Buffer(device,instances.data(),sizeof(instances));
            for(unsigned cutout=0;cutout<2;++cutout)
            {
                // The tangent PS uses screen-space fade clipping rather than
                // texture-alpha clipping. Exercise its real texkill path.
                ps[39][0]=cutout && tangent ? 0.f:1.f;
                Check(device->SetPixelShaderConstantF(39,ps[39],1));
                D3DLOCKED_RECT mapped{};Check(atlasUpload->LockRect(0,&mapped,nullptr,0));
                const DWORD colors[2][2]{{0xFF80FF40,0xFFFF4080},{0xFF4080FF,0xFFFFFFFF}};
                for(unsigned y=0;y<2;++y)for(unsigned x=0;x<2;++x)
                    reinterpret_cast<DWORD*>(static_cast<char*>(mapped.pBits)+y*mapped.Pitch)[x]=colors[y][x]&(cutout ? 0x00FFFFFF:0xFFFFFFFF);
                Check(atlasUpload->UnlockRect(0));Check(device->UpdateTexture(atlasUpload.Get(),atlas.Get()));
                std::vector<unsigned char> images[5][4];
                VRMeshPacketProbe::Probe packetCheck(true,true,true);
                VRMeshPacketProbe::Probe liveCheck(true,true,false,true);
                VRRigidInstance::Renderer liveRenderer;
                VRMeshPacketProbe::ShaderReads packetReads{};
                const auto inspect=[&](auto* shader,auto& mask,std::span<bool> samplers)
                {
                    UINT bytes=0;Check(shader->GetFunction(nullptr,&bytes));std::vector<DWORD> code(bytes/4);
                    Check(shader->GetFunction(code.data(),&bytes));ComPtr<ID3DXBuffer> text;
                    Check(D3DXDisassembleShader(code.data(),FALSE,nullptr,&text));
                    if(!VRMeshPacketProbe::ReadAssembly(static_cast<const char*>(text->GetBufferPointer()),mask,samplers))
                        throw std::runtime_error("Fixture shader dependency analysis failed");
                };
                inspect(native,packetReads.vertex,std::span(packetReads.samplers).subspan(16,4));
                inspect(pixel,packetReads.pixel,std::span(packetReads.samplers).first(16));
                for(unsigned mode=0;mode<5;++mode)
                {
                    // Nonzero, spatially varying initial stencil must survive
                    // packet replay, including regions untouched by geometry.
                    Check(device->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER|D3DCLEAR_STENCIL,0,1,7));
                    const D3DRECT seeded{size/2,0,size,size};
                    Check(device->Clear(1,&seeded,D3DCLEAR_STENCIL,0,1,128));Check(device->BeginScene());
                    Check(device->SetTexture(0,atlas.Get()));
                    Check(device->SetStreamSourceFreq(0,1));Check(device->SetStreamSourceFreq(1,1));
                    Check(device->SetStreamSource(1,nullptr,0,0));
                    Check(device->SetStreamSource(0,vertices.Get(),0,sizeof(Vertex)));
                    Check(device->SetVertexDeclaration(mode==1 ? instanceDecl.Get():nativeDecl.Get()));
                    Check(device->SetVertexShader(mode==1 ? instanced:native));
                    if(mode!=1 && mode!=3)
                        for(const auto& instance:instances)
                        {
                            Check(device->SetVertexShaderConstantF(0,instance.matrices[0],4));Check(device->SetVertexShaderConstantF(8,instance.matrices[4],4));
                            if(mode==4)
                            {
                                if(!liveCheck.Record(device,D3DPT_TRIANGLELIST,0,0,4,0,2,[&](auto*,auto*,auto*){return &packetReads;}))
                                    throw std::runtime_error("Live packet unexpectedly rejected");
                            }
                            else if(!mode)
                            {
                                for(unsigned slot=0;slot<16;++slot)if(!packetReads.samplers[slot])
                                    {Check(device->SetTexture(slot,((&instance-instances.data())&1) ? atlas.Get():occluder.Get()));
                                     Check(device->SetSamplerState(slot,D3DSAMP_ADDRESSU,((&instance-instances.data())&1) ? D3DTADDRESS_WRAP:D3DTADDRESS_CLAMP));break;}
                                packetCheck.Record(device,D3DPT_TRIANGLELIST,0,0,4,0,2,
                                    [&](auto*,auto*,auto*){return &packetReads;});
                                Check(device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,0,0,4,0,2));
                            }
                            else
                            {
                                HRESULT result=S_OK;
                                const bool executed=compatibility.TryDraw(device,eye,D3DPT_TRIANGLELIST,0,0,4,0,2,
                                    [instanced,tangent](const std::vector<DWORD>&,bool variant,ComPtr<IDirect3DVertexShader9>& shader)
                                    {if(variant!=tangent)return false;shader=instanced;return true;},
                                    [device](D3DPRIMITIVETYPE kind,INT base,UINT first,UINT vertices,UINT start,UINT triangles)
                                    {return device->DrawIndexedPrimitive(kind,base,first,vertices,start,triangles);},result);
                                if(!executed || compatibility.restorationFailed)throw std::runtime_error("Compatibility renderer fallback/restoration failure");
                                Check(result);
                                ComPtr<IDirect3DVertexShader9> restored;ComPtr<IDirect3DVertexDeclaration9> restoredDeclaration;
                                ComPtr<IDirect3DVertexBuffer9> restoredStream;UINT offset=0,stride=0,f0=0,f1=0;
                                Check(device->GetVertexShader(&restored));Check(device->GetVertexDeclaration(&restoredDeclaration));
                                Check(device->GetStreamSource(1,&restoredStream,&offset,&stride));
                                Check(device->GetStreamSourceFreq(0,&f0));Check(device->GetStreamSourceFreq(1,&f1));
                                if(restored.Get()!=native || restoredDeclaration.Get()!=nativeDecl.Get() || restoredStream || f0!=1 || f1!=1)
                                    throw std::runtime_error("Native bindings not restored");
                            }
                        }
                    else if(mode==1)
                    {
                        Check(device->SetStreamSource(1,instanceBuffer.Get(),0,sizeof(Instance)));
                        Check(device->SetStreamSourceFreq(0,D3DSTREAMSOURCE_INDEXEDDATA|count));
                        Check(device->SetStreamSourceFreq(1,static_cast<UINT>(D3DSTREAMSOURCE_INSTANCEDATA)|1u));
                        Check(device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,0,0,4,0,2));
                    }
                    else
                    {
                        const auto submit=[&](std::span<const VRRigidInstance::Matrices> packet)
                        {
                            HRESULT result=S_OK;
                            const bool executed=batched.TryDraw(device,eye,D3DPT_TRIANGLELIST,0,0,4,0,2,
                                [instanced,tangent](const std::vector<DWORD>&,bool variant,ComPtr<IDirect3DVertexShader9>& shader)
                                {if(variant!=tangent)return false;shader=instanced;return true;},
                                [device](D3DPRIMITIVETYPE kind,INT base,UINT first,UINT vertices,UINT start,UINT triangles)
                                {return device->DrawIndexedPrimitive(kind,base,first,vertices,start,triangles);},result,packet);
                            if(!executed || batched.restorationFailed)throw std::runtime_error("Batched renderer fallback/restoration failure");
                            Check(result);
                            ComPtr<IDirect3DVertexShader9> shader;ComPtr<IDirect3DVertexDeclaration9> decl;
                            ComPtr<IDirect3DVertexBuffer9> stream;UINT offset=0,stride=0,f0=0,f1=0;
                            Check(device->GetVertexShader(&shader));Check(device->GetVertexDeclaration(&decl));
                            Check(device->GetStreamSource(1,&stream,&offset,&stride));
                            Check(device->GetStreamSourceFreq(0,&f0));Check(device->GetStreamSourceFreq(1,&f1));
                            if(shader.Get()!=native || decl.Get()!=nativeDecl.Get() || stream || f0!=1 || f1!=1)
                                throw std::runtime_error("Batch bindings not restored");
                        };
                        if(!cutout)
                        {
                            // Fill almost the entire ring with offscreen instances;
                            // the visible16-instance packet must wrap with DISCARD.
                            std::vector<VRRigidInstance::Matrices> wrap(8190,packets[0]);
                            for(auto& instance:wrap)instance.clip[3][0]=1000;
                            submit(wrap);
                        }
                        submit(packets);
                    }
                    Check(device->SetStreamSourceFreq(0,1));Check(device->SetStreamSourceFreq(1,1));
                    if(!mode)
                    {
                        bool packetValid=false;
                        packetCheck.Compare(device,
                            [instanced](const auto&,bool,auto& shader){shader=instanced;return true;},
                            [device](D3DPRIMITIVETYPE kind,INT base,UINT first,UINT vertices,UINT start,UINT triangles)
                            {return device->DrawIndexedPrimitive(kind,base,first,vertices,start,triangles);},
                            [&](const char* line)
                            {
                                std::puts(line);
                                if(std::strstr(line,"valid=1 restored=1 differingBytes=0"))packetValid=true;
                            });
                        if(!packetValid)throw std::runtime_error("Packet capture/comparison failed");
                        bool orderValid=false,depthValid=false,nativeValid=false,stencilValid=false,nativeStencilValid=false,nativeDepthValid=false;
                        packetCheck.Compare(device,
                            [instanced](const auto&,bool,auto& shader){shader=instanced;return true;},
                            [device](D3DPRIMITIVETYPE kind,INT base,UINT first,UINT vertices,UINT start,UINT triangles)
                            {return device->DrawIndexedPrimitive(kind,base,first,vertices,start,triangles);},
                            [&](const char* line)
                            {
                                std::puts(line);
                                if(std::strstr(line,"mesh-order-probe capturedSubset=16 referenceDraws=16 groupedDraws=1 valid=1 restored=1 differingColorBytes=0"))orderValid=true;
                                if(std::strstr(line,"mesh-order-probe differingDepthPixels=0 referenceDepthPixels="))depthValid=true;
                                if(std::strstr(line,"mesh-replay-probe nativeCompared=1 nativeColorDifferences=0 otherDraws=0 queryEvents=0"))nativeValid=true;
                                const std::string expectedStencil="mesh-order-probe differingStencilPixels=0 referenceStencilPixels="+std::to_string(size*size);
                                if(std::strstr(line,expectedStencil.c_str()))stencilValid=true;
                                if(std::strstr(line,"mesh-replay-probe nativeStencilCompared=1 nativeStencilDifferences=0"))nativeStencilValid=true;
                                if(std::strstr(line,"mesh-replay-probe nativeDepthCompared=1 nativeDepthDifferences=0"))nativeDepthValid=true;
                            },true);
                        if(!nativeDepthValid || !orderValid || !depthValid || !nativeValid || !stencilValid || !nativeStencilValid)
                            throw std::runtime_error("Ordered packet native/color/depth/stencil comparison failed");
                    }
                    if(mode==4)
                    {
                        Check(liveCheck.FlushLive(device,eye,liveRenderer,
                            [instanced](const auto&,bool,auto& shader){shader=instanced;return true;},
                            [device](D3DPRIMITIVETYPE kind,INT base,UINT first,UINT vertices,UINT start,UINT triangles)
                            {return device->DrawIndexedPrimitive(kind,base,first,vertices,start,triangles);}));
                        if(liveCheck.liveQueued!=16 || liveCheck.liveEmitted!=1 || liveCheck.liveBatches!=1 || liveCheck.failures)
                            throw std::runtime_error("Live native draw replacement did not batch16 to1");
                    }
                    if(!cutout)
                    {
                        // A later magenta plane must cover only the farther half
                        // of the instances. Verify native shader-written depth
                        // indirectly through all four resulting color targets.
                        const float plane[4][4]{{1,0,0,0},{0,1,0,0},{0,0,1,0},{0,0,.5f,1}};
                        Check(device->SetTexture(0,occluder.Get()));Check(device->SetVertexShader(native));
                        Check(device->SetVertexDeclaration(nativeDecl.Get()));
                        Check(device->SetVertexShaderConstantF(0,plane[0],4));Check(device->SetVertexShaderConstantF(8,plane[0],4));
                        Check(device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,0,0,4,0,2));
                    }
                    Check(device->EndScene());
                    for(unsigned target=0;target<4;++target)
                    {
                        Check(device->GetRenderTargetData(targets[target].Get(),readback.Get()));Check(readback->LockRect(&mapped,nullptr,D3DLOCK_READONLY));
                        auto& image=images[mode][target];image.resize(size*size*4);
                        for(unsigned y=0;y<size;++y)std::memcpy(image.data()+y*size*4,static_cast<const char*>(mapped.pBits)+y*mapped.Pitch,size*4);
                        Check(readback->UnlockRect());
                    }
                }
                for(unsigned target=0;target<4;++target)
                {
                    unsigned mismatches=0,compatibilityMismatches=0,batchMismatches=0,liveMismatches=0,lit=0,planePixels=0;
                    for(unsigned pixelIndex=0;pixelIndex<size*size;++pixelIndex)
                    {
                        const auto offset=pixelIndex*4;
                        mismatches+=std::memcmp(images[0][target].data()+offset,images[1][target].data()+offset,4)!=0;
                        compatibilityMismatches+=std::memcmp(images[0][target].data()+offset,images[2][target].data()+offset,4)!=0;
                        batchMismatches+=std::memcmp(images[0][target].data()+offset,images[3][target].data()+offset,4)!=0;
                        liveMismatches+=std::memcmp(images[0][target].data()+offset,images[4][target].data()+offset,4)!=0;
                        lit+=(images[0][target][offset]|images[0][target][offset+1]|images[0][target][offset+2]|images[0][target][offset+3])!=0;
                        DWORD color=0;std::memcpy(&color,images[0][target].data()+offset,4);planePixels+=color==0xFFFF00FF;
                    }
                    std::printf("rigid eye=%u cutout=%u target=%u mismatchedPixels=%u compatibilityMismatches=%u batchMismatches=%u liveMismatches=%u litPixels=%u\n",eye,cutout,target,mismatches,compatibilityMismatches,batchMismatches,liveMismatches,lit);
                    passed &= !mismatches && !compatibilityMismatches && !batchMismatches && !liveMismatches && (cutout ? lit==0:(target!=0 || lit>0));
                    if(!cutout && target==0)
                    {std::printf("depth plane pixels=%u foregroundPixels=%u\n",planePixels,size*size-planePixels);passed &= planePixels>0 && planePixels<size*size;}
                }
            }
        }
        for(unsigned i=1;i<4;++i)device->SetRenderTarget(i,nullptr);
        std::printf("compatibility draws=%llu,%llu failures=%llu restorationFailed=%u\n",compatibility.draws[0],compatibility.draws[1],compatibility.failures,compatibility.restorationFailed);
        std::printf("batched draws=%llu,%llu instances=%llu,%llu failures=%llu restorationFailed=%u\n",batched.draws[0],batched.draws[1],batched.instanceCounts[0],batched.instanceCounts[1],batched.failures,batched.restorationFailed);
        return passed;
    }
}
int wmain(int argc,wchar_t** argv)
{
    if(argc!=4 && (argc!=5 || std::wcscmp(argv[4],L"--tangent"))){std::fprintf(stderr,"Usage: rigid-instance-prefix backend.dll local-native-vs.asm local-native-ps.asm [--tangent]\n");return 2;}
    try
    {
        const auto original=Read(argv[2]);std::string prefix;
        const bool tangent=argc==5;
        if(!VRRigidInstanceShader::AddInstanceMatrices(original,prefix,tangent))throw std::runtime_error("Unsupported rigid shader variant");
        const auto oldCode=Assemble(original),newCode=Assemble(prefix),pixelCode=Assemble(Read(argv[3]));
        const auto module=LoadLibraryW(argv[1]);if(!module)throw std::runtime_error("Backend load failed");
        using Create=HRESULT (WINAPI*)(UINT,IDirect3D9Ex**);
        const auto create=reinterpret_cast<Create>(GetProcAddress(module,"Direct3DCreate9Ex"));if(!create)throw std::runtime_error("D3D9Ex export missing");
        ComPtr<IDirect3D9Ex> d3d;Check(create(D3D_SDK_VERSION,&d3d));
        const auto window=CreateWindowExW(0,L"STATIC",L"Rigid instance equivalence",WS_OVERLAPPEDWINDOW,0,0,128,128,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        if(!window)throw std::runtime_error("Hidden test window creation failed");
        D3DPRESENT_PARAMETERS parameters{};parameters.Windowed=TRUE;parameters.SwapEffect=D3DSWAPEFFECT_DISCARD;
        parameters.hDeviceWindow=window;parameters.BackBufferWidth=parameters.BackBufferHeight=128;parameters.BackBufferFormat=D3DFMT_A8R8G8B8;
        ComPtr<IDirect3DDevice9Ex> device;Check(d3d->CreateDeviceEx(D3DADAPTER_DEFAULT,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING,&parameters,nullptr,&device));
        ComPtr<IDirect3DVertexShader9> native,instanced;ComPtr<IDirect3DPixelShader9> pixel;
        Check(device->CreateVertexShader(static_cast<const DWORD*>(oldCode->GetBufferPointer()),&native));
        Check(device->CreateVertexShader(static_cast<const DWORD*>(newCode->GetBufferPointer()),&instanced));
        Check(device->CreatePixelShader(static_cast<const DWORD*>(pixelCode->GetBufferPointer()),&pixel));
        std::printf("Rigid shader creation original=%zu instanced=%zu\n",oldCode->GetBufferSize(),newCode->GetBufferSize());
        const bool passed=PixelCheck(device.Get(),native.Get(),instanced.Get(),pixel.Get(),tangent);
        pixel.Reset();instanced.Reset();native.Reset();device.Reset();d3d.Reset();DestroyWindow(window);
        std::printf("%s; synthetic four-target parity only, no in-game batching or performance claim\n",passed ? "PASS":"FAIL");return passed ? 0:1;
    }
    catch(const std::exception& error){std::fprintf(stderr,"%s\n",error.what());return 1;}
}
