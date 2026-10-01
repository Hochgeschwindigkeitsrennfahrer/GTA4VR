module;
#include <common.hxx>
#include <wrl/client.h>
#include <d3dx9shader.h>
#include "includes/vrmath.hpp"
#include <atomic>
#include <cstring>
#include <cstdio>
#include <cwchar>
#include <vector>
#include <string>
#include <memory>
#include <regex>
export module vrstereo;
import common;
import comvars;
import settings;
import openxrvr;
import vrcamera;
import d3dx9_43;

export namespace VRStereo
{
    void Install();
    void Shutdown(); // Render callback thread, never DllMain.
    bool RestartExperiment();
    std::string Status();
}

namespace
{
    using Microsoft::WRL::ComPtr;
    struct Target
    {
        std::string name;
        ComPtr<IDirect3DTexture9> original;
        ComPtr<IDirect3DSurface9> originalSurface;
        ComPtr<IDirect3DTexture9> texture[2];
        ComPtr<IDirect3DSurface9> surface[2];
        std::vector<ComPtr<IDirect3DSurface9>> originalMips;
        std::vector<ComPtr<IDirect3DSurface9>> eyeMips[2];
    };
    struct Resources
    {
        std::vector<Target> targets;
        ComPtr<IDirect3DSurface9> originalBack;
        ComPtr<IDirect3DTexture9> finalTexture[2];
        ComPtr<IDirect3DSurface9> finalSurface[2];
        HANDLE shared[2]{};
        std::vector<ComPtr<IDirect3DVertexShader9>> observedShaders;
        ComPtr<IDirect3DVertexShader9> symmetricRayShader,asymmetricRayShader;
        std::vector<ComPtr<IDirect3DPixelShader9>> observedPixelShaders,cascadeShaders;
    };
    // Explicit render-thread ownership; process detach must not invoke driver
    // cleanup under the loader lock. The bridge/reset calls Shutdown instead.
    Resources* resources=nullptr;
    std::atomic<bool> ready{false};
    std::atomic<bool> enabled{false};
    unsigned maxFrames=1;
    std::atomic<unsigned> stereoFrames{0};
    unsigned gameFrame=0,preparedFrame=0;
    long long viewTime=0;
    OpenXRVR::EyeView xrViews[2]{};
    rage::grcViewport eyeViewports[2]{};
    std::array<VRMath::Vector,6> visibilityPlanes{};
    void* scenePhase=nullptr;
    void* lightingPhase=nullptr;
    void* drawPhase=nullptr;
    void* worldParent=nullptr;
    bool setupThisFrame=false,prepared=false;
    int buildingEye=-1,renderEye=-1;
    unsigned renderStage=0;
    unsigned completedFrame=0,completedEyes=0;
    bool captured=false;
    bool submit=false;
    bool mirror=true;
    unsigned publishedFrame=0;
    OpenXRVR::EyeView completedViews[2]{};
    long long completedPoseTime=0;
    std::atomic<DWORD> routingThread{0};
    std::atomic<bool> restartRequested{false},captureNextPair{false};
    SafetyHookInline sceneSetup{},drawSetup{},renderFrame{},buildFrame{};
    SafetyHookInline setTarget{},setDepth{},setTexture{},stretch{};
    SafetyHookInline setVertexShader{};
    SafetyHookInline setVertexConstants{};
    SafetyHookInline drawPrimitive{};
    SafetyHookInline setPixelShader{},setPixelConstants{};
    bool cascadeShaderBound=false;
    float originalCascadeForward[4]{},cascadeForward[4]{},gameShadowForward[4]{};
    unsigned lightingProbeMask=0;
    float expectedCamera[16]{};
    bool rayShaderBound=false;
    float originalRayConstants[4]{},eyeRayConstants[4]{};

    void Log(const char* message)
    {
        wchar_t path[MAX_PATH]{};
        if(!GetModuleFileNameW(nullptr,path,MAX_PATH)) return;
        auto* name=wcsrchr(path,L'\\');
        if(!name) return;
        name[1]=0;
        if(wcscat_s(path,L"plugins\\GTAIV.VR.stereo.log")) return;
        FILE* file=nullptr;
        if(!_wfopen_s(&file,path,L"a") && file) {fprintf(file,"%s\n",message); fclose(file);}
    }

    bool Check(const char* operation,HRESULT result)
    {
        if(SUCCEEDED(result)) return true;
        char line[160];
        sprintf_s(line,"%s failed: 0x%08lX",operation,static_cast<unsigned long>(result));
        Log(line);
        return false;
    }

    bool EyeTarget(const std::string& name)
    {
        return name.starts_with("_DEFERRED_GBUFFER_") || name=="_STENCIL_BUFFER_" ||
            name=="_BACK_ZBUFFER_" || name.starts_with("FullScreen") ||
            name.starts_with("Quarter Screen") || name.starts_with("Blur Screen") ||
            name.starts_with("Lum ") || name.starts_with("Adapted Lum") || name=="Current Lum" ||
            name=="PreAlphaDepthCopy" || name.starts_with("AO") || name=="edgesTex" || name=="blendTex";
    }

    IDirect3DSurface9* MapSurface(IDirect3DSurface9* input,int eye)
    {
        if(!input || !resources) return input;
        if(input==resources->originalBack.Get() || input==resources->finalSurface[0].Get() ||
            input==resources->finalSurface[1].Get())
            return eye<0 ? resources->originalBack.Get() : resources->finalSurface[eye].Get();
        for(auto& target:resources->targets)
        {
            if(input==target.originalSurface.Get() || input==target.surface[0].Get() || input==target.surface[1].Get())
                return eye<0 ? target.originalSurface.Get() : target.surface[eye].Get();
            for(size_t level=0;level<target.originalMips.size();++level)
                if(input==target.originalMips[level].Get() || input==target.eyeMips[0][level].Get() ||
                    input==target.eyeMips[1][level].Get())
                    return eye<0 ? target.originalMips[level].Get() : target.eyeMips[eye][level].Get();
        }
        return input;
    }

    IDirect3DBaseTexture9* MapTexture(IDirect3DBaseTexture9* input,int eye)
    {
        if(!input || !resources) return input;
        for(auto& target:resources->targets)
            if(input==target.original.Get() || input==target.texture[0].Get() || input==target.texture[1].Get())
                return eye<0 ? target.original.Get() : target.texture[eye].Get();
        return input;
    }

