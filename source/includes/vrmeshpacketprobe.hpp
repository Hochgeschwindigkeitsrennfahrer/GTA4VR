#pragma once
#include <d3d9.h>
#include <d3dx9shader.h>
#include <wrl/client.h>
#include <array>
#include <vector>
#include <span>
#include <cstring>
#include <algorithm>
#include <new>
#include <utility>
#include <string>
#include <sstream>
#include <regex>
#include <unordered_map>
#include "vrrigidinstance.hpp"

namespace VRMeshPacketProbe
{
    using Microsoft::WRL::ComPtr;
    struct CpuScope
    {
        long long* total;LARGE_INTEGER begin{};
        explicit CpuScope(long long* value):total(value){if(total)QueryPerformanceCounter(&begin);}
        ~CpuScope(){if(total){LARGE_INTEGER end{};QueryPerformanceCounter(&end);*total+=end.QuadPart-begin.QuadPart;}}
    };
    struct ShaderReads { std::array<bool,256> vertex{},pixel{};std::array<bool,20> samplers{}; };
    inline bool ReadAssembly(const std::string& text,std::array<bool,256>& constants,std::span<bool> samplers)
    {
        constants.fill(false);std::fill(samplers.begin(),samplers.end(),false);
        std::array<bool,256> definitions{};std::istringstream input(text);std::string line;
        const std::regex registers(R"(\bc(\d+)\b)"),textures(R"(\bs(\d+)\b)"),relative(R"(\b[cs]\d*\s*\[)");
        while(std::getline(input,line))
        {
            const auto comment=line.find("//");if(comment!=std::string::npos)line.resize(comment);
            const auto start=line.find_first_not_of(" \t\r");if(start==std::string::npos)continue;
            const auto instruction=line.substr(start);
            if(instruction.starts_with("dcl_"))continue;
            if(std::regex_search(instruction,relative))return false;
            const bool definition=instruction.starts_with("def ");
            for(std::sregex_iterator i(instruction.begin(),instruction.end(),registers),end;i!=end;++i)
            {const auto index=std::stoul((*i)[1].str());if(index>=256)return false;if(definition)definitions[index]=true;else constants[index]=true;}
            for(std::sregex_iterator i(instruction.begin(),instruction.end(),textures),end;i!=end;++i)
            {const auto index=std::stoul((*i)[1].str());if(index>=samplers.size())return false;samplers[index]=true;}
        }
        for(unsigned i=0;i<256;++i)if(definitions[i])constants[i]=false;
        return true;
    }
    // One-frame scratch comparison, plus an explicit bounded live experiment.
    // Owns state/resource references, never reads game geometry buffers back.
    // Resources are not inherently immutable: live integration must flush on
    // writes and queries, and cannot infer general reordering safety from one
    // scene's parity. Distributed defaults never enable live replacement.
    class Probe
    {
        struct Group
        {
            std::array<uintptr_t,14> geometry{};
            bool batchable=false;
            std::vector<DWORD> state;
            std::array<size_t,6> stateEnds{};
            ComPtr<IDirect3DStateBlock9> bindings;
            std::vector<VRRigidInstance::Matrices> matrices;
            D3DPRIMITIVETYPE type{};INT base=0;UINT minimum=0,vertices=0,start=0,primitives=0;
        };
        std::vector<Group> groups;
        std::unordered_multimap<size_t,size_t> groupLookup;
        struct Occurrence {size_t group=0,instance=0;uintptr_t material=0;};
        std::vector<Occurrence> sequence;
        ULONGLONG started=GetTickCount64();
        unsigned drawLimit=2048,groupLimit=1024,timeLimitMs=1500;
        bool allIndexed=false,benchmark=false,live=false;
        bool allocationFailed=false;
        bool initialStencilFailed=false;
        HRESULT initialStencilResult=S_OK;
        ComPtr<IDirect3DTexture9> initialStencil;
        unsigned barrier=0;
        std::array<DWORD,9> previousStencil{};
        bool haveStencil=false;
        unsigned stencilBoundaries=0;
        bool reading=false;
        static bool Add(std::vector<DWORD>& key,HRESULT result,DWORD value)
        {key.push_back(static_cast<DWORD>(result));key.push_back(value);return SUCCEEDED(result)||result==D3DERR_INVALIDCALL;}
        static HRESULT ReadPixels(IDirect3DDevice9* device,IDirect3DSurface9* target,IDirect3DSurface9* staging,
            const D3DSURFACE_DESC& description,std::vector<unsigned char>& output)
        {
            unsigned bytes=0;
            switch(description.Format)
            {case D3DFMT_A8R8G8B8:case D3DFMT_X8R8G8B8:case D3DFMT_A2R10G10B10:case D3DFMT_R32F:case D3DFMT_G16R16F:
            case D3DFMT_A8B8G8R8:case D3DFMT_X8B8G8R8:bytes=4;break;
            case D3DFMT_A16B16G16R16:case D3DFMT_A16B16G16R16F:case D3DFMT_G32R32F:bytes=8;break;
            case D3DFMT_A32B32G32R32F:bytes=16;break;case D3DFMT_R16F:bytes=2;break;default:return E_NOTIMPL;}
            const auto result=device->GetRenderTargetData(target,staging);if(FAILED(result))return result;
            D3DLOCKED_RECT mapped{};const auto locked=staging->LockRect(&mapped,nullptr,D3DLOCK_READONLY);if(FAILED(locked))return locked;
            try {output.resize(static_cast<size_t>(description.Width)*description.Height*bytes);}
            catch(const std::bad_alloc&){staging->UnlockRect();return E_OUTOFMEMORY;}
            for(unsigned y=0;y<description.Height;++y)std::memcpy(output.data()+static_cast<size_t>(y)*description.Width*bytes,
                static_cast<const char*>(mapped.pBits)+y*mapped.Pitch,description.Width*bytes);
            return staging->UnlockRect();
        }
        template<class Check>
        static bool ReadStencil(IDirect3DDevice9* device,IDirect3DSurface9* depth,IDirect3DSurface9* color,
            IDirect3DSurface9* staging,const D3DSURFACE_DESC& d,IDirect3DPixelShader9* shader,
            std::vector<unsigned char>& output,Check&& check,bool readback=true)
        {
            bool ok=check("stencil read unbind texture",device->SetTexture(0,nullptr));
            for(unsigned target=1;target<4;++target)ok=check("stencil read unbind MRT",device->SetRenderTarget(target,nullptr)) && ok;
            ok=check("stencil read target",device->SetRenderTarget(0,color)) &&
                check("stencil read depth",device->SetDepthStencilSurface(depth)) &&
                check("stencil read PS",device->SetPixelShader(shader)) &&
                check("stencil read VS",device->SetVertexShader(nullptr)) &&
                check("stencil read FVF",device->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1)) && ok;
            for(const auto state:{D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,D3DRS_ALPHATESTENABLE,D3DRS_SCISSORTESTENABLE,
                D3DRS_SRGBWRITEENABLE,D3DRS_FOGENABLE,D3DRS_CLIPPLANEENABLE,D3DRS_TWOSIDEDSTENCILMODE,D3DRS_SEPARATEALPHABLENDENABLE})
                ok=check("stencil read state",device->SetRenderState(state,0)) && ok;
            const std::pair<D3DRENDERSTATETYPE,DWORD> states[]{
                {D3DRS_CULLMODE,D3DCULL_NONE},{D3DRS_FILLMODE,D3DFILL_SOLID},{D3DRS_COLORWRITEENABLE,15},
                {D3DRS_ALPHABLENDENABLE,TRUE},{D3DRS_SRCBLEND,D3DBLEND_ONE},{D3DRS_DESTBLEND,D3DBLEND_ONE},{D3DRS_BLENDOP,D3DBLENDOP_ADD},
                {D3DRS_STENCILENABLE,TRUE},{D3DRS_STENCILFUNC,D3DCMP_EQUAL},{D3DRS_STENCILFAIL,D3DSTENCILOP_KEEP},
                {D3DRS_STENCILZFAIL,D3DSTENCILOP_KEEP},{D3DRS_STENCILPASS,D3DSTENCILOP_KEEP},{D3DRS_STENCILWRITEMASK,0}};
            for(const auto& state:states)ok=check("stencil read pipeline",device->SetRenderState(state.first,state.second)) && ok;
            for(unsigned stream=0;stream<16;++stream)ok=check("stencil read stream frequency",device->SetStreamSourceFreq(stream,1)) && ok;
            const D3DVIEWPORT9 viewport{0,0,d.Width,d.Height,0,1};
            ok=check("stencil read viewport",device->SetViewport(&viewport)) &&
                check("stencil read clear color",device->Clear(0,nullptr,D3DCLEAR_TARGET,0,1,0)) && ok;
            struct Vertex {float x,y,z,rhw,u,v;};
            const Vertex quad[]{{-.5f,-.5f,0,1,0,0},{static_cast<float>(d.Width)-.5f,-.5f,0,1,1,0},
                {-.5f,static_cast<float>(d.Height)-.5f,0,1,0,1},{static_cast<float>(d.Width)-.5f,static_cast<float>(d.Height)-.5f,0,1,1,1}};
            // Read each bit through EQUAL testing, accumulating its exact integer
            // value in R32F. Depth writes are OFF, all stencil ops KEEP, write mask
            // ZERO. The source depth/stencil is attached for READ-ONLY tests only.
            for(unsigned bit=0;bit<8 && ok;++bit)
            {
                const DWORD mask=1u<<bit;const float weight[4]{static_cast<float>(mask),0,0,0};
                ok=check("stencil read mask",device->SetRenderState(D3DRS_STENCILMASK,mask)) &&
                    check("stencil read reference",device->SetRenderState(D3DRS_STENCILREF,mask)) &&
                    check("stencil read weight",device->SetPixelShaderConstantF(0,weight,1));
                if(ok)ok=check("stencil read draw",device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(Vertex)));
            }
            return ok && (!readback || check("stencil read pixels",ReadPixels(device,color,staging,d,output)));
        }
        template<class Check>
        static bool ReadDepth(IDirect3DDevice9* device,IDirect3DTexture9* texture,IDirect3DSurface9* color,
            IDirect3DSurface9* staging,const D3DSURFACE_DESC& d,IDirect3DPixelShader9* shader,
            std::vector<unsigned char>& output,Check&& check)
        {
            bool ok=true;
            // Isolate the copy from every captured pipeline state.
            // All state and native target bindings are restored below.
            ok=check("unbind sampled depth",device->SetDepthStencilSurface(nullptr));
            for(unsigned target=1;target<4;++target)ok=check("unbind MRT",device->SetRenderTarget(target,nullptr)) && ok;
            ok=check("bind depth copy",device->SetRenderTarget(0,color)) && ok;
            ok=check("depth copy shader",device->SetPixelShader(shader)) &&
                check("depth copy vertex",device->SetVertexShader(nullptr)) &&
                check("depth copy FVF",device->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1)) && ok;
            for(const auto state:{D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,D3DRS_STENCILENABLE,D3DRS_ALPHABLENDENABLE,
                D3DRS_ALPHATESTENABLE,D3DRS_SCISSORTESTENABLE,D3DRS_SRGBWRITEENABLE,D3DRS_FOGENABLE,D3DRS_CLIPPLANEENABLE})
                ok=check("depth copy state",device->SetRenderState(state,0)) && ok;
            ok=check("depth copy cull",device->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE)) &&
                check("depth copy color mask",device->SetRenderState(D3DRS_COLORWRITEENABLE,15)) &&
                check("depth copy fill",device->SetRenderState(D3DRS_FILLMODE,D3DFILL_SOLID)) && ok;
            for(unsigned stream=0;stream<16;++stream)ok=check("depth copy frequency",device->SetStreamSourceFreq(stream,1)) && ok;
            for(const auto state:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER,D3DSAMP_MIPFILTER})
                ok=check("depth copy filter",device->SetSamplerState(0,state,state==D3DSAMP_MIPFILTER ? D3DTEXF_NONE:D3DTEXF_POINT)) && ok;
            for(const auto state:{D3DSAMP_ADDRESSU,D3DSAMP_ADDRESSV})ok=check("depth copy address",device->SetSamplerState(0,state,D3DTADDRESS_CLAMP)) && ok;
            ok=check("depth copy sRGB",device->SetSamplerState(0,D3DSAMP_SRGBTEXTURE,0)) &&
                check("depth texture",device->SetTexture(0,texture)) && ok;
            const auto width=d.Width,height=d.Height;
            const D3DVIEWPORT9 viewport{0,0,width,height,0,1};
            ok=check("depth copy viewport",device->SetViewport(&viewport)) && ok;
            struct Vertex {float x,y,z,rhw,u,v;};
            const Vertex quad[]{{-.5f,-.5f,0,1,0,0},{static_cast<float>(width)-.5f,-.5f,0,1,1,0},
                {-.5f,static_cast<float>(height)-.5f,0,1,0,1},{static_cast<float>(width)-.5f,static_cast<float>(height)-.5f,0,1,1,1}};
            if(ok)ok=check("depth copy draw",device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(Vertex)));
            return ok && check("depth read pixels",ReadPixels(device,color,staging,d,output));
        }
        bool SnapshotInitialStencil(IDirect3DDevice9* device)
        {
            ComPtr<IDirect3DStateBlock9> state;ComPtr<IDirect3DSurface9> targets[4],depth,target;
            if(FAILED(initialStencilResult=device->CreateStateBlock(D3DSBT_ALL,&state)))return false;
            for(unsigned i=0;i<4;++i)if(FAILED(initialStencilResult=device->GetRenderTarget(i,&targets[i])))return false;
            if(FAILED(initialStencilResult=device->GetDepthStencilSurface(&depth)))return false;
            D3DSURFACE_DESC d{};if(FAILED(initialStencilResult=targets[0]->GetDesc(&d)))return false;
            ComPtr<IDirect3D9> root;D3DDEVICE_CREATION_PARAMETERS creation{};D3DDISPLAYMODE display{};
            if(FAILED(initialStencilResult=device->GetDirect3D(&root)) || FAILED(initialStencilResult=device->GetCreationParameters(&creation)) ||
                FAILED(initialStencilResult=root->GetAdapterDisplayMode(creation.AdapterOrdinal,&display)) ||
                FAILED(initialStencilResult=root->CheckDeviceFormat(creation.AdapterOrdinal,creation.DeviceType,display.Format,
                    D3DUSAGE_RENDERTARGET|D3DUSAGE_QUERY_POSTPIXELSHADER_BLENDING,D3DRTYPE_SURFACE,D3DFMT_R32F)))return false;
            if(FAILED(initialStencilResult=device->CreateTexture(d.Width,d.Height,1,D3DUSAGE_RENDERTARGET,D3DFMT_R32F,
                D3DPOOL_DEFAULT,&initialStencil,nullptr)) || FAILED(initialStencilResult=initialStencil->GetSurfaceLevel(0,&target)) ||
                FAILED(initialStencilResult=target->GetDesc(&d)))return false;
            const char assembly[]="ps_3_0\nmov oC0, c0\n";ComPtr<ID3DXBuffer> code,errors;ComPtr<IDirect3DPixelShader9> shader;
            if(FAILED(initialStencilResult=D3DXAssembleShader(assembly,sizeof(assembly)-1,nullptr,nullptr,0,&code,&errors)) ||
                FAILED(initialStencilResult=device->CreatePixelShader(static_cast<const DWORD*>(code->GetBufferPointer()),&shader)))return false;
            std::vector<unsigned char> unused;
            const auto check=[&](const char*,HRESULT result){if(FAILED(result))initialStencilResult=result;return SUCCEEDED(result);};
            reading=true;
            bool ok=ReadStencil(device,depth.Get(),target.Get(),nullptr,d,shader.Get(),unused,check,false);
            for(unsigned i=0;i<4;++i)ok=check("restore target",device->SetRenderTarget(i,targets[i].Get())) && ok;
            ok=check("restore depth",device->SetDepthStencilSurface(depth.Get())) && ok;
            ok=check("restore state",state->Apply()) && ok;
            reading=false;
            return ok;
        }
    public:
        unsigned observed=0,seen=0,eligible=0,dropped=0,failures=0,otherDraws=0,queryEvents=0;
        unsigned liveQueued=0,liveEmitted=0,liveBatches=0,liveFlushes=0;
        long long liveCollectTicks=0,liveStateCaptureTicks=0,liveEmitTicks=0,liveApplyTicks=0,liveInstanceTicks=0;
        explicit Probe(bool extended=false,bool all=false,bool timed=false,bool replacement=false):allIndexed(all),benchmark(timed),live(replacement)
        {if(extended){drawLimit=8192;groupLimit=all ? 5120:4096;timeLimitMs=4000;}}
        bool Observing() const {return !reading;}
        void AllocationFailure(){allocationFailed=true;++failures;}
        void OtherDraw(){if(!reading){++otherDraws;++barrier;}}
        void QueryEvent(){if(!reading){++queryEvents;++barrier;}}
        bool Capturing() const {return !reading && !allocationFailed && seen<drawLimit && GetTickCount64()-started<timeLimitMs;}
        template<class Inspect>
        bool Record(IDirect3DDevice9* device,D3DPRIMITIVETYPE type,INT base,UINT minimum,
            UINT vertices,UINT start,UINT primitives,Inspect&& inspect,uintptr_t material=0)
        {
            CpuScope collect(live ? &liveCollectTicks:nullptr);
            if(!reading)++observed;
            if(!Capturing())return false;
            if(!live && allIndexed && !initialStencil && !initialStencilFailed)
            {
                if(queryEvents || !SnapshotInitialStencil(device)){initialStencilFailed=true;++failures;return false;}
            }
            if(initialStencilFailed)return false;
            ++seen;
            if(type!=D3DPT_TRIANGLELIST && !allIndexed)return false;
            ComPtr<IDirect3DVertexShader9> vs;ComPtr<IDirect3DPixelShader9> ps;
            ComPtr<IDirect3DVertexDeclaration9> declaration;ComPtr<IDirect3DVertexBuffer9> vb;
            ComPtr<IDirect3DIndexBuffer9> ib;UINT offset=0,stride=0;
            if(FAILED(device->GetVertexShader(&vs))||FAILED(device->GetPixelShader(&ps))||
                FAILED(device->GetVertexDeclaration(&declaration))||
                FAILED(device->GetStreamSource(0,&vb,&offset,&stride))||!vb||
                FAILED(device->GetIndices(&ib))||!ib){++failures;return false;}
            D3DVERTEXBUFFER_DESC vd{};D3DINDEXBUFFER_DESC id{};
            if(FAILED(vb->GetDesc(&vd))||FAILED(ib->GetDesc(&id))){++failures;return false;}
            const bool staticTriangles=type==D3DPT_TRIANGLELIST && !((vd.Usage|id.Usage)&D3DUSAGE_DYNAMIC);
            const ShaderReads* reads=staticTriangles && vs && ps && declaration ? inspect(vs.Get(),ps.Get(),declaration.Get()):nullptr;
            if(!reads && !allIndexed)return false;
            bool batchable=reads!=nullptr;
            std::vector<DWORD> state;state.reserve(2400);std::array<size_t,6> stateEnds{};
            float vc[256][4]{},pc[224][4]{};VRRigidInstance::Matrices matrices{};
            if(FAILED(device->GetVertexShaderConstantF(0,vc[0],256))||
                FAILED(device->GetPixelShaderConstantF(0,pc[0],224))){++failures;return false;}
            std::memcpy(matrices.world,vc,sizeof(matrices.world));std::memcpy(matrices.clip,vc+8,sizeof(matrices.clip));
            for(unsigned i=0;i<256;++i)
                if(!reads || (reads->vertex[i] && !(i<4 || (i>=8 && i<12))))
                    for(float lane:vc[i]){DWORD bits;std::memcpy(&bits,&lane,4);state.push_back(bits);}
            stateEnds[0]=state.size();
            for(unsigned i=0;i<224;++i)if(!reads || reads->pixel[i])
                for(float lane:pc[i]){DWORD bits;std::memcpy(&bits,&lane,4);state.push_back(bits);}
            stateEnds[1]=state.size();
            int ints[16][4]{};BOOL bools[16]{};
            for(unsigned shader=0;shader<2;++shader)
            {
                const auto a=shader ? device->GetPixelShaderConstantI(0,ints[0],16):device->GetVertexShaderConstantI(0,ints[0],16);
                const auto b=shader ? device->GetPixelShaderConstantB(0,bools,16):device->GetVertexShaderConstantB(0,bools,16);
                if(FAILED(a)||FAILED(b)){++failures;return false;}
                for(const auto& row:ints)for(int lane:row)state.push_back(static_cast<DWORD>(lane));
                for(BOOL lane:bools)state.push_back(static_cast<DWORD>(lane));
            }
            stateEnds[2]=state.size();
            for(unsigned i=1;i<=209;++i)
            {DWORD value=0;const auto result=device->GetRenderState(static_cast<D3DRENDERSTATETYPE>(i),&value);if(!Add(state,result,value)){++failures;return false;}}
            stateEnds[3]=state.size();
            for(unsigned i=0;i<20;++i)
            {
                if(reads && !reads->samplers[i])continue;
                const DWORD slot=i<16 ? i:D3DVERTEXTEXTURESAMPLER0+i-16;
                ComPtr<IDirect3DBaseTexture9> texture;
                if(FAILED(device->GetTexture(slot,&texture))){++failures;return false;}
                if(live && texture)
                {
                    // Do not defer draws sampling a changing render target or
                    // dynamic texture. This first live path accepts static 2D
                    // textures only; other resources stay native.
                    if(texture->GetType()!=D3DRTYPE_TEXTURE)return false;
                    D3DSURFACE_DESC description{};
                    if(FAILED(static_cast<IDirect3DTexture9*>(texture.Get())->GetLevelDesc(0,&description)) ||
                        (description.Usage&(D3DUSAGE_DYNAMIC|D3DUSAGE_RENDERTARGET)))return false;
                }
                state.push_back(static_cast<DWORD>(reinterpret_cast<uintptr_t>(texture.Get())));
                for(unsigned s=1;s<=13;++s)
                {DWORD value=0;const auto result=device->GetSamplerState(slot,static_cast<D3DSAMPLERSTATETYPE>(s),&value);if(!Add(state,result,value)){++failures;return false;}}
            }
            stateEnds[4]=state.size();
            for(unsigned i=0;i<16;++i)
            {
                ComPtr<IDirect3DVertexBuffer9> buffer;UINT o=0,s=0,f=0;
                if(FAILED(device->GetStreamSource(i,&buffer,&o,&s))||FAILED(device->GetStreamSourceFreq(i,&f))){++failures;return false;}
                if(f!=1 || (i && buffer))
                {if(!allIndexed)return false;batchable=false;}
                state.push_back(o);state.push_back(s);state.push_back(f);
            }
            D3DVIEWPORT9 viewport{};RECT scissor{};
            if(FAILED(device->GetViewport(&viewport))||FAILED(device->GetScissorRect(&scissor))){++failures;return false;}
            const auto appendBytes=[&](const auto& value)
            {const auto old=state.size();state.resize(old+sizeof(value)/4);std::memcpy(state.data()+old,&value,sizeof(value));};
            appendBytes(viewport);appendBytes(scissor);
            for(unsigned i=0;i<6;++i)
            {float plane[4]{};if(FAILED(device->GetClipPlane(i,plane))){++failures;return false;}appendBytes(plane);}
            for(unsigned i=0;i<5;++i)
            {
                ComPtr<IDirect3DSurface9> surface;
                const auto result=i<4 ? device->GetRenderTarget(i,&surface):device->GetDepthStencilSurface(&surface);
                if(FAILED(result)&&result!=D3DERR_NOTFOUND){++failures;return false;}
                state.push_back(static_cast<DWORD>(reinterpret_cast<uintptr_t>(surface.Get())));
            }
            stateEnds[5]=state.size();
            if(allIndexed)
            {
                DWORD blend=0,write=0,z=0,zfunc=0;
                if(FAILED(device->GetRenderState(D3DRS_ALPHABLENDENABLE,&blend)) || FAILED(device->GetRenderState(D3DRS_ZWRITEENABLE,&write)) ||
                    FAILED(device->GetRenderState(D3DRS_ZENABLE,&z)) || FAILED(device->GetRenderState(D3DRS_ZFUNC,&zfunc))){++failures;return false;}
                constexpr D3DRENDERSTATETYPE stencilStates[]{D3DRS_STENCILENABLE,D3DRS_STENCILFUNC,D3DRS_STENCILFAIL,D3DRS_STENCILZFAIL,
                    D3DRS_STENCILPASS,D3DRS_STENCILREF,D3DRS_STENCILMASK,D3DRS_STENCILWRITEMASK,D3DRS_TWOSIDEDSTENCILMODE};
                std::array<DWORD,9> stencil{};
                for(unsigned i=0;i<stencil.size();++i)
                    if(FAILED(device->GetRenderState(stencilStates[i],&stencil[i]))){++failures;return false;}
                // The observed rigid G-buffer pass uses ALWAYS/KEEP/KEEP/REPLACE.
                // Within one unchanged stencil signature, passing fragments only
                // replace the same bits with the same value. Signature changes
                // are ordering barriers. Never permit stencil tests, increments,
                // two-sided operations or other order-dependent stencil behavior.
                if(haveStencil && stencil!=previousStencil){++barrier;++stencilBoundaries;}
                previousStencil=stencil;haveStencil=true;
                const bool simpleStencil=!stencil[0] || (stencil[1]==D3DCMP_ALWAYS && stencil[2]==D3DSTENCILOP_KEEP &&
                    stencil[3]==D3DSTENCILOP_KEEP && (stencil[4]==D3DSTENCILOP_KEEP || stencil[4]==D3DSTENCILOP_REPLACE) && !stencil[8]);
                if(blend || !write || z!=D3DZB_TRUE || (zfunc!=D3DCMP_LESS && zfunc!=D3DCMP_LESSEQUAL) || !simpleStencil)batchable=false;
                if(!batchable)++barrier;
            }
            const std::array<uintptr_t,14> geometry{reinterpret_cast<uintptr_t>(vs.Get()),reinterpret_cast<uintptr_t>(ps.Get()),
                reinterpret_cast<uintptr_t>(declaration.Get()),reinterpret_cast<uintptr_t>(vb.Get()),reinterpret_cast<uintptr_t>(ib.Get()),
                offset,stride,static_cast<uintptr_t>(type),static_cast<uintptr_t>(base),minimum,vertices,start,primitives,barrier};
            if(live && !batchable)return false;
            eligible+=batchable;
            // Hash only selects candidates; complete equality remains the gate.
            // Linear scanning thousands of prior groups per draw dominated
            // collection and is inappropriate in a live replacement.
            size_t hash=2166136261u;
            for(auto value:geometry)hash=(hash^value)*16777619u;
            for(auto value:state)hash=(hash^value)*16777619u;
            auto found=groups.end();
            if(batchable)
            {
                const auto range=groupLookup.equal_range(hash);
                for(auto candidate=range.first;candidate!=range.second;++candidate)
                {
                    auto& group=groups[candidate->second];
                    if(group.geometry==geometry && group.state==state){found=groups.begin()+candidate->second;break;}
                }
            }
            if(found==groups.end())
            {
                if(groups.size()==groupLimit){++dropped;return false;}
                Group group{};group.geometry=geometry;group.state=std::move(state);group.stateEnds=stateEnds;
                group.batchable=batchable;
                group.type=type;group.base=base;group.minimum=minimum;group.vertices=vertices;group.start=start;group.primitives=primitives;
                {CpuScope capture(live ? &liveStateCaptureTicks:nullptr);
                if(FAILED(device->CreateStateBlock(D3DSBT_ALL,&group.bindings))){++failures;return false;}}
                groups.push_back(std::move(group));found=groups.end()-1;
                if(batchable)groupLookup.emplace(hash,groups.size()-1);
            }
            if(found->matrices.size()<128)
            {
                sequence.reserve(sequence.size()+1);found->matrices.reserve(found->matrices.size()+1);
                sequence.push_back({static_cast<size_t>(found-groups.begin()),found->matrices.size(),material});
                found->matrices.push_back(matrices);
                if(live)++liveQueued;
                return true;
            }
            else ++dropped;
            return false;
        }
        // Bounded live experiment uses the parity-tested last-occurrence order.
        // Opaque equal-depth ordering still needs scene-specific validation;
        // this is NOT a distributed/continuous gameplay optimization.
        // Call before resource writes, queries, other draws, clears,
        // attachment changes and pass exit. The collector cost is NOT excluded.
        template<class Adapt,class Draw>
        HRESULT FlushLive(IDirect3DDevice9* device,unsigned eye,VRRigidInstance::Renderer& renderer,
            Adapt&& adapt,Draw&& draw)
        {
            if(!live || reading || sequence.empty())return S_OK;
            CpuScope emission(&liveEmitTicks);
            reading=true;++liveFlushes;
            ComPtr<IDirect3DStateBlock9> restore;
            HRESULT result=device->CreateStateBlock(D3DSBT_ALL,&restore);
            // Without a restoration snapshot, leave the queue intact and report
            // failure. Never pretend accepted-but-unsubmitted geometry succeeded.
            if(FAILED(result)){reading=false;++failures;return result;}
            for(const auto& occurrence:sequence)
            {
                const auto& packet=groups[occurrence.group];
                if(occurrence.instance+1!=packet.matrices.size())continue;
                if(packet.matrices.empty())continue;
                {CpuScope apply(&liveApplyTicks);result=packet.bindings->Apply();}if(FAILED(result))break;
                bool executed=false;
                if(packet.matrices.size()>1)
                {
                    CpuScope instanced(&liveInstanceTicks);
                    executed=renderer.TryDraw(device,eye,packet.type,packet.base,packet.minimum,packet.vertices,
                        packet.start,packet.primitives,adapt,draw,result,packet.matrices);
                    if(executed){++liveEmitted;++liveBatches;}
                }
                if(!executed)
                {
                    for(const auto& matrix:packet.matrices)
                    {
                        result=device->SetVertexShaderConstantF(0,matrix.world[0],4);
                        if(SUCCEEDED(result))result=device->SetVertexShaderConstantF(8,matrix.clip[0],4);
                        if(SUCCEEDED(result)){result=draw(packet.type,packet.base,packet.minimum,packet.vertices,packet.start,packet.primitives);++liveEmitted;}
                        if(FAILED(result))break;
                    }
                }
                if(FAILED(result) || renderer.restorationFailed)break;
            }
            HRESULT restored=S_OK;{CpuScope apply(&liveApplyTicks);restored=restore->Apply();}
            if(FAILED(restored))result=restored;
            if(FAILED(result) || renderer.restorationFailed){++failures;allocationFailed=true;}
            groups.clear();groupLookup.clear();sequence.clear();haveStencil=false;
            reading=false;return result;
        }
        template<class Log>
        void ReportMaterialRecipes(Log&& log)
        {
            // Diagnostic only: validate whether a cheap native material identity
            // predicts the complete observed non-transform state. Do not reuse
            // recipes in gameplay just because pointers or this frame match.
            struct Recipe {std::array<uintptr_t,14> key{};std::vector<size_t> states;size_t draws=0;};
            std::vector<Recipe> recipes;size_t missing=0,covered=0,splitRecipes=0,stableDraws=0;
            try
            {
                for(const auto& occurrence:sequence)
                {
                    const auto& group=groups[occurrence.group];
                    if(!group.batchable)continue;
                    if(!occurrence.material){++missing;continue;}
                    auto key=group.geometry;key[13]=occurrence.material; // Exclude ordering epoch only from this diagnostic key.
                    auto found=std::find_if(recipes.begin(),recipes.end(),[&](const auto& recipe){return recipe.key==key;});
                    if(found==recipes.end()){recipes.push_back({key,{},0});found=recipes.end()-1;}
                    ++found->draws;++covered;
                    if(std::none_of(found->states.begin(),found->states.end(),[&](size_t i){return groups[i].state==group.state;}))
                        found->states.push_back(occurrence.group);
                }
                std::array<size_t,6> changedKeys{},changedDraws{};
                for(const auto& recipe:recipes)
                {
                    splitRecipes+=recipe.states.size()>1;if(recipe.states.size()==1)stableDraws+=recipe.draws;
                    if(recipe.states.size()<2)continue;
                    const auto& first=groups[recipe.states.front()];std::array<bool,6> changed{};
                    for(size_t n=1;n<recipe.states.size();++n)
                    {
                        const auto& other=groups[recipe.states[n]];
                        if(first.stateEnds!=other.stateEnds || first.state.size()!=other.state.size())
                        {changed.fill(true);continue;}
                        size_t begin=0;
                        for(unsigned category=0;category<6;++category)
                        {
                            changed[category]|=!std::equal(first.state.begin()+begin,first.state.begin()+first.stateEnds[category],other.state.begin()+begin);
                            begin=first.stateEnds[category];
                        }
                    }
                    for(unsigned category=0;category<6;++category)if(changed[category]){++changedKeys[category];changedDraws[category]+=recipe.draws;}
                }
                constexpr const char* categories[]{"VS-float","PS-float","int-bool","render","texture-sampler","stream-view-target"};
                for(unsigned category=0;category<6;++category)
                {
                    char detail[180];sprintf_s(detail,"mesh-recipe-probe changed category=%s keys=%zu draws=%zu",categories[category],changedKeys[category],changedDraws[category]);log(detail);
                }
                char line[260];sprintf_s(line,"mesh-recipe-probe coveredDraws=%zu missingMaterialDraws=%zu materialGeometryKeys=%zu stateSplitKeys=%zu singleStateDraws=%zu",
                    covered,missing,recipes.size(),splitRecipes,stableDraws);log(line);
                log("mesh-recipe-probe native pointer identity is not a cache validity proof; frame-local complete-state comparison only");
            }
            catch(const std::bad_alloc&){log("mesh-recipe-probe unavailable: bounded diagnostic allocation failed");}
        }
        template<class Adapt,class Draw,class Log>
        void Compare(IDirect3DDevice9* device,Adapt&& adapt,Draw&& draw,Log&& log,bool orderedSubset=false)
        {
            reading=true;
            ReportMaterialRecipes(log);
            if(allocationFailed){log("mesh-packet-probe canceled: capture allocation failed; native draws preserved");return;}
            if(initialStencilFailed){char line[140];sprintf_s(line,"mesh-replay-probe canceled: initial stencil snapshot failed HRESULT=%08lX",static_cast<unsigned long>(initialStencilResult));log(line);return;}
            auto chosen=std::max_element(groups.begin(),groups.end(),[](const auto& a,const auto& b){return a.matrices.size()<b.matrices.size();});
            if(chosen==groups.end() || (!allIndexed && chosen->matrices.size()<2)){log("mesh-packet-probe: no repeated compatible packet in bounded capture");return;}
            auto& g=*chosen;
            if(queryEvents){log("mesh-replay-probe unavailable: query events in captured stream");return;}
            // Replaying several packets into a single scratch target is meaningful
            // only when they all originally wrote the same target/depth set.
            if(orderedSubset && std::any_of(groups.begin(),groups.end(),[&](const auto& other)
                {return !std::equal(g.state.end()-5,g.state.end(),other.state.end()-5);}))
            {log("mesh-order-probe unavailable: captured packets span different target/depth sets");return;}
            const auto check=[&](const char* operation,HRESULT result)
            {if(SUCCEEDED(result))return true;char line[140];sprintf_s(line,"mesh-packet-probe failure %s HRESULT=%08lX",operation,static_cast<unsigned long>(result));log(line);return false;};
            ComPtr<IDirect3DStateBlock9> saved;ComPtr<IDirect3DSurface9> savedTargets[4],savedDepth;
            if(FAILED(device->CreateStateBlock(D3DSBT_ALL,&saved))){log("mesh-packet-probe: save failed");return;}
            D3DSURFACE_DESC descriptions[5]{};ComPtr<IDirect3DSurface9> scratch[5],staging[5],depth;
            ComPtr<IDirect3DTexture9> depthTexture;ComPtr<IDirect3DPixelShader9> depthShader,stencilShader,restoreStencilShader;
            bool valid=SUCCEEDED(device->GetDepthStencilSurface(&savedDepth));
            for(unsigned i=0;i<4;++i)
            {
                valid=SUCCEEDED(device->GetRenderTarget(i,&savedTargets[i])) && valid;
                if(!savedTargets[i] || FAILED(savedTargets[i]->GetDesc(&descriptions[i])))valid=false;
            }
            if(!valid){log("mesh-packet-probe: original target capture unavailable");return;}
            for(unsigned i=0;i<4;++i)
            {
                const auto& d=descriptions[i];
                valid=check("scratch target",device->CreateRenderTarget(d.Width,d.Height,d.Format,D3DMULTISAMPLE_NONE,0,FALSE,&scratch[i],nullptr)) &&
                    check("staging target",device->CreateOffscreenPlainSurface(d.Width,d.Height,d.Format,D3DPOOL_SYSTEMMEM,&staging[i],nullptr));
                if(!valid)break;
            }
            if(valid && orderedSubset)
            {
                const auto width=descriptions[0].Width,height=descriptions[0].Height;
                ComPtr<IDirect3D9> root;D3DDEVICE_CREATION_PARAMETERS creation{};D3DDISPLAYMODE display{};
                valid=check("stencil copy D3D9",device->GetDirect3D(&root)) &&
                    check("stencil copy creation",device->GetCreationParameters(&creation));
                if(valid)valid=check("stencil copy display",root->GetAdapterDisplayMode(creation.AdapterOrdinal,&display)) &&
                    check("stencil copy float blend support",root->CheckDeviceFormat(creation.AdapterOrdinal,creation.DeviceType,display.Format,
                        D3DUSAGE_RENDERTARGET|D3DUSAGE_QUERY_POSTPIXELSHADER_BLENDING,D3DRTYPE_SURFACE,D3DFMT_R32F));
                // DXVK's D3D9 INTZ extension exposes normalized D24 depth to a
                // pixel shader. Fail closed if unavailable; never substitute a
                // different live depth format. Native INTZ is sampled read-only.
                if(valid)valid=check("scratch INTZ depth",device->CreateTexture(width,height,1,D3DUSAGE_DEPTHSTENCIL,
                    static_cast<D3DFORMAT>(MAKEFOURCC('I','N','T','Z')),D3DPOOL_DEFAULT,&depthTexture,nullptr));
                if(valid)valid=check("INTZ surface",depthTexture->GetSurfaceLevel(0,&depth));
                if(valid)valid=check("depth color",device->CreateRenderTarget(width,height,D3DFMT_R32F,D3DMULTISAMPLE_NONE,0,FALSE,&scratch[4],nullptr)) &&
                    check("depth staging",device->CreateOffscreenPlainSurface(width,height,D3DFMT_R32F,D3DPOOL_SYSTEMMEM,&staging[4],nullptr)) &&
                    check("depth description",scratch[4]->GetDesc(&descriptions[4]));
                const char assembly[]="ps_3_0\ndcl_texcoord0 v0.xy\ndcl_2d s0\ntexld r0, v0, s0\nmov oC0, r0.xxxx\n";
                ComPtr<ID3DXBuffer> code,errors;
                if(valid)valid=check("depth shader assembly",D3DXAssembleShader(assembly,sizeof(assembly)-1,nullptr,nullptr,0,&code,&errors)) &&
                    check("depth shader",device->CreatePixelShader(static_cast<const DWORD*>(code->GetBufferPointer()),&depthShader));
                const char stencilAssembly[]="ps_3_0\nmov oC0, c0\n";code.Reset();errors.Reset();
                if(valid)valid=check("stencil shader assembly",D3DXAssembleShader(stencilAssembly,sizeof(stencilAssembly)-1,nullptr,nullptr,0,&code,&errors)) &&
                    check("stencil shader",device->CreatePixelShader(static_cast<const DWORD*>(code->GetBufferPointer()),&stencilShader));
                if(valid && allIndexed)
                {
                    const char restoreAssembly[]="ps_3_0\ndcl_texcoord0 v0.xy\ndcl_2d s0\ntexld r0, v0, s0\nmul r0, r0.xxxx, c0.xxxx\nfrc r0, r0\nadd r0, r0, -c0.yyyy\ntexkill r0\nmov oC0, c0.zzzz\n";
                    code.Reset();errors.Reset();
                    valid=check("stencil restore shader assembly",D3DXAssembleShader(restoreAssembly,sizeof(restoreAssembly)-1,nullptr,nullptr,0,&code,&errors)) &&
                        check("stencil restore shader",device->CreatePixelShader(static_cast<const DWORD*>(code->GetBufferPointer()),&restoreStencilShader));
                }
            }
            else if(valid)valid=check("scratch depth",device->CreateDepthStencilSurface(descriptions[0].Width,descriptions[0].Height,D3DFMT_D24S8,D3DMULTISAMPLE_NONE,0,TRUE,&depth,nullptr));
            std::vector<unsigned char> images[2][6],nativeImages[4],nativeStencil,nativeDepth;VRRigidInstance::Renderer renderer;
            const bool nativeComparable=allIndexed && observed==seen && sequence.size()==observed && !otherDraws && !dropped && !failures;
            if(valid && nativeComparable)
                for(unsigned target=0;target<4 && valid;++target)
                    valid=check("actual native target readback",ReadPixels(device,savedTargets[target].Get(),staging[target].Get(),descriptions[target],nativeImages[target]));
            if(valid && nativeComparable && orderedSubset)
                valid=ReadStencil(device,savedDepth.Get(),scratch[4].Get(),staging[4].Get(),descriptions[4],stencilShader.Get(),nativeStencil,check);
            bool nativeDepthComparable=false;
            if(valid && nativeComparable && orderedSubset)
            {
                D3DSURFACE_DESC nativeDescription{};ComPtr<IDirect3DTexture9> nativeTexture;
                // GetContainer retains the existing texture. Unsupported live
                // formats leave this gate explicitly unverified, never replaced.
                if(SUCCEEDED(savedDepth->GetDesc(&nativeDescription)) &&
                    nativeDescription.Format==static_cast<D3DFORMAT>(MAKEFOURCC('I','N','T','Z')) &&
                    nativeDescription.Width==descriptions[4].Width && nativeDescription.Height==descriptions[4].Height &&
                    SUCCEEDED(savedDepth->GetContainer(__uuidof(IDirect3DTexture9),reinterpret_cast<void**>(nativeTexture.GetAddressOf()))))
                {
                    valid=ReadDepth(device,nativeTexture.Get(),scratch[4].Get(),staging[4].Get(),
                        descriptions[4],depthShader.Get(),nativeDepth,check);
                    nativeDepthComparable=valid;
                }
            }
            ComPtr<IDirect3DQuery9> gpuStart,gpuEnd,gpuFrequency,gpuDisjoint;
            if(valid && benchmark)
                valid=check("benchmark timestamp start",device->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&gpuStart)) &&
                    check("benchmark timestamp end",device->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&gpuEnd)) &&
                    check("benchmark timestamp frequency",device->CreateQuery(D3DQUERYTYPE_TIMESTAMPFREQ,&gpuFrequency)) &&
                    check("benchmark disjoint",device->CreateQuery(D3DQUERYTYPE_TIMESTAMPDISJOINT,&gpuDisjoint));
            const auto awaitQuery=[&](IDirect3DQuery9* query,void* data,DWORD bytes)
            {
                const auto deadline=GetTickCount64()+5000;HRESULT result=S_FALSE;
                while((result=query->GetData(data,bytes,D3DGETDATA_FLUSH))==S_FALSE && GetTickCount64()<deadline)Sleep(0);
                return result==S_OK;
            };
            LARGE_INTEGER counterFrequency{};QueryPerformanceFrequency(&counterFrequency);
            double cpuMs[2][4]{},bindMs[2][4]{},gpuMs[2][4]{};unsigned timingCounts[2]{};
            size_t referenceDraws=0,batchDraws=0;bool countParity=true;

            try
            {
            for(unsigned pass=0;pass<(benchmark ? 10u:2u) && valid;++pass)
            {
                const unsigned mode=pass<2 ? pass:((pass-2)%4==0 || (pass-2)%4==3 ? 0u:1u);
                countParity=pass<2;
                valid=check("packet state",g.bindings->Apply());
                for(unsigned i=0;i<4;++i)valid=check("bind scratch",device->SetRenderTarget(i,scratch[i].Get())) && valid;
                valid=check("bind depth",device->SetDepthStencilSurface(depth.Get())) && valid;
                valid=check("clear",device->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER|D3DCLEAR_STENCIL,0,1,0)) && valid;
                if(!valid)break;
                if(allIndexed && orderedSubset)
                {
                    // Preserve the stencil contents present immediately before
                    // the first native indexed draw. The engine need not clear
                    // hardware stencil along with G-buffer colors/depth.
                    std::vector<unsigned char> unused;
                    valid=ReadStencil(device,depth.Get(),scratch[4].Get(),nullptr,descriptions[4],stencilShader.Get(),unused,check,false);
                    valid=check("stencil restore input",device->SetTexture(0,initialStencil.Get())) &&
                        check("stencil restore PS",device->SetPixelShader(restoreStencilShader.Get())) &&
                        check("stencil restore blend",device->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE)) &&
                        check("stencil restore no color writes",device->SetRenderState(D3DRS_COLORWRITEENABLE,0)) &&
                        check("stencil restore always",device->SetRenderState(D3DRS_STENCILFUNC,D3DCMP_ALWAYS)) &&
                        check("stencil restore replace",device->SetRenderState(D3DRS_STENCILPASS,D3DSTENCILOP_REPLACE)) && valid;
                    for(const auto s:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER,D3DSAMP_MIPFILTER})
                        valid=check("stencil restore filter",device->SetSamplerState(0,s,s==D3DSAMP_MIPFILTER ? D3DTEXF_NONE:D3DTEXF_POINT)) && valid;
                    for(const auto s:{D3DSAMP_ADDRESSU,D3DSAMP_ADDRESSV})valid=check("stencil restore clamp",device->SetSamplerState(0,s,D3DTADDRESS_CLAMP)) && valid;
                    valid=check("stencil restore sRGB",device->SetSamplerState(0,D3DSAMP_SRGBTEXTURE,0)) && valid;
                    struct Vertex {float x,y,z,rhw,u,v;};
                    const auto& d=descriptions[4];
                    const Vertex quad[]{{-.5f,-.5f,0,1,0,0},{static_cast<float>(d.Width)-.5f,-.5f,0,1,1,0},
                        {-.5f,static_cast<float>(d.Height)-.5f,0,1,0,1},{static_cast<float>(d.Width)-.5f,static_cast<float>(d.Height)-.5f,0,1,1,1}};
                    for(unsigned bit=0;bit<8 && valid;++bit)
                    {
                        const DWORD mask=1u<<bit;const float constants[4]{1.f/(2*mask),.5f,0,0};
                        valid=check("stencil restore write mask",device->SetRenderState(D3DRS_STENCILWRITEMASK,mask)) &&
                            check("stencil restore reference",device->SetRenderState(D3DRS_STENCILREF,mask)) &&
                            check("stencil restore bit selector",device->SetPixelShaderConstantF(0,constants,1));
                        if(valid)valid=check("stencil restore draw",device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(Vertex)));
                    }
                    if(!valid)break;
                }
                LARGE_INTEGER begin{},end{};long long bindTicks=0;
                if(pass>=2)
                {
                    valid=check("benchmark disjoint begin",gpuDisjoint->Issue(D3DISSUE_BEGIN)) &&
                        check("benchmark frequency",gpuFrequency->Issue(D3DISSUE_END)) &&
                        check("benchmark start",gpuStart->Issue(D3DISSUE_END));
                    if(!valid)break;
                    QueryPerformanceCounter(&begin);
                }
                const auto bindPacket=[&](const Group& packet)
                {
                    LARGE_INTEGER before{},after{};if(pass>=2)QueryPerformanceCounter(&before);
                    bool ok=check("replay packet state",packet.bindings->Apply());
                    for(unsigned i=0;i<4;++i)ok=check("replay scratch target",device->SetRenderTarget(i,scratch[i].Get())) && ok;
                    ok=check("replay scratch depth",device->SetDepthStencilSurface(depth.Get())) && ok;
                    if(pass>=2){QueryPerformanceCounter(&after);bindTicks+=after.QuadPart-before.QuadPart;}
                    return ok;
                };
                const auto reference=[&](const Group& packet,const VRRigidInstance::Matrices& matrices)
                {
                    if(countParity && !mode)++referenceDraws;
                    return check("world matrix",device->SetVertexShaderConstantF(0,matrices.world[0],4)) &&
                        check("clip matrix",device->SetVertexShaderConstantF(8,matrices.clip[0],4)) &&
                        check("reference draw",draw(packet.type,packet.base,packet.minimum,packet.vertices,packet.start,packet.primitives));
                };
                const auto batch=[&](const Group& packet)
                {
                    if(countParity)++batchDraws;HRESULT result=S_OK;
                    if(!packet.batchable || packet.matrices.size()==1)return reference(packet,packet.matrices[0]);
                    const bool ok=renderer.TryDraw(device,0,packet.type,packet.base,packet.minimum,packet.vertices,
                        packet.start,packet.primitives,adapt,draw,result,packet.matrices,pass>=2) && SUCCEEDED(result) && !renderer.restorationFailed;
                    if(!ok)log("mesh-packet-probe failure batch rejected or draw/restoration failed");
                    return ok;
                };
                if(orderedSubset)
                {
                    // Preserve reference order. The alternative emits a group's
                    // instances at its LAST occurrence, retaining instance order.
                    // This intentionally tests reordering, never changes gameplay.
                    for(const auto& occurrence:sequence)
                    {
                        const auto& packet=groups[occurrence.group];
                        if((mode || pass>=2) && occurrence.instance+1!=packet.matrices.size())continue;
                        valid=bindPacket(packet);
                        if(valid && pass>=2 && !mode)
                        {
                            // Same group order and one shared binding in BOTH
                            // benchmark modes: isolate ordinary vs instanced
                            // submission, not expensive StateBlock duplication.
                            for(const auto& matrices:packet.matrices)
                            {valid=reference(packet,matrices);if(!valid)break;}
                        }
                        else if(valid)valid=mode ? batch(packet):reference(packet,packet.matrices[occurrence.instance]);
                        if(!valid)break;
                    }
                }
                else if(!mode)
                    for(const auto& matrices:g.matrices){valid=reference(g,matrices);if(!valid)break;}
                else valid=batch(g);
                if(pass>=2)
                {
                    QueryPerformanceCounter(&end);
                    valid=check("benchmark end",gpuEnd->Issue(D3DISSUE_END)) &&
                        check("benchmark disjoint end",gpuDisjoint->Issue(D3DISSUE_END)) && valid;
                    UINT64 start=0,finish=0,frequency=0;BOOL disjoint=TRUE;
                    valid=valid && awaitQuery(gpuEnd.Get(),&finish,sizeof(finish)) &&
                        awaitQuery(gpuStart.Get(),&start,sizeof(start)) &&
                        awaitQuery(gpuFrequency.Get(),&frequency,sizeof(frequency)) &&
                        awaitQuery(gpuDisjoint.Get(),&disjoint,sizeof(disjoint)) && !disjoint && frequency && finish>=start;
                    if(!valid){log("mesh-benchmark-probe canceled: invalid/disjoint/timed-out GPU sample");break;}
                    const auto n=timingCounts[mode]++;
                    cpuMs[mode][n]=(end.QuadPart-begin.QuadPart)*1000./counterFrequency.QuadPart;
                    bindMs[mode][n]=bindTicks*1000./counterFrequency.QuadPart;
                    gpuMs[mode][n]=(finish-start)*1000./frequency;
                    continue; // No staging/readback in timed iterations.
                }
                for(unsigned i=0;i<(orderedSubset ? 5u:4u) && valid;++i)
                {
                    if(i==4)
                    {
                        valid=ReadDepth(device,depthTexture.Get(),scratch[4].Get(),staging[4].Get(),
                            descriptions[4],depthShader.Get(),images[mode][4],check);
                        if(!valid)break;
                        continue;
                    }
                    valid=check("scratch target readback",ReadPixels(device,scratch[i].Get(),staging[i].Get(),descriptions[i],images[mode][i]));
                }
                if(valid && orderedSubset)
                    valid=ReadStencil(device,depth.Get(),scratch[4].Get(),staging[4].Get(),descriptions[4],stencilShader.Get(),images[mode][5],check);
            }
            }
            catch(const std::bad_alloc&){valid=false;log("mesh-packet-probe scratch replay allocation failed; restoring native bindings");}
            bool restored=true;
            for(unsigned i=0;i<4;++i)restored=SUCCEEDED(device->SetRenderTarget(i,savedTargets[i].Get())) && restored;
            restored=SUCCEEDED(device->SetDepthStencilSurface(savedDepth.Get())) && restored;
            restored=SUCCEEDED(saved->Apply()) && restored;
            size_t differences=0,nonzero=0,depthDifferences=0,depthPixels=0,stencilDifferences=0,stencilPixels=0;
            if(valid)for(unsigned target=0;target<4;++target)
                for(size_t i=0;i<images[0][target].size();++i)
                {differences+=images[0][target][i]!=images[1][target][i];nonzero+=images[0][target][i]!=0;}
            if(valid && orderedSubset)
                for(size_t i=0;i<images[0][4].size();i+=sizeof(float))
                {
                    depthDifferences+=std::memcmp(images[0][4].data()+i,images[1][4].data()+i,sizeof(float))!=0;
                    float value=1;std::memcpy(&value,images[0][4].data()+i,sizeof(value));depthPixels+=value<1;
                    stencilDifferences+=std::memcmp(images[0][5].data()+i,images[1][5].data()+i,sizeof(float))!=0;
                    std::memcpy(&value,images[0][5].data()+i,sizeof(value));stencilPixels+=value!=0;
                }
            char line[300];
            if(benchmark)
            {
                bool nativeMatches=nativeDepthComparable && nativeDepth==images[0][4] && nativeStencil==images[0][5];
                for(unsigned target=0;target<4;++target)nativeMatches=nativeMatches && nativeImages[target]==images[0][target];
                const bool timingValid=valid && restored && nativeComparable && nativeMatches &&
                    !differences && !depthDifferences && !stencilDifferences && timingCounts[0]==4 && timingCounts[1]==4;
                for(unsigned mode=0;mode<2;++mode)for(unsigned n=0;n<timingCounts[mode];++n)
                {
                    sprintf_s(line,"mesh-benchmark-probe mode=%s sample=%u cpuSubmitMs=%.6f stateApplyMs=%.6f emitterMs=%.6f gpuMs=%.6f valid=%u",
                        mode ? "grouped":"ordinary",n,cpuMs[mode][n],bindMs[mode][n],cpuMs[mode][n]-bindMs[mode][n],gpuMs[mode][n],timingValid);log(line);
                }
                log("mesh-benchmark-probe warmed same captured eye0 GBuffer; same grouped order/shared bindings in both modes; excludes capture/preparation/readback/clear/stencil initialization; includes state-block apply/scratch target rebinding; grouped emitter reuses warm packet validation and submits singleton groups natively; ABBA pairs with GPU drain; not gameplay FPS");
            }
            sprintf_s(line,"mesh-packet-probe seen=%u eligible=%u groups=%zu dropped=%u failures=%u selectedInstances=%zu valid=%u restored=%u differingBytes=%zu nonzeroReferenceBytes=%zu",
                seen,eligible,groups.size(),dropped,failures,g.matrices.size(),valid,restored,differences,nonzero);log(line);
            log("mesh-packet-probe isolated native-geometry comparison only; no scene reordering or performance claim");
            sprintf_s(line,"mesh-packet-probe coverage observedDraws=%u inspectedDraws=%u capturedDraws=%zu drawLimit=%u groupLimit=%u timeLimitMs=%u",
                observed,seen,sequence.size(),drawLimit,groupLimit,timeLimitMs);log(line);
            if(orderedSubset)
            {
                sprintf_s(line,"mesh-order-probe capturedSubset=%zu referenceDraws=%zu groupedDraws=%zu valid=%u restored=%u differingColorBytes=%zu nonzeroReferenceBytes=%zu",
                    sequence.size(),referenceDraws,batchDraws,valid,restored,differences,nonzero);log(line);
                sprintf_s(line,"mesh-order-probe differingDepthPixels=%zu referenceDepthPixels=%zu",depthDifferences,depthPixels);log(line);
                sprintf_s(line,"mesh-order-probe differingStencilPixels=%zu referenceStencilPixels=%zu",stencilDifferences,stencilPixels);log(line);
                log("mesh-order-probe partial captured subset only: skipped draws and query/texture side effects are NOT validated; never enable gameplay reordering from scratch equality alone");
            }
            if(allIndexed)
            {
                size_t nativeDifferences=0;
                if(valid && nativeComparable)
                    for(unsigned target=0;target<4;++target)
                        for(size_t i=0;i<nativeImages[target].size();++i)nativeDifferences+=nativeImages[target][i]!=images[0][target][i];
                sprintf_s(line,"mesh-replay-probe nativeCompared=%u nativeColorDifferences=%zu otherDraws=%u queryEvents=%u orderingBarriers=%u valid=%u restored=%u",
                    valid && nativeComparable,nativeDifferences,otherDraws,queryEvents,barrier,valid,restored);log(line);
                size_t nativeStencilDifferences=0;
                if(valid && nativeComparable && orderedSubset)
                    for(size_t i=0;i<nativeStencil.size();i+=sizeof(float))
                        nativeStencilDifferences+=std::memcmp(nativeStencil.data()+i,images[0][5].data()+i,sizeof(float))!=0;
                sprintf_s(line,"mesh-replay-probe nativeStencilCompared=%u nativeStencilDifferences=%zu",
                    valid && nativeComparable && orderedSubset,nativeStencilDifferences);log(line);
                size_t nativeDepthDifferences=0;
                if(valid && nativeDepthComparable)
                    for(size_t i=0;i<nativeDepth.size();i+=sizeof(float))
                        nativeDepthDifferences+=std::memcmp(nativeDepth.data()+i,images[0][4].data()+i,sizeof(float))!=0;
                sprintf_s(line,"mesh-replay-probe nativeDepthCompared=%u nativeDepthDifferences=%zu",
                    valid && nativeDepthComparable,nativeDepthDifferences);log(line);
                log("mesh-replay-probe native parity is frame-local; no query/texture lifetime proof or production speedup");
                sprintf_s(line,"mesh-replay-probe stencilSignatureBoundaries=%u",stencilBoundaries);log(line);
            }
        }
    };
}
