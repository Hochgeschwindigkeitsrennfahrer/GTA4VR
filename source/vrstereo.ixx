module;
#include <common.hxx>
#include <wrl/client.h>
#include <d3dx9shader.h>
#include "includes/vrmath.hpp"
#include "includes/vrmodelrefs.hpp"
#include <atomic>
#include <cstring>
#include <cstdio>
#include <cwchar>
#include <vector>
#include <string>
#include <memory>
#include <regex>
#include <unordered_set>
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
    bool RestartLodProbe();
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
    unsigned buildingStage=0;
    unsigned hiddenPlayerCalls[2]{};
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
    SafetyHookInline pedRender{};
    SafetyHookInline playerDrawCtor{},playerDrawExecute{},playerDrawDestroy{};
    SafetyHookInline retainModel{};
    VRModelRefs::Batch modelReferences[4]{};
    SRWLOCK modelReferencesLock=SRWLOCK_INIT;
    unsigned retainedModelPeak=0,duplicateModelReferences=0;
    thread_local bool buildingLocalBody=false;
    SRWLOCK bodyCommandsLock=SRWLOCK_INIT;
    std::unordered_set<void*> bodyCommands;
    unsigned headlessBodyCalls[2]{};
    SafetyHookInline cameraVolumeCull{};
    unsigned cameraVolumeCorrections=0;
    SafetyHookInline sphereCull{};
    SafetyHookInline visibilityRaster{};
    SafetyHookInline sceneVisibility{};
    unsigned sceneVisibilityCorrections=0;
    unsigned sharedCameraCorrections=0;
    void** currentVisibilityPhase=nullptr;
    void* fineGridCell=nullptr;
    void* coarseGridCell=nullptr;
    void* streamingGridCell=nullptr;
    unsigned streamingRasterQueries=0;
    unsigned headsetFineRasterQueries=0;
    bool waitingForFreshViews=false;
    constexpr uint32_t commandBufferCapacity=8u*1024*1024;
    uint32_t* commandPool=nullptr;
    std::atomic<unsigned> commandBufferPeak{0},commandBufferOverflows{0};
    SafetyHookInline lodDecision{},entityCull{},entityOcclusion{},requestFrustum{},outdoorLod{};
    thread_local void* probedEntity=nullptr;
    thread_local void* probedPhase=nullptr;
    thread_local unsigned probedRejects=0;
    std::atomic<bool> lodProbeRequested{false};
    unsigned lodProbeFrame=0,lodProbeSamples=480,lodProbeEntityCount=0;
    void* lodProbeEntities[48]{};
    unsigned requestProbeSamples=32;
    unsigned outdoorProbeSamples=480,outdoorProbeFrame=0,outdoorProbeCounts[2]{};
    void* outdoorProbeEntities[2][48]{};
    unsigned outdoorLodSamples=480,outdoorLodFrame=0,outdoorLodCount=0;
    void* outdoorLodEntities[128]{};
    unsigned visibilityRasterQueries=0;
    unsigned visibilityRasterObservations=0;
    unsigned sphereQueries[3]{},sphereCorrections[3]{};
    rage::grcViewport* observedSphereViewports[16]{};
    unsigned observedSphereViewportCount=0;
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
        buildingStage=begin ? stage : 0;
        auto* marker=new EyeMarker(buildingEye,stage,gameFrame,begin);
        if(marker) marker->Append();
    }

    // CE CDrawPlayerDC: constructor ret 0x28; executor ret; destructor ret 4.
    // Tag commands while the local ped builds an eye stream. A matching model
    // index alone would also affect other entities using the player's model.
    void* __fastcall PlayerDrawCtor(void* self,void* edx,uintptr_t a,uintptr_t b,
        uintptr_t c,uintptr_t d,uintptr_t e,uintptr_t f,uintptr_t g,uintptr_t h,
        uintptr_t i,uintptr_t j)
    {
        auto* result=playerDrawCtor.unsafe_fastcall<void*>(self,edx,a,b,c,d,e,f,g,h,i,j);
        if(buildingLocalBody && result)
        {
            AcquireSRWLockExclusive(&bodyCommandsLock);
            if(bodyCommands.size()<512) bodyCommands.insert(result);
            else VRCamera::SetHeadlessBodyAvailable(false);
            ReleaseSRWLockExclusive(&bodyCommandsLock);
        }
        return result;
    }

    void __fastcall RetainModel(void* self,void* edx,int model)
    {
        // CE942000 adds a model-info reference and appends its ID to a
        // four-slot lifetime list. 9424E0 releases exactly those references
        // after the corresponding command batch. One hold per distinct model
        // preserves that lifetime without multiplying holds for both eyes.
        if(!ready)
        {
            AcquireSRWLockExclusive(&modelReferencesLock);
            for(auto& batch:modelReferences) batch.owner=nullptr;
            ReleaseSRWLockExclusive(&modelReferencesLock);
            retainModel.unsafe_fastcall(self,edx,model); return;
        }
        if(model<0 || model>32767)
        {retainModel.unsafe_fastcall(self,edx,model); return;}
        auto* manager=static_cast<char*>(self);
        const unsigned slot=*reinterpret_cast<unsigned*>(manager+0x2040);
        if(slot>=4) {retainModel.unsafe_fastcall(self,edx,model); return;}
        auto* data=*reinterpret_cast<uint16_t**>(manager+0x204C+slot*8);
        const unsigned count=*reinterpret_cast<uint16_t*>(manager+0x2050+slot*8);
        AcquireSRWLockExclusive(&modelReferencesLock);
        auto& batch=modelReferences[slot];
        if(!batch.NeedsHold(self,data,count,static_cast<unsigned>(model))) ++duplicateModelReferences;
        else
        {
            retainModel.unsafe_fastcall(self,edx,model);
        }
        batch.Recorded(static_cast<unsigned>(model),*reinterpret_cast<uint16_t*>(manager+0x2050+slot*8));
        if(batch.count>retainedModelPeak)
        {
            const bool first=retainedModelPeak==0;
            retainedModelPeak=batch.count;
            if(first || retainedModelPeak%1024==0)
            {
                char line[160];
                sprintf_s(line,"VR model lifetime list peak=%u capacity=%u duplicates=%u",retainedModelPeak,
                    *reinterpret_cast<uint16_t*>(manager+0x2052+slot*8),duplicateModelReferences);
                Log(line);
            }
        }
        ReleaseSRWLockExclusive(&modelReferencesLock);
    }

    void __fastcall PlayerDrawExecute(void* self,void* edx)
    {
        AcquireSRWLockExclusive(&bodyCommandsLock);
        const bool local=bodyCommands.erase(self)!=0;
        ReleaseSRWLockExclusive(&bodyCommandsLock);
        auto** table=reinterpret_cast<void**>(static_cast<char*>(self)+0x10);
        if(local && renderEye>=0 && renderStage && routingThread.load()==GetCurrentThreadId() && *table)
        {
            // AB7EA0 synchronously reads eleven component pointers and flag
            // +5C. Substitute a command-local copy; never edit the ped's table.
            alignas(16) std::array<std::byte,0x60> components;
            void* original=*table;
            std::memcpy(components.data(),original,components.size());
            const uintptr_t empty=0;
            for(unsigned component:{0u,7u,9u,10u}) // head, hair, teeth, face
                std::memcpy(components.data()+component*4,&empty,4);
            *table=components.data();
            playerDrawExecute.unsafe_fastcall(self,edx);
            *table=original;
            if(++headlessBodyCalls[renderEye]==1) Log("Headless local-player body command executed in an eye stream");
            return;
        }
        playerDrawExecute.unsafe_fastcall(self,edx);
    }

    void* __fastcall PlayerDrawDestroy(void* self,void* edx,unsigned flags)
    {
        AcquireSRWLockExclusive(&bodyCommandsLock);
        bodyCommands.erase(self);
        ReleaseSRWLockExclusive(&bodyCommandsLock);
        return playerDrawDestroy.unsafe_fastcall<void*>(self,edx,flags);
    }

    uint32_t __fastcall PedRender(void* self,void* edx,uint32_t shader,
        uint32_t flags,uint32_t alpha,uint32_t unknown)
    {
        // CE CPed virtual +0x8C builds entity draw commands (ret 0x10).
        // Restrict filtering to the two world streams; shared shadows and
        // auxiliary views retain the character. Never alter entity flags.
        if(buildingEye>=0 && buildingStage && VRCamera::HideLocalCharacter(self))
        {
            ++hiddenPlayerCalls[buildingEye];
            return 0;
        }
        const bool previous=buildingLocalBody;
        buildingLocalBody=buildingEye>=0 && buildingStage && VRCamera::ShowLocalBody(self);
        const auto result=pedRender.unsafe_fastcall<uint32_t>(self,edx,shader,flags,alpha,unknown);
        buildingLocalBody=previous;
        return result;
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

    bool __fastcall ObserveRequestFrustum(void* volume,void* edx,const float* center,float radius,int sidesOnly)
    {
        // CE B1DDC0 has a sole caller at AE8CF1 in the model request batch.
        // Return AL is the rejection flag; thiscall RET 0x0C verified live.
        const bool original=requestFrustum.unsafe_fastcall<bool>(volume,edx,center,radius,sidesOnly);
        if(original && ready && prepared && preparedFrame==gameFrame && currentVisibilityPhase &&
            *currentVisibilityPhase==scenePhase && requestProbeSamples<32)
        {
            bool visible[2]{};
            for(unsigned eye=0;eye<2;++eye)
            {
                std::array<VRMath::Vector,6> planes{};
                std::memcpy(planes.data(),eyeViewports[eye].mFrustumClipPlanes,sizeof(eyeViewports[eye].mFrustumClipPlanes));
                visible[eye]=!VRMath::SphereOutside(planes,center,radius);
            }
            if(visible[0] || visible[1])
            {
                ++requestProbeSamples;
                char line[240];
                sprintf_s(line,"VR request frustum mismatch frame=%u volume=%p center=(%.3f %.3f %.3f) radius=%.3f sidesOnly=%d eyeVisible=%u,%u nativeReject=1",
                    gameFrame,volume,center[0],center[1],center[2],radius,sidesOnly&255,visible[0],visible[1]);
                Log(line);
            }
        }
        return original;
    }

    void __cdecl ObserveOutdoorLod(uintptr_t* begin,uintptr_t* end,void* phase)
    {
        // CE AE7990: three cdecl arguments, {entity*,float} entries, plain RET.
        // Record before filtering: the native batch clears discarded entries.
        struct Sample {uintptr_t* entry; void* entity; float distance;};
        std::array<Sample,128> samples{};
        unsigned count=0;
        if(ready && prepared && preparedFrame==gameFrame && gameFrame%120==0 &&
            outdoorLodSamples<480 && phase && begin && end &&
            reinterpret_cast<uintptr_t>(end)>=reinterpret_cast<uintptr_t>(begin) &&
            reinterpret_cast<uintptr_t>(end)-reinterpret_cast<uintptr_t>(begin)<=65536)
        {
            if(outdoorLodFrame!=gameFrame) {outdoorLodFrame=gameFrame; outdoorLodCount=0;}
            const auto* camera=reinterpret_cast<const float*>(static_cast<char*>(phase)+0x910);
            for(auto* entry=begin;entry+1<end && count<samples.size() &&
                outdoorLodCount<128 && outdoorLodSamples+count<480;entry+=2)
            {
                auto* entity=reinterpret_cast<void*>(entry[0]);
                if(!entity || std::find(outdoorLodEntities,outdoorLodEntities+outdoorLodCount,entity)!=
                    outdoorLodEntities+outdoorLodCount) continue;
                const auto* object=static_cast<const char*>(entity);
                const auto* matrix=*reinterpret_cast<const char* const*>(object+0x20);
                const auto* position=reinterpret_cast<const float*>(matrix ? matrix+0x30 : object+0x10);
                const float dx=position[0]-camera[0],dy=position[1]-camera[1],dz=position[2]-camera[2];
                const float distance=std::sqrt(dx*dx+dy*dy+dz*dz);
                if(!std::isfinite(distance) || distance>150.f) continue;
                outdoorLodEntities[outdoorLodCount++]=entity;
                samples[count++]={entry,entity,distance};
            }
        }
        outdoorLod.unsafe_ccall(begin,end,phase);
        for(unsigned i=0;i<count;++i)
        {
            const auto& sample=samples[i];
            const auto* object=static_cast<const char*>(sample.entity);
            char line[400];
            sprintf_s(line,"VR outdoor LOD probe frame=%u entity=%p model=%d retained=%u rootDistance=%.3f nativeDistance=%.3f drawDistance=%.3f lodScale=%.3f drawable=%p parent=%p flags=%08X alpha=%u",
                gameFrame,sample.entity,*reinterpret_cast<const short*>(object+0x2E),
                sample.entry[0]!=0,sample.distance,*reinterpret_cast<const float*>(sample.entry+1),
                *reinterpret_cast<const float*>(object+0x50),*reinterpret_cast<const float*>(static_cast<char*>(phase)+0x934),
                *reinterpret_cast<void* const*>(object+0x34),*reinterpret_cast<void* const*>(object+0x4C),
                *reinterpret_cast<const unsigned*>(object+0x24),static_cast<unsigned char>(object[0x63]));
            Log(line);
            ++outdoorLodSamples;
        }
    }

    void ObserveOutdoorDecision(void* entity,void* phase,unsigned stage,bool rejected)
    {
        // AE77F0/AE8680 iterate the outdoor phase array directly. They bypass
        // AEBDF0 (the interior path), so observe their existing primitives too.
        if(probedEntity==entity || !ready || !prepared || preparedFrame!=gameFrame ||
            phase!=scenePhase || gameFrame%120!=0 || outdoorProbeSamples>=480) return;
        if(outdoorProbeFrame!=gameFrame)
        {outdoorProbeFrame=gameFrame; outdoorProbeCounts[0]=outdoorProbeCounts[1]=0;}
        auto* object=static_cast<char*>(entity);
        auto* scene=static_cast<char*>(phase);
        const auto* camera=reinterpret_cast<float*>(scene+0x910);
        const auto* matrix=*reinterpret_cast<const char**>(object+0x20);
        const auto* position=reinterpret_cast<const float*>(matrix ? matrix+0x30 : object+0x10);
        const float dx=position[0]-camera[0],dy=position[1]-camera[1],dz=position[2]-camera[2];
        auto& count=outdoorProbeCounts[stage];
        auto* entities=outdoorProbeEntities[stage];
        if(dx*dx+dy*dy+dz*dz>150.f*150.f || count==48 ||
            std::find(entities,entities+count,entity)!=entities+count) return;
        entities[count++]=entity;
        ++outdoorProbeSamples;
        char line[420];
        sprintf_s(line,"VR outdoor probe frame=%u stage=%s entity=%p model=%d rejected=%u drawable=%p parent=%p position=(%.3f %.3f %.3f) phaseBit=%u candidateMask=%08X rejectMask=%08X flags=%08X",
            gameFrame,stage ? "occlusion" : "entity",entity,*reinterpret_cast<short*>(object+0x2E),rejected,
            *reinterpret_cast<void**>(object+0x34),*reinterpret_cast<void**>(object+0x4C),position[0],position[1],position[2],
            *reinterpret_cast<unsigned*>(scene+0x938),*reinterpret_cast<unsigned*>(object+8),
            *reinterpret_cast<unsigned*>(object+0xC),*reinterpret_cast<unsigned*>(object+0x24));
        Log(line);
    }

    bool __cdecl ObserveEntityCull(void* entity,void* phase,void* bounds)
    {
        const bool result=entityCull.unsafe_ccall<bool>(entity,phase,bounds);
        if(entity==probedEntity && phase==probedPhase && result) probedRejects|=1;
        ObserveOutdoorDecision(entity,phase,0,result);
        return result;
    }

    bool __fastcall ObserveEntityOcclusion(void* entity,void* edx,void* phase)
    {
        const bool result=entityOcclusion.unsafe_fastcall<bool>(entity,edx,phase);
        if(entity==probedEntity && phase==probedPhase && result) probedRejects|=2;
        ObserveOutdoorDecision(entity,phase,1,result);
        return result;
    }

    int __cdecl ObserveLodDecision(void* entity,float* nativeDistance,int phaseIndex,void* phase)
    {
        // CE AEBDF0: four cdecl words and plain RET verified live. This
        // includes model classes 5/6, which bypass the inner AEBFF0 decision.
        // Preserve the native decision and calls; observe only a bounded set.
        const bool sample=ready && prepared && preparedFrame==gameFrame &&
            phase==scenePhase && gameFrame%120==0 && lodProbeSamples<480;
        if(!sample) return lodDecision.unsafe_ccall<int>(entity,nativeDistance,phaseIndex,phase);
        if(lodProbeFrame!=gameFrame)
        {lodProbeFrame=gameFrame; lodProbeEntityCount=0;}
        auto* object=static_cast<char*>(entity);
        auto* scene=static_cast<char*>(phase);
        const auto* camera=reinterpret_cast<float*>(scene+0x910);
        // Same entity position selection as CE's AEBDF0. Use real proximity,
        // because the native distance argument may include a LOD bias.
        const auto* matrix=*reinterpret_cast<const char**>(object+0x20);
        const auto* position=reinterpret_cast<const float*>(matrix ? matrix+0x30 : object+0x10);
        const float dx=position[0]-camera[0],dy=position[1]-camera[1],dz=position[2]-camera[2];
        if(dx*dx+dy*dy+dz*dz>150.f*150.f || lodProbeEntityCount==48 ||
            std::find(lodProbeEntities,lodProbeEntities+lodProbeEntityCount,entity)!=lodProbeEntities+lodProbeEntityCount)
            return lodDecision.unsafe_ccall<int>(entity,nativeDistance,phaseIndex,phase);
        lodProbeEntities[lodProbeEntityCount++]=entity;
        void* previousEntity=probedEntity;
        void* previousPhase=probedPhase;
        const auto previousRejects=probedRejects;
        probedEntity=entity; probedPhase=phase; probedRejects=0;
        const int result=lodDecision.unsafe_ccall<int>(entity,nativeDistance,phaseIndex,phase);
        const unsigned rejects=probedRejects;
        probedEntity=previousEntity; probedPhase=previousPhase; probedRejects=previousRejects;
        if(result>=0 && result<4)
        {
            ++lodProbeSamples;
            auto* viewport=reinterpret_cast<rage::grcViewport*>(scene+0x4A0);
            char line[600];
            sprintf_s(line,"VR entity probe frame=%u entity=%p model=%d flags=%08X drawable=%p parent=%p position=(%.3f %.3f %.3f) rootDistance=%.3f drawDistance=%.3f result=%d rejects=%u camera=(%.3f %.3f %.3f) forward=(%.3f %.3f %.3f) lodScale=%.3f far=%.3f occlusion=%d,%d",
                gameFrame,entity,*reinterpret_cast<short*>(object+0x2E),*reinterpret_cast<unsigned*>(object+0x24),
                *reinterpret_cast<void**>(object+0x34),*reinterpret_cast<void**>(object+0x4C),position[0],position[1],position[2],std::sqrt(dx*dx+dy*dy+dz*dz),*reinterpret_cast<float*>(object+0x50),result,rejects,
                camera[0],camera[1],camera[2],viewport->mCameraMatrix[2][0],viewport->mCameraMatrix[2][1],viewport->mCameraMatrix[2][2],
                *reinterpret_cast<float*>(scene+0x934),viewport->mFarClip,
                *reinterpret_cast<int*>(scene+0x8F8),*reinterpret_cast<int*>(scene+0x8FC));
            Log(line);
        }
        return result;
    }

    int __fastcall SphereCull(rage::grcViewport* self,void* edx,float x,float y,float z,float radius,float* distance)
    {
        const int original=sphereCull.unsafe_fastcall<int>(self,edx,x,y,z,radius,distance);
        if(!ready || !prepared || preparedFrame!=gameFrame) return original;
        int owner=-1;
        if(scenePhase && self==reinterpret_cast<rage::grcViewport*>(static_cast<char*>(scenePhase)+0xB0)) owner=0;
        else if(scenePhase && self==reinterpret_cast<rage::grcViewport*>(static_cast<char*>(scenePhase)+0x4A0)) owner=1;
        else if(worldParent && self==reinterpret_cast<rage::grcViewport*>(static_cast<char*>(worldParent)+0x10)) owner=2;
        if(owner<0)
        {
            if(observedSphereViewportCount<16 &&
                std::find(std::begin(observedSphereViewports),std::end(observedSphereViewports),self)==std::end(observedSphereViewports))
            {
                observedSphereViewports[observedSphereViewportCount++]=self;
                char line[220];
                sprintf_s(line,"VR sphere other viewport=%p camera=(%.3f %.3f %.3f) scene=%p parent=%p frame=%u",
                    self,self->mViewInverseMatrix[3][0],self->mViewInverseMatrix[3][1],self->mViewInverseMatrix[3][2],scenePhase,worldParent,gameFrame);
                Log(line);
            }
            return original;
        }
        ++sphereQueries[owner];
        alignas(16) rage::grcViewport visibility=*self;
        std::memcpy(visibility.mFrustumClipPlanes,visibilityPlanes.data(),sizeof(visibility.mFrustumClipPlanes));
        // The CE optional output is plane[0].dot(center)+radius. Plane zero
        // in our world AABB is an X boundary, not the engine's near plane.
        // Keep union visibility separate from the headset near-plane distance.
        int result=sphereCull.unsafe_fastcall<int>(&visibility,edx,x,y,z,radius,nullptr);
        if(distance)
        {
            float nearestVisible=-INFINITY;
            for(const auto& eye:eyeViewports)
            {
                const auto& nearPlane=eye.mFrustumClipPlanes[0];
                nearestVisible=std::max(nearestVisible,nearPlane.x*x+nearPlane.y*y+nearPlane.z*z+nearPlane.w+radius);
            }
            *distance=nearestVisible;
            // CE's output-distance branch returns 1 for any accepted sphere.
            if(result) result=1;
        }
        if(!original && result) ++sphereCorrections[owner];
        return result;
    }

    void __cdecl VisibilityRaster(float* polygon,unsigned count,void* data,void* cell,
        int positiveX,int positiveY,int swapAxes,int sorted)
    {
        if(ready && visibilityRasterObservations<24)
        {
            ++visibilityRasterObservations;
            char line[240];
            sprintf_s(line,"VR grid observation phase=%p scene=%p prepared=%u frame=%u preparedFrame=%u cell=%p fine=%p coarse=%p count=%u",
                currentVisibilityPhase ? *currentVisibilityPhase : nullptr,scenePhase,prepared,gameFrame,preparedFrame,cell,fineGridCell,coarseGridCell,count);
            Log(line);
        }
        if(ready && prepared && preparedFrame==gameFrame && currentVisibilityPhase &&
            *currentVisibilityPhase==scenePhase &&
            (cell==fineGridCell || cell==coarseGridCell || (streamingGridCell && cell==streamingGridCell)))
        {
            const bool streaming=cell==streamingGridCell;
            const float scale=streaming ? .002f : cell==fineGridCell ? .02f : .005f;
            const float origin=streaming ? 6.f : cell==fineGridCell ? 60.f : 15.f;
            float lowX=-visibilityPlanes[0][3]*scale+origin;
            float highX=visibilityPlanes[1][3]*scale+origin;
            float lowY=-visibilityPlanes[2][3]*scale+origin;
            float highY=visibilityPlanes[3][3]*scale+origin;
            if(cell==fineGridCell)
            {
                // CE schedules at most 199 shared grid jobs (AE28D0).
                // A far-clip AABB fills all 48 fine rows: 240 jobs before
                // coarse work. Native AE95E0 limits its detailed footprint
                // to about 300m; retain 600m here for the current LOD scale2.
                constexpr float detailRadius=600.f;
                const auto& position=eyeViewports[0].mViewInverseMatrix[3];
                lowX=std::max(lowX,(position[0]-detailRadius)*scale+origin);
                highX=std::min(highX,(position[0]+detailRadius)*scale+origin);
                lowY=std::max(lowY,(position[1]-detailRadius)*scale+origin);
                highY=std::min(highY,(position[1]+detailRadius)*scale+origin);
            }
            float bounds[]{lowX,lowY,highX,lowY,highX,highY,lowX,highY};
            // The broad-phase grid uses camera corners, not viewport clip
            // planes. Rasterize the same conservative eye union there before
            // the per-entity tests; preserve cell callbacks and phase bits.
            ++visibilityRasterQueries;
            if(streaming) ++streamingRasterQueries;
            visibilityRaster.unsafe_ccall(bounds,4u,data,cell,1,1,0,0);
            return;
        }
        visibilityRaster.unsafe_ccall(polygon,count,data,cell,positiveX,positiveY,swapAxes,sorted);
    }

    bool __fastcall CameraVolumeCull(void* self,void* edx,const float* center,float radius)
    {
        const bool original=cameraVolumeCull.unsafe_fastcall<bool>(self,edx,center,radius);
        // Interior traversal builds a shared four-plane volume from the
        // ordinary camera, then queries it before the entity sphere test.
        // Scope by the verified active phase rather than guessing volume ownership.
        // Auxiliary phases retain their original portal/camera-volume queries.
        if(ready && prepared && preparedFrame==gameFrame && scenePhase && currentVisibilityPhase &&
            *currentVisibilityPhase==scenePhase)
        {
            const bool outside=VRMath::SphereOutside(visibilityPlanes,center,radius);
            if(original && !outside) ++cameraVolumeCorrections;
            return outside;
        }
        return original;
    }

    void __cdecl HeadsetFineCell(int x,int y,void* phase,void*)
    {
        // D62DC0 supplies X/Y, explicit data and the global active phase.
        // AE95B0 reads only the FOURTH argument's +938 phase index. Use the
        // registered main scene explicitly without changing the global owner.
        using Cell=void(__cdecl*)(int,int,void*,void*);
        reinterpret_cast<Cell>(fineGridCell)(x,y,nullptr,phase);
    }

    void __fastcall SceneVisibility(void* self,void* edx)
    {
        // CE 0x00D689E0 constructs occlusion from +0xB0 before 0x00AE4880
        // caches position/direction from +0x4A0 for LOD and traversal. This
        // happens before BuildFrame's per-eye viewport replacements.
        if(!ready || !prepared || preparedFrame!=gameFrame || self!=scenePhase ||
            !currentVisibilityPhase || *currentVisibilityPhase!=self)
        {sceneVisibility.unsafe_fastcall(self,edx); return;}
        alignas(16) rage::grcViewport saved[2]{};
        for(unsigned slot=0;slot<2;++slot)
        {
            auto* viewport=reinterpret_cast<rage::grcViewport*>(static_cast<char*>(self)+(slot ? 0x4A0 : 0xB0));
            std::memcpy(&saved[slot],viewport,sizeof(*viewport));
            std::memcpy(viewport,&eyeViewports[0],sizeof(*viewport));
            std::memcpy(viewport->mFrustumClipPlanes,visibilityPlanes.data(),sizeof(viewport->mFrustumClipPlanes));
        }
        sceneVisibility.unsafe_fastcall(self,edx);
        if(fineGridCell && visibilityRaster &&
            *reinterpret_cast<unsigned*>(static_cast<char*>(self)+0x938)<32)
        {
            // Native fine traversal belongs to another phase, so the scoped
            // raster hook above cannot supply the main headset footprint.
            // Add its bits after native registration, preserving all existing
            // shadow/auxiliary marks and the native fine-cell implementation.
            constexpr float scale=.02f,origin=60.f;
            constexpr float detailRadius=600.f;
            const auto& position=eyeViewports[0].mViewInverseMatrix[3];
            const float lowX=std::max(-visibilityPlanes[0][3],position[0]-detailRadius)*scale+origin;
            const float highX=std::min(visibilityPlanes[1][3],position[0]+detailRadius)*scale+origin;
            const float lowY=std::max(-visibilityPlanes[2][3],position[1]-detailRadius)*scale+origin;
            const float highY=std::min(visibilityPlanes[3][3],position[1]+detailRadius)*scale+origin;
            float bounds[]{lowX,lowY,highX,lowY,highX,highY,lowX,highY};
            visibilityRaster.unsafe_ccall(bounds,4u,self,HeadsetFineCell,1,1,0,0);
            if(++headsetFineRasterQueries==1 || gameFrame%120==0)
            {
                char line[160];
                sprintf_s(line,"VR headset fine-grid pass frame=%u phase=%p phaseBit=%u count=%u",
                    gameFrame,self,*reinterpret_cast<unsigned*>(static_cast<char*>(self)+0x938),headsetFineRasterQueries);
                Log(line);
            }
        }
        for(unsigned slot=0;slot<2;++slot)
            std::memcpy(static_cast<char*>(self)+(slot ? 0x4A0 : 0xB0),&saved[slot],sizeof(saved[slot]));
        ++sceneVisibilityCorrections;
    }

    void __fastcall SceneSetup(void* self,void* edx,void* parent)
    {
        sceneSetup.unsafe_fastcall(self,edx,parent);
        setupThisFrame=true;
        scenePhase=self;
        worldParent=parent;
        prepared=false;
        if(!ready || (maxFrames && stereoFrames>=maxFrames) || *CTimer::ms_bUserPause || *CTimer::ms_bScriptPause) return;
        if(!OpenXRVR::GetEyeViews(xrViews,viewTime))
        {
            if(!waitingForFreshViews) Log("Fresh OpenXR poses unavailable; retaining ordinary scene construction");
            waitingForFreshViews=true;
            VRCamera::Recenter(); return;
        }
        if(waitingForFreshViews) Log("Fresh OpenXR poses resumed; stereo scene construction restored");
        waitingForFreshViews=false;
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
        // Setup copies the shared parent camera into main and auxiliary phase
        // viewports before SceneSetup. Revalidate the previous world's owner
        // against this renderer's current camera inventory before using it.
        auto* renderer=static_cast<char*>(self);
        const auto count=*reinterpret_cast<uint16_t*>(renderer+0xC);
        auto** parents=*reinterpret_cast<void***>(renderer);
        auto* indices=*reinterpret_cast<int**>(renderer+0x8);
        bool currentParent=worldParent && *reinterpret_cast<void**>(renderer+0x14)==worldParent;
        if(worldParent && parents && indices && count<=64)
            for(unsigned i=0;i<count;++i)
                if(indices[i]>=0 && indices[i]<256 && parents[indices[i]]==worldParent) currentParent=true;
        alignas(16) rage::grcViewport saved{},generated[2]{};
        auto* shared=currentParent ? reinterpret_cast<rage::grcViewport*>(static_cast<char*>(worldParent)+0x10) : nullptr;
        OpenXRVR::EyeView views[2]{};
        long long time=0;
        const bool override=shared && ready && VRCamera::IsFirstPersonActive() &&
            !*CTimer::ms_bUserPause && !*CTimer::ms_bScriptPause &&
            OpenXRVR::GetEyeViews(views,time) && VRCamera::Generate(*shared,views,generated) &&
            VRCamera::IsFirstPersonActive();
        if(override)
        {
            std::memcpy(&saved,shared,sizeof(saved));
            std::memcpy(shared,&generated[0],sizeof(*shared));
            ++sharedCameraCorrections;
        }
        renderFrame.unsafe_fastcall(self,edx);
        if(override) std::memcpy(shared,&saved,sizeof(saved));
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
        buildingStage=0;
        if(commandPool)
        {
            const auto bytes=commandPool[7];
            if(bytes>commandBufferPeak.load()) commandBufferPeak=bytes;
            if(reinterpret_cast<unsigned char*>(commandPool)[8+commandPool[5]])
            {
                if(++commandBufferOverflows==1) Log("Native render command buffer overflowed; scene exceeds stereo capacity");
            }
        }
        phaseArray=originalArray;
        phaseCount=originalCount;
        for(unsigned phase=0;phase<3;++phase)
            for(unsigned slot=0;slot<2;++slot)
                std::memcpy(static_cast<char*>(phases[phase])+(slot ? 0x4A0 : 0xB0),&saved[phase][slot],sizeof(rage::grcViewport));
        ++stereoFrames;
        char line[160];
        sprintf_s(line,"built frame=%u two world lists displayTime=%lld process counter before=%u after=%u",gameFrame,viewTime,preparedFrame,gameFrame);
        if(stereoFrames==1 || stereoFrames==maxFrames || gameFrame%120==0) Log(line);
        if(gameFrame%120==0)
        {
            sprintf_s(line,"first-person filtered local character commands eye0=%u eye1=%u",hiddenPlayerCalls[0],hiddenPlayerCalls[1]);
            Log(line);
            sprintf_s(line,"main-world camera-volume rejections corrected=%u",cameraVolumeCorrections);
            Log(line);
            sprintf_s(line,"VR main-world visibility camera preparations=%u",sceneVisibilityCorrections);
            Log(line);
            sprintf_s(line,"VR shared-world camera preparations=%u",sharedCameraCorrections);
            Log(line);
            sprintf_s(line,"VR main-world streaming grid rasterizations=%u",streamingRasterQueries);
            Log(line);
            sprintf_s(line,"VR main-world visibility grid rasterizations=%u",visibilityRasterQueries);
            Log(line);
            sprintf_s(line,"VR sphere queries scene=%u secondary=%u parent=%u corrected=%u,%u,%u",
                sphereQueries[0],sphereQueries[1],sphereQueries[2],sphereCorrections[0],sphereCorrections[1],sphereCorrections[2]);
            Log(line);
        }
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

bool VRStereo::RestartLodProbe()
{
    if(!enabled || !ready || !lodDecision || !requestFrustum) return false;
    lodProbeRequested=true;
    return true;
}

std::string VRStereo::Status()
{
    char line[400];
    AcquireSRWLockShared(&modelReferencesLock);
    sprintf_s(line,"{\"enabled\":%s,\"ready\":%s,\"builtFrames\":%u,\"frameBudget\":%u,\"continuous\":%s,\"modelReferencePeak\":%u,\"duplicateModelReferences\":%u,\"commandBufferCapacity\":%u,\"commandBufferPeak\":%u,\"commandBufferOverflows\":%u}",
        enabled ? "true" : "false",ready ? "true" : "false",stereoFrames.load(),maxFrames,maxFrames ? "false" : "true",
        retainedModelPeak,duplicateModelReferences,commandBufferCapacity,commandBufferPeak.load(),commandBufferOverflows.load());
    ReleaseSRWLockShared(&modelReferencesLock);
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
    // CE 8DEB60 owns two command buffers, each 2 MiB plus 10,000 bytes of
    // native padding. Repeating world commands overflowed this arena in #029:
    // the executor 8DE060 skips a buffer marked overflowed. Grow allocation
    // and every matching producer/reader bound together before initialization.
    auto commandInit=hook::pattern("56 68 10 27 20 00 8B F1 E8 ? ? ? ? 68 10 27 20 00 89 06 E8");
    auto commandAlloc=hook::pattern("53 56 57 8B 7C 24 10 FF 74 24 14 8B F7 F7 DE 83 E6 0F B9 ? ? ? ? 8D 04 3E 50 E8");
    auto commandCopy=hook::pattern("53 55 8B 6C 24 10 56 57 8B DD F7 DB 8B F9 83 E3 0F 8B 47 1C 03 DD 83 C0 10 03 C3 3D 00 00 20 00");
    auto commandCheck=hook::pattern("8B 41 1C 03 44 24 04 3D 00 00 20 00 72 16 8B 41 14");
    auto commandRead=hook::pattern("8D 04 16 5F 5E 3D 00 00 20 00 73 0E 8B 41 10");
    if(commandInit.size()!=1 || commandAlloc.size()!=1 || commandCopy.size()!=1 ||
        commandCheck.size()!=1 || commandRead.size()!=1 ||
        *commandAlloc.get_first<uint32_t>(0x42)!=0x200000)
    {Log("Stereo command buffer safety signatures unavailable; retaining desktop"); return;}
    auto modelList=hook::pattern("BD 00 40 00 00 8D 9B 00 00 00 00 6A 00 E8 ? ? ? ? 68 00 80 00 00 89 04 BB E8 ? ? ? ? 6A 40 89 46 DA 66 89 6E E0");
    auto modelAppend=hook::pattern("56 57 8B 7C 24 0C 8B F1 8B 0C BD ? ? ? ? E8 ? ? ? ? 8B 86 40 20 00 00 0F B7 94 C6 50 20 00 00");
    if(modelList.size()!=1 || modelAppend.size()!=1)
    {Log("Stereo model lifetime list safety signatures unavailable; retaining desktop"); return;}
    commandPool=*commandAlloc.get_first<uint32_t*>(19);
    injector::WriteMemory<uint32_t>(commandInit.get_first(2),commandBufferCapacity+10000,true);
    injector::WriteMemory<uint32_t>(commandInit.get_first(14),commandBufferCapacity+10000,true);
    injector::WriteMemory<uint32_t>(commandAlloc.get_first(0x42),commandBufferCapacity,true);
    injector::WriteMemory<uint32_t>(commandCopy.get_first(28),commandBufferCapacity,true);
    injector::WriteMemory<uint32_t>(commandCheck.get_first(8),commandBufferCapacity,true);
    injector::WriteMemory<uint32_t>(commandRead.get_first(6),commandBufferCapacity,true);
    Log("Native stereo command buffers expanded from 2 MiB to 8 MiB; native ownership retained");
    // Native 9422B0 allocates four 0x4000-entry model-ID lists. Broader
    // visibility plus stereo exceeded that bound and 942000 overwrote a live
    // input device (hardware write watch, #029). Preserve native ownership,
    // allocator and ushort consumer; expand storage before native initialization.
    injector::WriteMemory<uint32_t>(modelList.get_first(1),0xFFFF,true);
    injector::WriteMemory<uint32_t>(modelList.get_first(19),0x1FFFE,true);
    retainModel=safetyhook::create_inline(modelAppend.get_first(),RetainModel);
    if(!retainModel) {Log("Stereo model lifetime retention hook unavailable"); return;}
    Log("Stereo model lifetime lists expanded to 65535 entries; distinct-model holds enabled");
    sceneSetup=safetyhook::create_inline(a.get_first(),SceneSetup);
    drawSetup=safetyhook::create_inline(b.get_first(),DrawSetup);
    renderFrame=safetyhook::create_inline(c.get_first(),RenderFrame);
    buildFrame=safetyhook::create_inline(d.get_first(),BuildFrame);
    if(!sceneSetup || !drawSetup || !renderFrame || !buildFrame) {Log("Stereo scheduler hooks unavailable"); return;}
    if(ini.ReadInteger("VR","ExperimentalFirstPerson",0))
    {
        auto ped=hook::pattern("83 EC 14 53 55 8B 6C 24 20 8B D9 83 FD 01 75 0D 80 3D ? ? ? ? 00");
        auto volume=hook::pattern("8B 44 24 04 F3 0F 10 41 10 F3 0F 10 18 F3 0F 10 50 04 F3 0F 5C 19");
        auto sphere=hook::pattern("55 8B EC 83 E4 F8 51 8B 45 18 56 8B F1 0F 57 F6 85 C0");
        auto raster=hook::pattern("83 EC 54 A1 ? ? ? ? 33 C4 89 44 24 50 80 7C 24 74 00");
        auto activePhase=hook::pattern("8B 0D ? ? ? ? 66 A1 ? ? ? ? 56 BA FF FF 00 00 8D B1 B0 00 00 00");
        auto fineCell=hook::pattern("8B 4C 24 10 B8 01 00 00 00 8B 89 38 09 00 00 D3 E0 50 FF 74 24 0C");
        auto coarseCell=hook::pattern("8B 54 24 04 85 D2 78 ? 56 8B 74 24 0C 85 F6 78 ? 83 FA 1E");
        auto streamingCell=hook::pattern("56 8B 74 24 0C 57 8B 7C 24 0C 85 FF 78 1C 85 F6 78 16 83 FF 0C 7D 11 83 FE 0C 7D 0C 6A 00 56 57");
        auto visibility=hook::pattern("55 8B EC 83 E4 F0 83 EC 3C A1 ? ? ? ? F3 0F 10 0D ? ? ? ? F3 0F 10 80 14 01 00 00 F3 0F 10 90 10 01 00 00");
        if(raster.size()==1 && activePhase.size()==1 && fineCell.size()==1 && coarseCell.size()==1)
        {
            currentVisibilityPhase=*activePhase.get_first<void**>(2);
            fineGridCell=fineCell.get_first();
            coarseGridCell=coarseCell.get_first();
            if(streamingCell.size()==1) streamingGridCell=streamingCell.get_first();
            visibilityRaster=safetyhook::create_inline(raster.get_first(),VisibilityRaster);
        }
        Log(visibilityRaster ? "VR main-world visibility grid correction installed" : "VR visibility grid correction unavailable");
        if(visibility.size()==1 && currentVisibilityPhase)
            sceneVisibility=safetyhook::create_inline(visibility.get_first(),SceneVisibility);
        Log(sceneVisibility ? "VR main-world visibility camera preparation installed" : "VR visibility camera preparation unavailable");
        if(ini.ReadInteger("DEVELOPMENT","ProbeFirstPersonAnchor",0))
        {
            auto lod=hook::pattern("56 8B 74 24 08 57 8B 7C 24 18 8B 46 24 83 7F 3C 02 74 0F A9 00 00 01 00");
            auto reject=hook::pattern("55 8B EC 83 E4 F0 83 EC 48 56 8B 75 0C 57 83 7E 3C 01");
            auto occlude=hook::pattern("55 8B EC 83 E4 F0 83 EC 78 56 57 8B F9 F7 47 24 00 00 00 08");
            auto request=hook::pattern("8B 44 24 04 80 7C 24 0C 00 F3 0F 10 28 F3 0F 10 70 04 F3 0F 10 78 08 BA 06 00 00 00");
            auto batch=hook::pattern("83 EC 1C E8 ? ? ? ? 84 C0 8B 44 24 28 0F 94 44 24 1B F3 0F 10 80 30 09 00 00");
            if(lod.size()==1 && reject.size()==1 && occlude.size()==1)
            {
                entityCull=safetyhook::create_inline(reject.get_first(),ObserveEntityCull);
                entityOcclusion=safetyhook::create_inline(occlude.get_first(),ObserveEntityOcclusion);
                if(entityCull && entityOcclusion) lodDecision=safetyhook::create_inline(lod.get_first(),ObserveLodDecision);
            }
            if(request.size()==1) requestFrustum=safetyhook::create_inline(request.get_first(),ObserveRequestFrustum);
            if(batch.size()==1) outdoorLod=safetyhook::create_inline(batch.get_first(),ObserveOutdoorLod);
            Log(outdoorLod ? "VR outdoor LOD batch observation installed" : "VR outdoor LOD batch observation unavailable");
            Log(requestFrustum ? "VR asset-request frustum observation installed" : "VR request frustum observation unavailable");
            Log(lodDecision ? "VR bounded native LOD decision observation installed" : "VR native LOD observation unavailable");
        }
        if(sphere.size()==1) sphereCull=safetyhook::create_inline(sphere.get_first(),SphereCull);
        Log(sphereCull ? "VR world sphere-query correction installed" : "VR world sphere-query correction unavailable");
        if(volume.size()==1) cameraVolumeCull=safetyhook::create_inline(volume.get_first(),CameraVolumeCull);
        Log(cameraVolumeCull ? "VR main-world camera-volume correction installed" : "VR camera-volume correction unavailable");
        if(ped.size()==1) pedRender=safetyhook::create_inline(ped.get_first(),PedRender);
        VRCamera::SetVisibilityAvailable(static_cast<bool>(pedRender));
        if(ini.ReadInteger("DEVELOPMENT","DebugFirstPersonBody",0))
        {
            auto ctor=hook::pattern("53 8B D9 56 8B 43 04 C7 03 ? ? ? ? 33 05 ? ? ? ? 57 25 FF 3F 00 00");
            auto execute=hook::pattern("51 56 8B F1 57 0F B7 46 08 8B 3C 85 ? ? ? ? 8B 47 08 85 C0");
            auto destroy=hook::pattern("56 8B F1 8D 4E 20 C7 06 ? ? ? ? E8 ? ? ? ? F6 44 24 08 01 C7 06 ? ? ? ? 74 ? 56 E8");
            if(ctor.size()==1 && execute.size()==1 && destroy.size()==1)
            {
                playerDrawCtor=safetyhook::create_inline(ctor.get_first(),PlayerDrawCtor);
                playerDrawExecute=safetyhook::create_inline(execute.get_first(),PlayerDrawExecute);
                playerDrawDestroy=safetyhook::create_inline(destroy.get_first(),PlayerDrawDestroy);
            }
            const bool bodyReady=playerDrawCtor && playerDrawExecute && playerDrawDestroy;
            VRCamera::SetHeadlessBodyAvailable(bodyReady);
            Log(bodyReady ? "Debug headless body filter installed" : "Debug body unavailable; whole-player hiding retained");
        }
        Log(pedRender ? "First-person per-eye character filter installed" : "Character filter unavailable; first-person camera disabled");
    }
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
        if(lodProbeRequested.exchange(false))
        {
            lodProbeSamples=0;
            requestProbeSamples=0;
            outdoorProbeSamples=0;
            outdoorProbeFrame=0;
            outdoorLodSamples=0;
            outdoorLodFrame=0;
            lodProbeFrame=0;
            Log("VR nearby LOD probe armed: maximum480 records, radius150");
        }
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