    HRESULT WINAPI SetTarget(IDirect3DDevice9* self,DWORD index,IDirect3DSurface9* surface)
    {return setTarget.unsafe_stdcall<HRESULT>(self,index,GetCurrentThreadId()==routingThread ? MapSurface(surface,renderEye) : surface);}
    HRESULT WINAPI SetDepth(IDirect3DDevice9* self,IDirect3DSurface9* surface)
    {return setDepth.unsafe_stdcall<HRESULT>(self,GetCurrentThreadId()==routingThread ? MapSurface(surface,renderEye) : surface);}
    HRESULT WINAPI SetTexture(IDirect3DDevice9* self,DWORD stage,IDirect3DBaseTexture9* texture)
    {return setTexture.unsafe_stdcall<HRESULT>(self,stage,GetCurrentThreadId()==routingThread ? MapTexture(texture,renderEye) : texture);}
    HRESULT WINAPI Stretch(IDirect3DDevice9* self,IDirect3DSurface9* source,const RECT* sourceRect,
        IDirect3DSurface9* destination,const RECT* destinationRect,D3DTEXTUREFILTERTYPE filter)
    {const bool route=GetCurrentThreadId()==routingThread;
     return stretch.unsafe_stdcall<HRESULT>(self,route ? MapSurface(source,renderEye) : source,sourceRect,
        route ? MapSurface(destination,renderEye) : destination,destinationRect,filter);}

    HRESULT WINAPI SetVertexShader(IDirect3DDevice9* self,IDirect3DVertexShader9* shader)
    {
        if(GetCurrentThreadId()!=routingThread || !resources)
            return setVertexShader.unsafe_stdcall<HRESULT>(self,shader);
        // Restore the engine's register before another lighting shader uses
        // c209 for a different purpose (for example local-light position).
        if(rayShaderBound)
        {
            setVertexConstants.unsafe_stdcall<HRESULT>(self,209,originalRayConstants,1);
            rayShaderBound=false;
        }
        if(resources && shader==resources->asymmetricRayShader.Get() && shader)
            shader=resources->symmetricRayShader.Get();
        if(shader && resources && GetCurrentThreadId()==routingThread && renderEye>=0 && renderStage==2 &&
            resources->observedShaders.size()<8 && std::none_of(resources->observedShaders.begin(),
                resources->observedShaders.end(),[shader](const auto& value){return value.Get()==shader;}))
        {
            resources->observedShaders.emplace_back(shader);
            UINT bytes=0;
            if(SUCCEEDED(shader->GetFunction(nullptr,&bytes)) && bytes && bytes<=65536)
            {
                std::vector<DWORD> code((bytes+3)/4);
                ComPtr<ID3DXBuffer> assembly;
                if(SUCCEEDED(shader->GetFunction(code.data(),&bytes)) &&
                    SUCCEEDED(D3DXDisassembleShader(code.data(),FALSE,nullptr,&assembly)))
                {
                    char line[120];
                    sprintf_s(line,"lighting vertex shader=%p frame=%u eye=%d",shader,completedFrame,renderEye);
                    Log(line);
                    // Bounded local diagnostic; shader output stays outside Git.
                    std::string text(static_cast<const char*>(assembly->GetBufferPointer()),
                        std::min<size_t>(assembly->GetBufferSize(),16000));
                    Log(text.c_str());
                    constexpr char symmetric[]="mul r0.xy, r0, c209";
                    const auto at=text.find(symmetric);
                    // Verified fullscreen deferred-lighting VS0. Keep its
                    // screen/UV transform and all other instructions intact.
                    if(at!=std::string::npos && text.find(symmetric,at+1)==std::string::npos &&
                        text.find("mad o1.xy, c208.zwzw, -r1.x, r0")!=std::string::npos &&
                        text.find("mad r0.xyz, -r0.x, c12, r0.yzww")!=std::string::npos &&
                        text.find("add o2.xyz, r0, c14")!=std::string::npos &&
                        text.find("approximately 11 instruction slots")!=std::string::npos &&
                        !resources->asymmetricRayShader)
                    {
                        text.replace(at,sizeof(symmetric)-1,"mad r0.xy, r0, c209.xyxy, c209.zwzw");
                        ComPtr<ID3DXBuffer> patched,errors;
                        if(Check("asymmetric lighting ray assembly",D3DXAssembleShader(text.data(),
                            static_cast<UINT>(text.size()),nullptr,nullptr,0,&patched,&errors)) &&
                            Check("asymmetric lighting ray shader",self->CreateVertexShader(
                                static_cast<DWORD*>(patched->GetBufferPointer()),&resources->asymmetricRayShader)))
                        {resources->symmetricRayShader=shader; Log("Verified deferred ray shader corrected for asymmetric eye projection");}
                    }
                }
            }
        }
        if(resources && renderEye>=0 && renderStage==2 && GetCurrentThreadId()==routingThread &&
            shader && shader==resources->symmetricRayShader.Get() && resources->asymmetricRayShader &&
            SUCCEEDED(self->GetVertexShaderConstantF(209,originalRayConstants,1)))
        {
            setVertexConstants.unsafe_stdcall<HRESULT>(self,209,eyeRayConstants,1);
            shader=resources->asymmetricRayShader.Get();
            rayShaderBound=true;
        }
        return setVertexShader.unsafe_stdcall<HRESULT>(self,shader);
    }

    HRESULT WINAPI SetVertexConstants(IDirect3DDevice9* self,UINT start,const float* data,UINT count)
    {
        const auto result=setVertexConstants.unsafe_stdcall<HRESULT>(self,start,data,count);
        if(SUCCEEDED(result) && GetCurrentThreadId()==routingThread && rayShaderBound &&
            start<=209 && count>209-start)
        {
            std::memcpy(originalRayConstants,data+(209-start)*4,sizeof(originalRayConstants));
            return setVertexConstants.unsafe_stdcall<HRESULT>(self,209,eyeRayConstants,1);
        }
        return result;
    }

