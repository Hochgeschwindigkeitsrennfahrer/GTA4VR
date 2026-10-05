#pragma once
#include <d3d9.h>
#include <wrl/client.h>
#include <array>
#include <cmath>
#include <cstring>
#include <span>
#include <vector>

namespace VRRigidInstance
{
    using Microsoft::WRL::ComPtr;
    struct Matrices {float world[4][4],clip[4][4];};
    static_assert(sizeof(Matrices)==128);
    // Explicit same-state packet emitter. Native-order compatibility tests use
    // one instance; the bounded live packet test may pass multiple instances.
    // This renderer itself does no sorting, callback skipping or geometry readback.
    class Renderer
    {
        struct Variant
        {
            ComPtr<IDirect3DVertexShader9> original,adapted;
            ComPtr<IDirect3DVertexDeclaration9> originalDeclaration,declaration;
        };
        static constexpr unsigned capacity=8192;
        ComPtr<IDirect3DVertexBuffer9> instances;
        std::vector<Variant> variants;
        unsigned cursor=0;
    public:
        unsigned long long draws[2]{},instanceCounts[2]{},fallbacks[2]{},failures=0;
        bool restorationFailed=false;
        template<class Adapt,class Draw>
        bool TryDraw(IDirect3DDevice9* device,unsigned eye,D3DPRIMITIVETYPE type,
            INT base,UINT minimum,UINT vertices,UINT start,UINT primitives,
            Adapt&& adapt,Draw&& draw,HRESULT& result,std::span<const Matrices> prepared={},bool preparedVerified=false)
        {
            if(eye>1 || type!=D3DPT_TRIANGLELIST || !primitives || !vertices || restorationFailed)
                return false;
            // Nonempty prepared data is for an explicit same-state packet only.
            // The caller must preserve material, geometry, query and draw order;
            // this renderer never searches or sorts native scene draws.
            if(prepared.size()>capacity)return false;
            ComPtr<IDirect3DVertexShader9> original;
            ComPtr<IDirect3DVertexDeclaration9> originalDeclaration;
            if(FAILED(device->GetVertexShader(&original)) || !original ||
                FAILED(device->GetVertexDeclaration(&originalDeclaration)) || !originalDeclaration)
                return false;
            // Stream1 is reserved for matrices. Reject other native streams and
            // existing instancing rather than guessing their semantics.
            // Only the bounded benchmark may reuse validation of the exact
            // immutable prepared packet after its warm parity replay. Gameplay
            // callers keep the default and verify live stream state each draw.
            if(!preparedVerified || prepared.empty())
            for(unsigned stream=0;stream<16;++stream)
            {
                ComPtr<IDirect3DVertexBuffer9> buffer;UINT offset=0,stride=0,frequency=0;
                if(FAILED(device->GetStreamSource(stream,&buffer,&offset,&stride)) ||
                    FAILED(device->GetStreamSourceFreq(stream,&frequency)) || frequency!=1 ||
                    (stream==0 ? !buffer : static_cast<bool>(buffer)))return false;
            }
            Variant* selected=nullptr;
            for(auto& variant:variants)
                if(variant.original.Get()==original.Get() &&
                    variant.originalDeclaration.Get()==originalDeclaration.Get())
                {selected=&variant;break;}
            if(!selected)
            {
                if(variants.size()>=64)return false;
                variants.emplace_back();selected=&variants.back();
                selected->original=original;selected->originalDeclaration=originalDeclaration;
                D3DVERTEXELEMENT9 elements[MAXD3DDECLLENGTH+1]{};UINT count=std::size(elements);
                if(FAILED(originalDeclaration->GetDeclaration(elements,&count)) || count<5 || count>6)
                    return false;
                const bool tangent=count==6;
                // Match the authored parity proof's exact native declaration.
                constexpr BYTE usages[]{D3DDECLUSAGE_POSITION,D3DDECLUSAGE_NORMAL,
                    D3DDECLUSAGE_COLOR,D3DDECLUSAGE_TEXCOORD,D3DDECLUSAGE_TANGENT};
                constexpr BYTE types[]{D3DDECLTYPE_FLOAT3,D3DDECLTYPE_FLOAT3,
                    D3DDECLTYPE_D3DCOLOR,D3DDECLTYPE_FLOAT2,D3DDECLTYPE_FLOAT4};
                constexpr WORD offsets[]{0,12,24,28,36};
                for(unsigned i=0;i<count-1;++i)
                    if(elements[i].Stream!=0 || elements[i].Offset!=offsets[i] ||
                        elements[i].Type!=types[i] || elements[i].Method!=D3DDECLMETHOD_DEFAULT ||
                        elements[i].Usage!=usages[i] || elements[i].UsageIndex!=0)return false;
                if(elements[count-1].Stream!=0xff || elements[count-1].Type!=D3DDECLTYPE_UNUSED)
                    return false;
                UINT bytes=0;
                if(FAILED(original->GetFunction(nullptr,&bytes)) || !bytes || bytes>65536 || bytes%4)
                    return false;
                std::vector<DWORD> code(bytes/4);
                if(FAILED(original->GetFunction(code.data(),&bytes)) ||
                    !adapt(code,tangent,selected->adapted))return false;
                for(unsigned i=0;i<8;++i)
                    elements[count-1+i]={1,static_cast<WORD>(i*16),D3DDECLTYPE_FLOAT4,
                        D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TEXCOORD,static_cast<BYTE>(i+1)};
                elements[count+7]=D3DDECL_END();
                if(FAILED(device->CreateVertexDeclaration(elements,&selected->declaration)))
                    return false;
            }
            if(!selected->adapted || !selected->declaration)return false;
            Matrices matrices{};
            if(prepared.empty())
            {
                if(FAILED(device->GetVertexShaderConstantF(0,matrices.world[0],4)) ||
                    FAILED(device->GetVertexShaderConstantF(8,matrices.clip[0],4)))return false;
                prepared={&matrices,1};
            }
            if(!preparedVerified)
            for(const auto& instance:prepared)
            {
                for(const auto& row:instance.world)for(float value:row)if(!std::isfinite(value))return false;
                for(const auto& row:instance.clip)for(float value:row)if(!std::isfinite(value))return false;
            }
            if(!instances && FAILED(device->CreateVertexBuffer(capacity*sizeof(Matrices),
                D3DUSAGE_DYNAMIC|D3DUSAGE_WRITEONLY,0,D3DPOOL_DEFAULT,&instances,nullptr)))
                return false;
            const auto count=static_cast<UINT>(prepared.size());
            if(cursor>capacity-count)cursor=0;
            const auto offset=cursor*sizeof(Matrices);void* mapped=nullptr;
            if(FAILED(instances->Lock(offset,count*sizeof(Matrices),&mapped,
                cursor ? D3DLOCK_NOOVERWRITE:D3DLOCK_DISCARD)))return false;
            std::memcpy(mapped,prepared.data(),count*sizeof(Matrices));cursor+=count;
            if(FAILED(instances->Unlock()))return false;
            const bool configured=SUCCEEDED(device->SetVertexDeclaration(selected->declaration.Get())) &&
                SUCCEEDED(device->SetVertexShader(selected->adapted.Get())) &&
                SUCCEEDED(device->SetStreamSource(1,instances.Get(),offset,sizeof(Matrices))) &&
                SUCCEEDED(device->SetStreamSourceFreq(0,static_cast<UINT>(D3DSTREAMSOURCE_INDEXEDDATA)|count)) &&
                SUCCEEDED(device->SetStreamSourceFreq(1,static_cast<UINT>(D3DSTREAMSOURCE_INSTANCEDATA)|1u));
            bool executed=false;
            if(configured)
            {result=draw(type,base,minimum,vertices,start,primitives);executed=true;++draws[eye];instanceCounts[eye]+=count;if(FAILED(result))++failures;}
            // Attempt every restoration, including after partial configuration.
            const auto f0=device->SetStreamSourceFreq(0,1);
            const auto f1=device->SetStreamSourceFreq(1,1);
            const auto stream=device->SetStreamSource(1,nullptr,0,0);
            const auto shader=device->SetVertexShader(original.Get());
            const auto declaration=device->SetVertexDeclaration(originalDeclaration.Get());
            restorationFailed=FAILED(f0)||FAILED(f1)||FAILED(stream)||FAILED(shader)||FAILED(declaration);
            if(restorationFailed)++failures;
            // A failed instanced draw is never followed by a duplicate native draw.
            return executed;
        }
    };
}