    HRESULT WINAPI DrawPrimitive(IDirect3DDevice9* device,D3DPRIMITIVETYPE type,UINT start,UINT count)
    {
        if(GetCurrentThreadId()==routingThread && renderEye>=0 && renderStage==2 && rayShaderBound &&
            !(lightingProbeMask&(1u<<renderEye)))
        {
            lightingProbeMask|=1u<<renderEye;
            float vertexCamera[16]{},pixelCamera[4]{},sun[4]{},shadow[16]{};
            if(SUCCEEDED(device->GetVertexShaderConstantF(12,vertexCamera,4)) &&
                SUCCEEDED(device->GetPixelShaderConstantF(15,pixelCamera,1)) &&
                SUCCEEDED(device->GetPixelShaderConstantF(17,sun,1)) &&
                SUCCEEDED(device->GetPixelShaderConstantF(60,shadow,4)))
            {
                char line[260];
                for(unsigned row=0;row<4;++row)
                {
                    const auto* actual=vertexCamera+row*4;
                    const auto* expected=expectedCamera+row*4;
                    sprintf_s(line,"lighting camera frame=%u eye=%d row=%u VS=(%.6f %.6f %.6f %.6f) expected=(%.6f %.6f %.6f %.6f)",
                        completedFrame,renderEye,row,actual[0],actual[1],actual[2],actual[3],expected[0],expected[1],expected[2],expected[3]);
                    Log(line);
                    const auto* matrix=shadow+row*4;
                    sprintf_s(line,"lighting shadow eye=%d row=%u (%.6f %.6f %.6f %.6f)",renderEye,row,matrix[0],matrix[1],matrix[2],matrix[3]); Log(line);
                }
                sprintf_s(line,"lighting pixel camera eye=%d (%.6f %.6f %.6f %.6f) sun=(%.6f %.6f %.6f %.6f)",
                    renderEye,pixelCamera[0],pixelCamera[1],pixelCamera[2],pixelCamera[3],sun[0],sun[1],sun[2],sun[3]); Log(line);
            }
        }
        return drawPrimitive.unsafe_stdcall<HRESULT>(device,type,start,count);
    }

    HRESULT WINAPI SetPixelShader(IDirect3DDevice9* device,IDirect3DPixelShader9* shader)
    {
        if(GetCurrentThreadId()!=routingThread || !resources)
            return setPixelShader.unsafe_stdcall<HRESULT>(device,shader);
        if(cascadeShaderBound)
        {
            setPixelConstants.unsafe_stdcall<HRESULT>(device,14,originalCascadeForward,1);
            cascadeShaderBound=false;
        }
        if(shader && renderEye>=0 && renderStage==2 && resources->observedPixelShaders.size()<16 &&
            std::none_of(resources->observedPixelShaders.begin(),resources->observedPixelShaders.end(),
                [shader](const auto& value){return value.Get()==shader;}))
        {
            resources->observedPixelShaders.emplace_back(shader);
            UINT bytes=0;
            if(SUCCEEDED(shader->GetFunction(nullptr,&bytes)) && bytes && bytes<=65536)
            {
                std::vector<DWORD> code((bytes+3)/4);
                ComPtr<ID3DXBuffer> assembly;
                if(SUCCEEDED(shader->GetFunction(code.data(),&bytes)) &&
                    SUCCEEDED(D3DXDisassembleShader(code.data(),FALSE,nullptr,&assembly)))
                {
                    const std::string text(static_cast<const char*>(assembly->GetBufferPointer()),assembly->GetBufferSize());
                    const std::regex register14("\\bc14\\b");
                    const auto uses=std::distance(std::sregex_iterator(text.begin(),text.end(),register14),std::sregex_iterator());
                    // Verified directional deferred PS1/PS2: c14 is read only
                    // to select the cascade. World reconstruction uses the
                    // eye camera in VS c12-14 and PS c15 and remains intact.
                    if(uses==1 && text.find("dp3 r1.x, c14, r0.yzww")!=std::string::npos &&
                        text.find("add r1.xyz, -r1.x, -c54")!=std::string::npos &&
                        text.find("mad r1.xyz, r0.y, c60.xyww, r1")!=std::string::npos &&
                        text.find("dcl_2d s15")!=std::string::npos)
                    {
                        resources->cascadeShaders.emplace_back(shader);
                        Log("Verified directional cascade shader: preserve game-camera selector in PS c14");
                    }
                }
            }
        }
        if(shader && renderEye>=0 && renderStage==2 &&
            std::any_of(resources->cascadeShaders.begin(),resources->cascadeShaders.end(),
                [shader](const auto& value){return value.Get()==shader;}) &&
            SUCCEEDED(device->GetPixelShaderConstantF(14,originalCascadeForward,1)))
        {
            setPixelConstants.unsafe_stdcall<HRESULT>(device,14,cascadeForward,1);
            cascadeShaderBound=true;
        }
        return setPixelShader.unsafe_stdcall<HRESULT>(device,shader);
    }

    HRESULT WINAPI SetPixelConstants(IDirect3DDevice9* device,UINT start,const float* data,UINT count)
    {
        const auto result=setPixelConstants.unsafe_stdcall<HRESULT>(device,start,data,count);
        if(SUCCEEDED(result) && GetCurrentThreadId()==routingThread && cascadeShaderBound && start<=14 && count>14-start)
        {
            std::memcpy(originalCascadeForward,data+(14-start)*4,sizeof(originalCascadeForward));
            return setPixelConstants.unsafe_stdcall<HRESULT>(device,14,cascadeForward,1);
        }
        return result;
    }

    void Rebind(IDirect3DDevice9* device,int eye)
    {
        ComPtr<IDirect3DVertexShader9> shader;
        if(SUCCEEDED(device->GetVertexShader(&shader))) SetVertexShader(device,shader.Get());
        ComPtr<IDirect3DPixelShader9> pixel;
        if(SUCCEEDED(device->GetPixelShader(&pixel))) SetPixelShader(device,pixel.Get());
        // RAGE caches the original pointers. Rebind actual device state even
        // when the game omits an identical SetTexture/SetRenderTarget call.
        for(unsigned i=0;i<4;++i)
        {
            ComPtr<IDirect3DSurface9> surface;
            if(SUCCEEDED(device->GetRenderTarget(i,&surface)))
                setTarget.unsafe_stdcall<HRESULT>(device,i,MapSurface(surface.Get(),eye));
        }
        ComPtr<IDirect3DSurface9> depth;
        if(SUCCEEDED(device->GetDepthStencilSurface(&depth)))
            setDepth.unsafe_stdcall<HRESULT>(device,MapSurface(depth.Get(),eye));
        for(unsigned i=0;i<20;++i)
        {
            const unsigned stage=i<16 ? i : D3DVERTEXTEXTURESAMPLER0+i-16;
            ComPtr<IDirect3DBaseTexture9> texture;
            if(SUCCEEDED(device->GetTexture(stage,&texture)))
                setTexture.unsafe_stdcall<HRESULT>(device,stage,MapTexture(texture.Get(),eye));
        }
    }

    class EyeMarker : public CBaseDC
    {
        int eye;
        unsigned stage,frame;
        bool begin;
        OpenXRVR::EyeView views[2]{};
        float camera[16]{};
        float shadowForward[4]{};
        long long poseTime;
    public:
        EyeMarker(int value,unsigned role,unsigned id,bool start):CBaseDC(),eye(value),stage(role),frame(id),begin(start),poseTime(viewTime)
        {views[0]=xrViews[0]; views[1]=xrViews[1]; std::memcpy(camera,eyeViewports[value].mViewInverseMatrix,sizeof(camera));
         std::memcpy(shadowForward,gameShadowForward,sizeof(shadowForward));}
        int32_t GetSize() override {return sizeof(EyeMarker);}
        void DrawCommand() override
        {
            if(!ready || !RageDirect3DDevice9::m_pRealDevice) return;
            auto* device=*RageDirect3DDevice9::m_pRealDevice;
            if(!device) return;
            if(begin && stage==1 && eye==0 && captureNextPair.exchange(false)) {captured=false; lightingProbeMask=0;}
            renderEye=begin ? eye : -1;
            renderStage=begin ? stage : 0;
            if(begin)
            {
                std::memcpy(expectedCamera,camera,sizeof(expectedCamera));
                std::memcpy(cascadeForward,shadowForward,sizeof(cascadeForward));
                const auto& fov=views[eye].fov;
                const float left=std::tan(fov[0]),right=std::tan(fov[1]),
                    up=std::tan(fov[2]),down=std::tan(fov[3]);
                eyeRayConstants[0]=(right-left)*.5f; eyeRayConstants[1]=(up-down)*.5f;
                eyeRayConstants[2]=(right+left)*.5f; eyeRayConstants[3]=(up+down)*.5f;
            }
            Rebind(device,renderEye);
            if(!begin && stage==3)
            {
                if(completedFrame!=frame) {completedFrame=frame; completedEyes=0;}
                completedEyes|=1u<<eye;
                if(completedEyes==3)
                {completedViews[0]=views[0]; completedViews[1]=views[1]; completedPoseTime=poseTime;}
            }
            char line[160];
            sprintf_s(line,"execute frame=%u eye=%d stage=%u begin=%u thread=%lu",frame,eye,stage,begin,GetCurrentThreadId());
            if(maxFrames==1 || frame%120==0) Log(line);
        }
    };

    void Marker(unsigned stage,bool begin)
    {
        if(buildingEye<0) return;
        auto* marker=new EyeMarker(buildingEye,stage,gameFrame,begin);
        if(marker) marker->Append();
    }

    void CaptureDepth(IDirect3DDevice9* device)
    {
        auto target=std::find_if(resources->targets.begin(),resources->targets.end(),
            [](const Target& value){return value.name=="_DEFERRED_GBUFFER_3_";});
        if(target==resources->targets.end()) {Log("Depth capture target unavailable"); return;}
        D3DSURFACE_DESC desc{};
        target->originalSurface->GetDesc(&desc);
        ComPtr<IDirect3DTexture9> color;
        ComPtr<IDirect3DSurface9> surface,staging;
        if(!Check("depth sample target",device->CreateTexture(desc.Width,desc.Height,1,D3DUSAGE_RENDERTARGET,
            D3DFMT_R32F,D3DPOOL_DEFAULT,&color,nullptr)) ||
            !Check("depth sample surface",color->GetSurfaceLevel(0,&surface)) ||
            !Check("depth staging",device->CreateOffscreenPlainSurface(desc.Width,desc.Height,
                D3DFMT_R32F,D3DPOOL_SYSTEMMEM,&staging,nullptr))) return;
        constexpr char shader[]="sampler2D depth:register(s0); float4 main(float2 uv:TEXCOORD0):COLOR0 {return tex2D(depth,uv).rrrr;}";
        ComPtr<ID3DXBuffer> code,errors;
        ComPtr<IDirect3DPixelShader9> pixel;
        if(!Check("depth sample compile",D3DXCompileShader(shader,sizeof(shader)-1,nullptr,nullptr,
            "main","ps_3_0",0,&code,&errors,nullptr)) ||
            !Check("depth sample shader",device->CreatePixelShader(static_cast<DWORD*>(code->GetBufferPointer()),&pixel))) return;
        ComPtr<IDirect3DStateBlock9> state;
        if(!Check("depth capture state",device->CreateStateBlock(D3DSBT_ALL,&state))) return;
        state->Capture();
        ComPtr<IDirect3DSurface9> saved[4],savedDepth;
        D3DVIEWPORT9 savedViewport{};
        for(unsigned i=0;i<4;++i) device->GetRenderTarget(i,&saved[i]);
        device->GetDepthStencilSurface(&savedDepth);
        device->GetViewport(&savedViewport);
        device->SetDepthStencilSurface(nullptr);
        for(unsigned i=1;i<4;++i) device->SetRenderTarget(i,nullptr);
        device->SetRenderTarget(0,surface.Get());
        const D3DVIEWPORT9 viewport{0,0,desc.Width,desc.Height,0,1};
        device->SetViewport(&viewport);
        device->SetVertexShader(nullptr);
        device->SetPixelShader(pixel.Get());
        device->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1);
        device->SetRenderState(D3DRS_ZENABLE,FALSE);
        device->SetRenderState(D3DRS_ZWRITEENABLE,FALSE);
        device->SetRenderState(D3DRS_STENCILENABLE,FALSE);
        device->SetRenderState(D3DRS_ALPHATESTENABLE,FALSE);
        device->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE);
        device->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE);
        device->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);
        device->SetRenderState(D3DRS_COLORWRITEENABLE,15);
        device->SetRenderState(D3DRS_SRGBWRITEENABLE,FALSE);
        device->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_POINT);
        device->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_POINT);
        device->SetSamplerState(0,D3DSAMP_MIPFILTER,D3DTEXF_NONE);
        device->SetSamplerState(0,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);
        device->SetSamplerState(0,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);
        device->SetSamplerState(0,D3DSAMP_SRGBTEXTURE,FALSE);
        struct Vertex {float x,y,z,rhw,u,v;};
        const float w=static_cast<float>(desc.Width),h=static_cast<float>(desc.Height);
        const Vertex quad[]{{-.5f,-.5f,0,1,0,0},{w-.5f,-.5f,0,1,1,0},
            {-.5f,h-.5f,0,1,0,1},{w-.5f,h-.5f,0,1,1,1}};
        for(unsigned eye=0;eye<2;++eye)
        {
            // Diagnostic reads a clone outside the eye marker scope. Bypass
            // normal routing, which would restore this pointer to the original.
            setTexture.unsafe_stdcall<HRESULT>(device,0,target->texture[eye].Get());
            if(!Check("depth sample draw",device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(Vertex)))) continue;
            if(!Check("depth sample readback",device->GetRenderTargetData(surface.Get(),staging.Get()))) continue;
            D3DLOCKED_RECT locked{};
            if(!Check("depth sample lock",staging->LockRect(&locked,nullptr,D3DLOCK_READONLY))) continue;
            float low=INFINITY,high=-INFINITY;
            unsigned valid=0;
            unsigned long long hash=14695981039346656037ull;
            for(unsigned y=0;y<desc.Height;++y)
            {
                auto* row=static_cast<unsigned char*>(locked.pBits)+y*locked.Pitch;
                for(unsigned x=0;x<desc.Width;++x)
                {
                    float value; std::memcpy(&value,row+x*4,4);
                    if(std::isfinite(value)) {low=std::min(low,value); high=std::max(high,value); ++valid;}
                }
                for(unsigned x=0;x<desc.Width*4;++x) {hash^=row[x]; hash*=1099511628211ull;}
            }
            staging->UnlockRect();
            char line[200];
            sprintf_s(line,"depth frame=%u eye=%u INTZ=%p sample=R32F hash=%016llX finite=%u range=%g,%g",
                completedFrame,eye,target->surface[eye].Get(),hash,valid,low,high);
            Log(line);
        }
        device->SetTexture(0,nullptr);
        for(unsigned i=0;i<4;++i) device->SetRenderTarget(i,saved[i].Get());
        device->SetDepthStencilSurface(savedDepth.Get());
        state->Apply();
        device->SetViewport(&savedViewport);
    }

    void CaptureEyes(IDirect3DDevice9* device)
    {
        if(captured || completedEyes!=3 || !resources) return;
        captured=true;
        CaptureDepth(device);
        for(unsigned eye=0;eye<2;++eye)
        {
            D3DSURFACE_DESC desc{};
            resources->finalSurface[eye]->GetDesc(&desc);
            ComPtr<IDirect3DSurface9> staging;
            if(!Check("eye readback allocation",device->CreateOffscreenPlainSurface(desc.Width,desc.Height,
                desc.Format,D3DPOOL_SYSTEMMEM,&staging,nullptr)) ||
                !Check("eye readback",device->GetRenderTargetData(resources->finalSurface[eye].Get(),staging.Get()))) continue;
            D3DLOCKED_RECT locked{};
            if(!Check("eye readback lock",staging->LockRect(&locked,nullptr,D3DLOCK_READONLY))) continue;
            wchar_t path[MAX_PATH]{};
            GetModuleFileNameW(nullptr,path,MAX_PATH);
            auto* name=wcsrchr(path,L'\\');
            if(name) name[1]=0;
            wchar_t suffix[80];
            swprintf_s(suffix,L"plugins\\GTAIV.VR.eye%u.bmp",eye);
            wcscat_s(path,suffix);
            FILE* file=nullptr;
            _wfopen_s(&file,path,L"wb");
            const DWORD bytes=desc.Width*desc.Height*4;
            BITMAPFILEHEADER header{};
            header.bfType=0x4D42; header.bfOffBits=sizeof(header)+sizeof(BITMAPINFOHEADER);
            header.bfSize=header.bfOffBits+bytes;
            BITMAPINFOHEADER info{};
            info.biSize=sizeof(info); info.biWidth=desc.Width; info.biHeight=-static_cast<LONG>(desc.Height);
            info.biPlanes=1; info.biBitCount=32; info.biCompression=BI_RGB; info.biSizeImage=bytes;
            unsigned long long hash=14695981039346656037ull;
            if(file) {fwrite(&header,sizeof(header),1,file); fwrite(&info,sizeof(info),1,file);}
            for(unsigned y=0;y<desc.Height;++y)
            {
                auto* row=static_cast<unsigned char*>(locked.pBits)+y*locked.Pitch;
                for(unsigned x=0;x<desc.Width*4;++x) {hash^=row[x]; hash*=1099511628211ull;}
                if(file) fwrite(row,desc.Width*4,1,file);
            }
            if(file) fclose(file);
            staging->UnlockRect();
            char line[180];
            sprintf_s(line,"captured frame=%u eye=%u hash=%016llX size=%ux%u surface=%p",
                completedFrame,eye,hash,desc.Width,desc.Height,resources->finalSurface[eye].Get());
            Log(line);
        }
    }

    void PublishEyes(IDirect3DDevice9* device)
    {
        if(!submit || !resources || completedEyes!=3 || publishedFrame==completedFrame) return;
        // STOPPING/reset is temporary. Retain the completed pair until the
        // session can accept it instead of permanently disabling submission.
        if(!OpenXRVR::CanAcceptEyeImages()) return;
        if(mirror && maxFrames!=1)
            mirror=Check("desktop eye mirror",stretch.unsafe_stdcall<HRESULT>(device,
                resources->finalSurface[0].Get(),nullptr,resources->originalBack.Get(),nullptr,D3DTEXF_NONE));
        ComPtr<IDirect3DQuery9> query;
        if(!Check("eye D3D9 event",device->CreateQuery(D3DQUERYTYPE_EVENT,&query)) ||
            !Check("eye D3D9 event issue",query->Issue(D3DISSUE_END))) {submit=false; return;}
        const auto deadline=GetTickCount64()+1000;
        HRESULT status=S_FALSE;
        do {status=query->GetData(nullptr,0,D3DGETDATA_FLUSH); if(status==S_FALSE) Sleep(0);}
        while(status==S_FALSE && GetTickCount64()<deadline);
        if(status!=S_OK) {Log("D3D9 eye completion failed; submission disabled"); submit=false; return;}
        D3DSURFACE_DESC desc{};
        resources->finalSurface[0]->GetDesc(&desc);
        if(OpenXRVR::SetEyeImages(resources->shared,desc.Width,desc.Height,completedViews,completedPoseTime))
        {
            if(publishedFrame==0 || completedFrame%120==0)
            {
                char line[140];
                sprintf_s(line,"Published completed eye pair frame=%u poseTime=%lld",completedFrame,completedPoseTime);
                Log(line);
            }
            publishedFrame=completedFrame;
        }
        else {Log("Eye pair publication unavailable; retaining desktop"); submit=false;}
    }

    void CreateResources(IDirect3DDevice9* device)
    {
        // Target creation can lag the first unpaused frame during loading.
        // Check completeness before allocating clones, and retry later.
        for(const char* required : {"_DEFERRED_GBUFFER_0_", "_DEFERRED_GBUFFER_1_",
            "_DEFERRED_GBUFFER_2_", "_DEFERRED_GBUFFER_3_", "_STENCIL_BUFFER_", "_BACK_ZBUFFER_"})
        {
            const auto found=rage::grcTextureFactoryPC::RTCache.find(required);
            if(found==rage::grcTextureFactoryPC::RTCache.end() || !found->second || !found->second->mD3DTexture)
            {
                static std::string previousMissing;
                if(previousMissing!=required)
                {
                    previousMissing=required;
                    Log(("Waiting for world target: "+previousMissing).c_str());
                }
                return;
            }
        }
        auto owned=std::make_unique<Resources>();
        ComPtr<IDirect3DDevice9Ex> ex;
        if(!Check("live Ex device",device->QueryInterface(__uuidof(IDirect3DDevice9Ex),
            reinterpret_cast<void**>(ex.GetAddressOf()))) ||
            !Check("back buffer",device->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&owned->originalBack))) return;
        D3DSURFACE_DESC back{};
        if(!Check("back description",owned->originalBack->GetDesc(&back)) || back.MultiSampleType!=D3DMULTISAMPLE_NONE) return;
        ComPtr<IDirect3DSwapChain9> desktopChain;
        D3DPRESENT_PARAMETERS desktopParameters{};
        if(SUCCEEDED(device->GetSwapChain(0,&desktopChain)) && SUCCEEDED(desktopChain->GetPresentParameters(&desktopParameters)))
        {
            char line[160];
            sprintf_s(line,"Desktop presentation interval=0x%08lX windowed=%u refresh=%u",desktopParameters.PresentationInterval,
                desktopParameters.Windowed,desktopParameters.FullScreen_RefreshRateInHz); Log(line);
        }
        for(unsigned eye=0;eye<2;++eye)
            if(!Check("shared eye color",device->CreateTexture(back.Width,back.Height,1,D3DUSAGE_RENDERTARGET,
                D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,&owned->finalTexture[eye],&owned->shared[eye])) ||
                !Check("eye color surface",owned->finalTexture[eye]->GetSurfaceLevel(0,&owned->finalSurface[eye]))) return;
        for(const auto& [name,original]:rage::grcTextureFactoryPC::RTCache)
        {
            if(!EyeTarget(name) || !original || !original->mD3DTexture) continue;
            Target target;
            target.name=name;
            target.original=original->mD3DTexture;
            D3DSURFACE_DESC desc{};
            if(!Check("target description",target.original->GetLevelDesc(0,&desc)) ||
                !Check("original target surface",target.original->GetSurfaceLevel(0,&target.originalSurface))) return;
            if(desc.MultiSampleType!=D3DMULTISAMPLE_NONE) {Log("Unsupported multisampled eye target"); return;}
            for(unsigned eye=0;eye<2;++eye)
            {
                if(!Check(name.c_str(),device->CreateTexture(desc.Width,desc.Height,target.original->GetLevelCount(),
                    desc.Usage,desc.Format,D3DPOOL_DEFAULT,&target.texture[eye],nullptr)) ||
                    !Check("target surface",target.texture[eye]->GetSurfaceLevel(0,&target.surface[eye]))) return;
                if(desc.Usage&D3DUSAGE_RENDERTARGET)
                    Check("seed color/history",device->StretchRect(target.originalSurface.Get(),nullptr,
                        target.surface[eye].Get(),nullptr,D3DTEXF_NONE));
            }
            // AO builds a manual depth mip chain. Route every surface level,
            // rather than letting higher mip writes leak into shared targets.
            const auto levels=target.original->GetLevelCount();
            target.originalMips.resize(levels-1);
            for(auto& mips:target.eyeMips) mips.resize(levels-1);
            for(unsigned level=1;level<levels;++level)
            {
                if(!Check("original target mip",target.original->GetSurfaceLevel(level,&target.originalMips[level-1]))) return;
                for(unsigned eye=0;eye<2;++eye)
                {
                    if(!Check("eye target mip",target.texture[eye]->GetSurfaceLevel(level,&target.eyeMips[eye][level-1]))) return;
                    if(desc.Usage&D3DUSAGE_RENDERTARGET)
                        Check("seed history mip",device->StretchRect(target.originalMips[level-1].Get(),nullptr,
                            target.eyeMips[eye][level-1].Get(),nullptr,D3DTEXF_NONE));
                }
            }
            owned->targets.push_back(std::move(target));
        }
        for(const char* required : {"_DEFERRED_GBUFFER_0_", "_DEFERRED_GBUFFER_1_",
            "_DEFERRED_GBUFFER_2_", "_DEFERRED_GBUFFER_3_", "_STENCIL_BUFFER_", "_BACK_ZBUFFER_"})
            if(std::none_of(owned->targets.begin(),owned->targets.end(),
                [required](const Target& target){return target.name==required;}))
            {Log("Incomplete world target inventory"); return;}
        auto** table=*reinterpret_cast<void***>(device);
        if(!setTarget) setTarget=safetyhook::create_inline(table[37],SetTarget);
        if(!setDepth) setDepth=safetyhook::create_inline(table[39],SetDepth);
        if(!setTexture) setTexture=safetyhook::create_inline(table[65],SetTexture);
        if(!stretch) stretch=safetyhook::create_inline(table[34],Stretch);
        if(!setVertexShader) setVertexShader=safetyhook::create_inline(table[92],SetVertexShader);
        if(!setVertexConstants) setVertexConstants=safetyhook::create_inline(table[94],SetVertexConstants);
        if(!drawPrimitive) drawPrimitive=safetyhook::create_inline(table[81],DrawPrimitive);
        if(!setPixelShader) setPixelShader=safetyhook::create_inline(table[107],SetPixelShader);
        if(!setPixelConstants) setPixelConstants=safetyhook::create_inline(table[109],SetPixelConstants);
        if(!setTarget || !setDepth || !setTexture || !stretch || !setVertexShader || !setVertexConstants || !drawPrimitive || !setPixelShader || !setPixelConstants) {Log("Device routing hook unavailable"); return;}
        char line[160];
        sprintf_s(line,"allocated two eye sets targets=%zu finalColor=%ux%u format=21 shared=%u,%u",
            owned->targets.size(),back.Width,back.Height,owned->shared[0]!=nullptr,owned->shared[1]!=nullptr);
        Log(line);
        resources=owned.release();
        routingThread=GetCurrentThreadId();
        ready=true;
    }

    void __fastcall SceneSetup(void* self,void* edx,void* parent)
    {
        sceneSetup.unsafe_fastcall(self,edx,parent);
        setupThisFrame=true;
        scenePhase=self;
        worldParent=parent;
        prepared=false;
        if(!ready || (maxFrames && stereoFrames>=maxFrames) || *CTimer::ms_bUserPause || *CTimer::ms_bScriptPause) return;
        if(!OpenXRVR::GetEyeViews(xrViews,viewTime)) return;
        if(stereoFrames==0) Log("preparing stereo camera");
        auto* base=reinterpret_cast<rage::grcViewport*>(static_cast<char*>(self)+0xB0);
        std::memcpy(gameShadowForward,base->mViewInverseMatrix[2],sizeof(gameShadowForward));
        if(!VRCamera::Generate(*base,xrViews,eyeViewports)) return;
        if(stereoFrames==0) Log("generated stereo camera");
        VRMath::Matrix cameras[2]{};
        float fov[2][4]{};
        for(unsigned i=0;i<2;++i)
        {
            std::memcpy(cameras[i].data(),eyeViewports[i].mViewInverseMatrix,sizeof(cameras[i]));
            std::memcpy(fov[i],xrViews[i].fov,sizeof(fov[i]));
        }
        std::array<VRMath::Vector,6> planes{};
        try {planes=VRMath::UnionBounds(cameras,fov,base->mNearClip,base->mFarClip);}
        catch(const std::exception&) {Log("Invalid union bounds; ordinary frame retained"); return;}
        visibilityPlanes=planes;
        std::memcpy(base->mFrustumClipPlanes,planes.data(),sizeof(base->mFrustumClipPlanes));
        auto* secondary=reinterpret_cast<rage::grcViewport*>(static_cast<char*>(self)+0x4A0);
        std::memcpy(secondary->mFrustumClipPlanes,planes.data(),sizeof(secondary->mFrustumClipPlanes));
        preparedFrame=gameFrame;
        prepared=true;
        if(stereoFrames==0) Log("prepared conservative scene visibility");
    }

    void __fastcall DrawSetup(void* self,void* edx,void* parent)
    {
        drawSetup.unsafe_fastcall(self,edx,parent);
        if(!setupThisFrame || parent!=worldParent) return;
        if(!lightingPhase) lightingPhase=self;
        else if(!drawPhase && self!=lightingPhase) drawPhase=self;
        if(prepared && preparedFrame==gameFrame && (self==lightingPhase || self==drawPhase))
            for(unsigned offset:{0xB0u,0x4A0u})
            {
                auto* viewport=reinterpret_cast<rage::grcViewport*>(static_cast<char*>(self)+offset);
                std::memcpy(viewport->mFrustumClipPlanes,visibilityPlanes.data(),sizeof(viewport->mFrustumClipPlanes));
            }
    }

    void __fastcall RenderFrame(void* self,void* edx)
    {
        setupThisFrame=false;
        lightingPhase=nullptr;
        drawPhase=nullptr;
        renderFrame.unsafe_fastcall(self,edx);
    }

    void __fastcall BuildFrame(void* self,void* edx)
    {
        if(!ready || !prepared || preparedFrame!=gameFrame || !scenePhase || !lightingPhase || !drawPhase || (maxFrames && stereoFrames>=maxFrames))
        {buildFrame.unsafe_fastcall(self,edx); return;}
        void* phases[]{scenePhase,lightingPhase,drawPhase};
        // CE's builder dispatches phaseArray[i]->vtable[7], with its array
        // at +0x38 and uint16 count at +0x3C (verified live at 0x008F7FA4).
        // Repeat only the world streams for eye 1. Shared shadow atlases,
        // reflections, weather updates and UI must not be rebuilt twice.
        auto& phaseArray=*reinterpret_cast<void***>(static_cast<char*>(self)+0x38);
        auto& phaseCount=*reinterpret_cast<uint16_t*>(static_cast<char*>(self)+0x3C);
        void** originalArray=phaseArray;
        const auto originalCount=phaseCount;
        std::vector<void*> eyeWorldPhases;
        if(!originalArray || originalCount>256)
        {buildFrame.unsafe_fastcall(self,edx); return;}
        for(unsigned i=0;i<originalCount;++i)
            if(originalArray[i]==scenePhase || originalArray[i]==lightingPhase || originalArray[i]==drawPhase)
                eyeWorldPhases.push_back(originalArray[i]);
        if(eyeWorldPhases.size()!=3)
        {Log("World phase inventory ambiguous; retaining ordinary frame"); buildFrame.unsafe_fastcall(self,edx); return;}
        rage::grcViewport saved[3][2]{};
        for(unsigned phase=0;phase<3;++phase)
            for(unsigned slot=0;slot<2;++slot)
                std::memcpy(&saved[phase][slot],static_cast<char*>(phases[phase])+(slot ? 0x4A0 : 0xB0),sizeof(rage::grcViewport));
        for(unsigned eye=0;eye<2;++eye)
        {
            for(unsigned phase=0;phase<3;++phase)
                for(unsigned slot=0;slot<2;++slot)
                    std::memcpy(static_cast<char*>(phases[phase])+(slot ? 0x4A0 : 0xB0),&eyeViewports[eye],sizeof(rage::grcViewport));
            buildingEye=eye;
            if(eye==1)
            {
                phaseArray=eyeWorldPhases.data();
                phaseCount=static_cast<uint16_t>(eyeWorldPhases.size());
            }
            if(stereoFrames==0) Log(eye ? "build eye 1 begin" : "build eye 0 begin");
            buildFrame.unsafe_fastcall(self,edx);
            if(stereoFrames==0) Log(eye ? "build eye 1 end" : "build eye 0 end");
        }
        buildingEye=-1;
        phaseArray=originalArray;
        phaseCount=originalCount;
        for(unsigned phase=0;phase<3;++phase)
            for(unsigned slot=0;slot<2;++slot)
                std::memcpy(static_cast<char*>(phases[phase])+(slot ? 0x4A0 : 0xB0),&saved[phase][slot],sizeof(rage::grcViewport));
        ++stereoFrames;
        char line[160];
        sprintf_s(line,"built frame=%u two world lists displayTime=%lld process counter before=%u after=%u",gameFrame,viewTime,preparedFrame,gameFrame);
        if(stereoFrames==1 || stereoFrames==maxFrames || gameFrame%120==0) Log(line);
    }
}

void VRStereo::Shutdown()
{
    enabled=false;
    ready=false;
    renderEye=-1;
    renderStage=0;
    rayShaderBound=false;
    cascadeShaderBound=false;
    lightingProbeMask=0;
    completedEyes=0;
    captured=false;
    publishedFrame=0;
    delete resources;
    resources=nullptr;
    routingThread=0;
    restartRequested=false;
}

bool VRStereo::RestartExperiment()
{
    OpenXRVR::EyeView views[2]{}; long long time=0;
    if(!enabled || !ready || !OpenXRVR::GetEyeViews(views,time)) return false;
    restartRequested=true;
    return true;
}

std::string VRStereo::Status()
{
    char line[160];
    sprintf_s(line,"{\"enabled\":%s,\"ready\":%s,\"builtFrames\":%u,\"frameBudget\":%u,\"continuous\":%s}",
        enabled ? "true" : "false",ready ? "true" : "false",stereoFrames.load(),maxFrames,maxFrames ? "false" : "true");
    return line;
}

void VRStereo::Install()
{
    CIniReader ini("");
    if(!ini.ReadInteger("VR","ExperimentalStereo",0)) return;
    if(CIniReader(CSettings::d3d9cfgPath).ReadInteger("MAIN","API",0)!=0)
    {Log("Stereo sharing requires system D3D9Ex; DXVK selected, retaining desktop"); return;}
    if(ini.ReadInteger("DEVELOPMENT","ProbeEyeViewport",0) || ini.ReadInteger("DEVELOPMENT","ProbeRenderBindings",0))
    {Log("Disable viewport/binding probes before stereo routing"); return;}
    auto a=hook::pattern("53 8B 5C 24 08 56 57 8B F9 8D 73 10 56 8D 8F B0 00 00 00");
    auto b=hook::pattern("53 8B 5C 24 08 55 8B 83 38 05 00 00 56 57 8B F9 8D 73 10 56");
    auto c=hook::pattern("55 8B EC 83 E4 F8 83 EC 20 53 55 56 57 8B E9 E8 ? ? ? ? 8D 4D 38");
    auto d=hook::pattern("56 57 8B F9 E8 ? ? ? ? 6A 01 6A 10 E8 ? ? ? ? 83 C4 08 85 C0");
    if(a.size()!=1 || b.size()!=1 || c.size()!=1 || d.size()!=1) {Log("Stereo scheduler signatures unavailable"); return;}
    sceneSetup=safetyhook::create_inline(a.get_first(),SceneSetup);
    drawSetup=safetyhook::create_inline(b.get_first(),DrawSetup);
    renderFrame=safetyhook::create_inline(c.get_first(),RenderFrame);
    buildFrame=safetyhook::create_inline(d.get_first(),BuildFrame);
    if(!sceneSetup || !drawSetup || !renderFrame || !buildFrame) {Log("Stereo scheduler hooks unavailable"); return;}
    maxFrames=std::clamp(ini.ReadInteger("VR","ExperimentalStereoFrames",1),0,600); // Zero runs continuously.
    submit=ini.ReadInteger("VR","ExperimentalSubmit",0)!=0;
    enabled=true;
    OpenXRVR::SetEyeImageProvider([]()
    {
        if(!enabled || !ready || !RageDirect3DDevice9::m_pRealDevice) return;
        auto* device=*RageDirect3DDevice9::m_pRealDevice;
        if(device) {CaptureEyes(device); PublishEyes(device);}
    });
    FusionFix::onGameProcessEvent() += []()
    {
        ++gameFrame;
        if(restartRequested.exchange(false))
        {
            stereoFrames=0;
            prepared=false;
            captureNextPair=true;
            Log("Stereo experiment restarted on game-process callback");
        }
    };
    CRenderPhaseDeferredLighting_SceneToGBuffer::OnBuildRenderList() += [](){Marker(1,true);};
    CRenderPhaseDeferredLighting_SceneToGBuffer::OnAfterBuildRenderList() += [](){Marker(1,false);};
    CRenderPhaseDeferredLighting_LightsToScreen::OnBuildRenderList() += [](){Marker(2,true);};
    CRenderPhaseDeferredLighting_LightsToScreen::OnAfterBuildRenderList() += [](){Marker(2,false);};
    CRenderPhaseDrawScene::OnBuildRenderList() += [](){Marker(3,true);};
    CRenderPhaseDrawScene::OnAfterBuildRenderList() += [](){Marker(3,false);};
    FusionFix::onBeforeReset() += [](){const bool wanted=enabled; Shutdown(); enabled=wanted; stereoFrames=0;};
    FusionFix::onEndScene() += []()
    {
        static unsigned frames=0;
        if(!enabled || !RageDirect3DDevice9::m_pRealDevice) return;
        auto* device=*RageDirect3DDevice9::m_pRealDevice;
        if(ready) {if(device) {CaptureEyes(device); PublishEyes(device);} return;}
        if(maxFrames && stereoFrames>=maxFrames) return;
        if(!CTimer::ms_bUserPause || !CTimer::ms_bScriptPause || *CTimer::ms_bUserPause || *CTimer::ms_bScriptPause) return;
        if(++frames<180) return;
        OpenXRVR::EyeView views[2]{};
        long long time=0;
        if(!OpenXRVR::GetEyeViews(views,time)) return;
        if(device && frames%120==0) CreateResources(device);
    };
    Log(maxFrames ? "Bounded experimental stereo enabled" : "Continuous experimental stereo enabled");
    Log(submit ? "OpenXR eye submission enabled" : "OpenXR eye submission disabled");
}
