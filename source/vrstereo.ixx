module;
#include <common.hxx>
#include <wrl/client.h>
#include <d3dx9shader.h>
#include "includes/vrmath.hpp"
#include "includes/vrscope.hpp"
#include "includes/vrmodelrefs.hpp"
#include "includes/vrconstantprobe.hpp"
#include "includes/vrtexturerouting.hpp"
#include "includes/vrvertex.hpp"
#include "includes/vrbillboardshader.hpp"
#include "includes/vrrigidinstanceshader.hpp"
#include "includes/vrrigidinstance.hpp"
#include "includes/vrmeshpacketprobe.hpp"
#include "includes/vrconstantbatch.hpp"
#include <intrin.h>
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
import vrprofile;
import vrcamera;
import d3dx9_43;
import vrhud;
import vrhands;
import vrreload;

export namespace VRStereo
{
    void Install();
    void Shutdown(); // Render callback thread, never DllMain.
    bool RestartExperiment();
    bool RequestScopeProbe();
    bool RestartLodProbe();
    bool SetVisibilityTest(bool value);
    bool RequestSphereClassificationProbe();
    bool RequestVertexCallerProbe();
    bool RequestModelCommandProbe();
    bool SetVertexEmissionTest(bool value);
    bool SetBillboardInstancingTest(bool value);
    bool SetRigidInstancingTest(bool value);
    bool RequestMeshPacketProbe();
    bool RequestMeshOrderProbe();
    bool RequestMeshReplayProbe();
    bool RequestMeshBenchmarkProbe();
    bool RequestLiveMeshProbe(bool replace);
    bool RequestCpuOmission(unsigned mode);
    std::string CpuOmissionStatus();
    bool SetConstantBatchTest(bool value);
    bool SetSphereClassificationTest(bool value);
    std::string Status();
}

namespace
{
    using Microsoft::WRL::ComPtr;
    struct ConstantProbe
    {
        VRConstantProbe::Observer observer;
        VRProfile::Total time{};
        unsigned long long calls=0,vectors=0,repeatedCandidates=0;
        void Reset() {observer.Reset(); time={}; calls=vectors=repeatedCandidates=0;}
        void Record(VRProfile::Stamp stamp,UINT start,const float* data,UINT count,HRESULT result)
        {
            if(!stamp.epoch) return;
            if(time.epoch!=stamp.epoch) {Reset(); time.epoch=stamp.epoch;}
            VRProfile::Accumulate(time,stamp);
            ++calls; vectors+=count;
            if(SUCCEEDED(result) && observer.Observe(start,data,count)) ++repeatedCandidates;
        }
    };
    thread_local ConstantProbe constantProbes[2][2];
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
        VRTextureRouting::Cache<IDirect3DBaseTexture9*> textureRoutes;
        struct GPUTiming
        {
            ComPtr<IDirect3DQuery9> begin,end,frequency,disjoint;
            unsigned epoch=0;
            bool pending=false;
        };
        std::array<std::array<GPUTiming,8>,2> gpuTimings;
        int gpuActive[2]{-1,-1};
        bool gpuInitialized=false,gpuSupported=false;
        std::vector<Target> targets;
        ComPtr<IDirect3DSurface9> originalBack;
        ComPtr<IDirect3DTexture9> finalTexture[2];
        ComPtr<IDirect3DSurface9> finalSurface[2];
        ComPtr<IDirect3DSurface9> worldDepth[2];
        D3DVIEWPORT9 worldViewport[2]{};
        HANDLE shared[2]{};
        std::vector<ComPtr<IDirect3DVertexShader9>> observedShaders;
        ComPtr<IDirect3DVertexShader9> symmetricRayShader,asymmetricRayShader;
        std::vector<ComPtr<IDirect3DPixelShader9>> observedPixelShaders,cascadeShaders;
    };
    // Explicit render-thread ownership; process detach must not invoke driver
    // cleanup under the loader lock. The bridge/reset calls Shutdown instead.
    Resources* resources=nullptr;
    Resources* scopeResources=nullptr;
    Resources* mainResources=nullptr;
    ComPtr<IDirect3DPixelShader9> scopeLensShader;
    ComPtr<IDirect3DVertexShader9> scopeLensVertexShader;
    VRScope::Settings scopeSettings{};
    VRMath::Matrix scopeCamera{};
    VRMath::Matrix queuedScopeGun{};
    bool queuedScopeGunValid=false;
    unsigned scopePoseSamples=0;
    rage::grcViewport scopeViewport{};
    bool scopeActive=false,buildingScope=false;
    float worldTargetAspect=1.f;
    unsigned scopeConfigRevision=~0u;
    std::atomic<unsigned> scopeBuilds=0,scopeRenders=0,scopeLensDraws=0,scopeFailures=0;
    unsigned scopeCompletedFrame=~0u;
    std::atomic<bool> scopeProbeRequested=false;
    std::atomic<bool> scopeCaptureRequested=false;
    unsigned scopeProbeFrames=0;
    VRMath::Matrix completedScopeCamera{};
    void Log(const char* message);

    void ReadScopeSettings()
    {
        const auto revision=VRReloadRuntime::ConfigRevision();
        if(revision==scopeConfigRevision) return;
        scopeConfigRevision=revision;
        CIniReader ini("VRWeapons/psg1.ini");
        VRScope::Settings next;
        bool valid=true;
        auto read=[&](const char* key,float fallback,float low,float high)
        {
            try
            {
                const auto text=ini.ReadString("Scope",key,std::to_string(fallback));
                size_t end=0;const float value=std::stof(text,&end);
                if(text.find_first_not_of(" \t\r\n",end)!=std::string::npos || !std::isfinite(value) || value<low || value>high)
                    {char message[256];sprintf_s(message,"Scope config rejected: %s=%s; allowed %.3f..%.3f. Previous settings retained.",key,text.c_str(),low,high);Log(message);valid=false;return fallback;}
                return value;
            }
            catch(...) {char message[160];sprintf_s(message,"Scope config rejected: invalid number for %s. Previous settings retained.",key);Log(message);valid=false;return fallback;}
        };
        next.enabled=ini.ReadInteger("Scope","Enabled",1)!=0;
        next.depthTest=ini.ReadInteger("Scope","DepthTest",1)!=0;
        next.debugLens=ini.ReadInteger("Scope","DebugLens",0)!=0;
        next.eyepiece[0]=read("EyepieceX",next.eyepiece[0],-.5f,.5f);
        next.eyepiece[1]=read("EyepieceY",next.eyepiece[1],-.2f,.2f);
        next.eyepiece[2]=read("EyepieceZ",next.eyepiece[2],-.2f,.4f);
        next.radius=read("LensRadius",next.radius,.005f,.06f);
        next.surfaceOffset=read("LensSurfaceOffset",next.surfaceOffset,0,.05f);
        next.eyeRelief=read("EyeRelief",next.eyeRelief,.04f,.5f);
        // A broad eye box is useful during live lens-placement tuning.
        next.eyeBox=read("EyeBoxRadius",next.eyeBox,.01f,1.5f);
        next.verticalFov=read("VerticalFovDegrees",next.verticalFov,1,60);
        if(valid) scopeSettings=next;else ++scopeFailures;
    }

    bool CreateScopeResources(IDirect3DDevice9* device)
    {
        if(scopeResources) return true;
        if(!resources) return false;
        // One world view, no additional OpenXR swapchain or shared handle.
        // Keep native dimensions until all fullscreen screen-size constants
        // can be scaled together; only render while the eyepiece is in use.
        auto owned=std::make_unique<Resources>();
        owned->originalBack=resources->originalBack;
        D3DSURFACE_DESC back{};
        if(FAILED(owned->originalBack->GetDesc(&back)) ||
            FAILED(device->CreateTexture(back.Width,back.Height,1,D3DUSAGE_RENDERTARGET,
                D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,&owned->finalTexture[0],nullptr)) ||
            FAILED(owned->finalTexture[0]->GetSurfaceLevel(0,&owned->finalSurface[0]))) return false;
        // The routing cache recognizes both stereo slots, including every
        // depth mip. Alias its unused slot to the single optical view; this
        // adds references, not another allocation or another rendered camera.
        owned->finalTexture[1]=owned->finalTexture[0];owned->finalSurface[1]=owned->finalSurface[0];
        for(const auto& source:resources->targets)
        {
            Target target;target.name=source.name;target.original=source.original;
            target.originalSurface=source.originalSurface;target.originalMips=source.originalMips;
            D3DSURFACE_DESC desc{};
            if(FAILED(target.original->GetLevelDesc(0,&desc)) ||
                FAILED(device->CreateTexture(desc.Width,desc.Height,target.original->GetLevelCount(),desc.Usage,
                    desc.Format,D3DPOOL_DEFAULT,&target.texture[0],nullptr)) ||
                FAILED(target.texture[0]->GetSurfaceLevel(0,&target.surface[0]))) return false;
            target.eyeMips[0].resize(target.originalMips.size());
            if(desc.Usage&D3DUSAGE_RENDERTARGET)
                device->StretchRect(target.originalSurface.Get(),nullptr,target.surface[0].Get(),nullptr,D3DTEXF_NONE);
            for(unsigned level=0;level<target.originalMips.size();++level)
            {
                if(FAILED(target.texture[0]->GetSurfaceLevel(level+1,&target.eyeMips[0][level]))) return false;
                if(desc.Usage&D3DUSAGE_RENDERTARGET)
                    device->StretchRect(target.originalMips[level].Get(),nullptr,target.eyeMips[0][level].Get(),nullptr,D3DTEXF_NONE);
            }
            target.texture[1]=target.texture[0];target.surface[1]=target.surface[0];
            target.eyeMips[1]=target.eyeMips[0];
            owned->targets.push_back(std::move(target));
        }
        scopeResources=owned.release();return true;
    }

    // No polling loop or FLUSH: consume completed older samples and skip a
    // full ring. Query allocation happens once, during the first capture.
    void GPUTime(IDirect3DDevice9* device,int eye,bool begin)
    {
        if(!resources) return;
        auto& state=*resources;
        if(!begin)
        {
            if(state.gpuActive[eye]<0) return;
            auto& query=state.gpuTimings[eye][state.gpuActive[eye]];
            const bool ended=SUCCEEDED(query.end->Issue(D3DISSUE_END)) &&
                SUCCEEDED(query.frequency->Issue(D3DISSUE_END)) &&
                SUCCEEDED(query.disjoint->Issue(D3DISSUE_END));
            query.pending=ended;
            state.gpuActive[eye]=-1;
            return;
        }
        const auto capture=VRProfile::Begin();
        if(!capture.epoch || !VRProfile::GPUQueriesEnabled()) return;
        if(!state.gpuInitialized)
        {
            state.gpuInitialized=true;
            state.gpuSupported=true;
            for(auto& ring:state.gpuTimings) for(auto& query:ring)
                if(FAILED(device->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&query.begin)) ||
                    FAILED(device->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&query.end)) ||
                    FAILED(device->CreateQuery(D3DQUERYTYPE_TIMESTAMPFREQ,&query.frequency)) ||
                    FAILED(device->CreateQuery(D3DQUERYTYPE_TIMESTAMPDISJOINT,&query.disjoint)))
                    state.gpuSupported=false;
        }
        if(!state.gpuSupported) return;
        auto& ring=state.gpuTimings[eye];
        for(auto& query:ring) if(query.pending)
        {
            UINT64 low=0,high=0,frequency=0; BOOL disjoint=TRUE;
            if(query.end->GetData(&high,sizeof(high),0)!=S_OK ||
                query.begin->GetData(&low,sizeof(low),0)!=S_OK ||
                query.frequency->GetData(&frequency,sizeof(frequency),0)!=S_OK ||
                query.disjoint->GetData(&disjoint,sizeof(disjoint),0)!=S_OK) continue;
            if(!disjoint && frequency && high>=low)
                VRProfile::Record(eye ? VRProfile::Metric::Eye1GPU : VRProfile::Metric::Eye0GPU,
                    query.epoch,1000.0*static_cast<double>(high-low)/frequency);
            query.pending=false;
        }
        for(unsigned index=0;index<ring.size();++index) if(!ring[index].pending)
        {
            auto& query=ring[index];
            if(SUCCEEDED(query.disjoint->Issue(D3DISSUE_BEGIN)) && SUCCEEDED(query.begin->Issue(D3DISSUE_END)))
            {query.epoch=capture.epoch; state.gpuActive[eye]=static_cast<int>(index);}
            break;
        }
    }
    std::atomic<bool> ready{false};
    std::atomic<bool> enabled{false};
    unsigned maxFrames=1;
    std::atomic<unsigned> stereoFrames{0};
    unsigned gameFrame=0,preparedFrame=0;
    long long viewTime=0;
    OpenXRVR::EyeView xrViews[2]{};
    rage::grcViewport eyeViewports[2]{};
    std::array<VRMath::Vector,6> visibilityPlanes{};
    std::array<VRMath::Vector,6> eyeVisibilityPlanes[2]{};
    bool tightVisibility=false;
    std::atomic<bool> visibilityTest{true};
    std::atomic<bool> sphereClassificationProbeRequested{false};
    unsigned sphereClassificationProbeFrames=0;
    unsigned sphereClassificationProbeQueries=0,sphereClassificationProbeMismatches=0;
    std::atomic<bool> sphereClassificationTest{false};
    bool sphereClassificationFast=false;
    void* scenePhase=nullptr;
    void* lightingPhase=nullptr;
    void* drawPhase=nullptr;
    void* worldParent=nullptr;
    bool setupThisFrame=false,prepared=false;
    int buildingEye=-1,renderEye=-1;
    thread_local bool composingHud=false;
    unsigned buildingStage=0;
    unsigned hiddenPlayerCalls[2]{};
    unsigned renderStage=0;
    unsigned completedFrame=0,completedEyes=0;
    bool captured=false;
    bool submit=false;
    bool mirror=true;
    unsigned publishedFrame=0;
    OpenXRVR::EyeView completedViews[2]{};
    VRMath::Matrix completedViewProjection[2]{};
    VRMath::Vector completedLogDepth[2]{};
    VRMath::Vector completedEyePosition[2]{};
    VRMath::Matrix completedEyeCamera[2]{};
    VRMath::Matrix completedWrist[2]{};
    bool completedWristValid[2]{};
    VRMath::Matrix completedDashboard[2]{};
    bool completedDashboardValid[2]{};
    long long completedPoseTime=0;
    std::atomic<DWORD> routingThread{0};
    std::atomic<unsigned> routingGeneration{1};
    thread_local unsigned routingPhaseGeneration=0;
    bool OnRoutingThread()
    {
        // A queued eye marker verifies its executing thread once per phase.
        // Resource replacement invalidates tokens left on any other thread.
        return (routingPhaseGeneration && routingPhaseGeneration==routingGeneration.load(std::memory_order_relaxed)) ||
            GetCurrentThreadId()==routingThread;
    }
    std::atomic<bool> restartRequested{false},captureNextPair{false};
    SafetyHookInline sceneSetup{},drawSetup{},renderFrame{},buildFrame{};
    SafetyHookInline pedRender{};
    SafetyHookInline scopeObjectRender{};
    std::atomic<unsigned> scopeHiddenObjects=0;
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
    SafetyHookInline setRenderState{},setSamplerState{},setStreamSource{},setIndices{},setVertexDeclaration{};
    bool stateHooksReady=false,stateHooksFailed=false;
    SafetyHookInline setVertexShader{};
    SafetyHookInline setVertexConstants{};
    SafetyHookInline drawPrimitive{},drawIndexedPrimitive{};
    // Enabled only between markers of the requested diagnostic pair. No
    // device getters or per-call timers in ordinary indexed draws.
    thread_local bool normalProbeActive=false;
    thread_local bool drawProbeActive=false;
    thread_local unsigned drawProbeEpoch=0,drawProbeOffset=0;
    thread_local VRProfile::DrawCounters drawProbeCounters{};
    thread_local bool stateProbeActive=false;
    thread_local bool constantProbeActive=false;
    thread_local bool routingProbeActive=false;
    thread_local bool textureCacheBypass=false;
    thread_local unsigned stateProbeEpoch=0,stateProbeOffset=0;
    thread_local VRProfile::StateCounters stateProbeCounters[static_cast<unsigned>(VRProfile::StateKind::Count)]{};

    template<typename Call> HRESULT ObserveStateCall(VRProfile::StateKind kind,Call&& call)
    {
        if(!stateProbeActive) return call();
        auto& counters=stateProbeCounters[static_cast<unsigned>(kind)];
        ++counters.calls;
        const auto stamp=((counters.calls+stateProbeOffset+static_cast<unsigned>(kind)*7)&63)==0
            ? VRProfile::Begin():VRProfile::Stamp{};
        const auto result=call();
        counters.failedCalls+=FAILED(result);
        if(stamp.epoch==stateProbeEpoch && stamp.ticks)
        {
            VRProfile::Total sample{stamp.epoch,0}; VRProfile::Accumulate(sample,stamp);
            ++counters.timedCalls; counters.timedMs+=sample.ms;
            counters.maxTimedMs=(std::max)(counters.maxTimedMs,sample.ms);
        }
        return result;
    }

    VRProfile::Stamp BeginDrawProbe(bool indexed,UINT primitives,UINT vertices)
    {
        auto& counters=drawProbeCounters;
        ++counters.calls; counters.indexedCalls+=indexed;
        counters.primitives+=primitives; counters.vertices+=vertices;
        // Rotate the systematic sample over64 frames to avoid always timing
        // the same positions in a repeated draw sequence. No per-call lock.
        return ((counters.calls+drawProbeOffset)&63)==0 ? VRProfile::Begin():VRProfile::Stamp{};
    }
    void EndDrawProbe(VRProfile::Stamp stamp,HRESULT result)
    {
        drawProbeCounters.failedCalls+=FAILED(result);
        if(stamp.epoch!=drawProbeEpoch || !stamp.ticks) return;
        VRProfile::Total sample{stamp.epoch,0}; VRProfile::Accumulate(sample,stamp);
        ++drawProbeCounters.timedCalls; drawProbeCounters.timedMs+=sample.ms;
        drawProbeCounters.maxTimedMs=(std::max)(drawProbeCounters.maxTimedMs,sample.ms);
    }
    struct NormalProbeKey {IDirect3DPixelShader9* shader; std::array<IDirect3DBaseTexture9*,8> textures;};
    thread_local std::vector<NormalProbeKey> normalProbeKeys[2];
    thread_local std::vector<ComPtr<IDirect3DPixelShader9>> normalProbeShaders;
    thread_local DWORD gbufferMipFilters[16]{};
    thread_local bool gbufferMipValid[16]{};

    void PrepareGBufferMipFilters(IDirect3DDevice9* device,unsigned eye)
    {
        // The second eye follows the first eye's lighting/transparency work.
        // Native cached commands can omit sampler writes: observed normal-map
        // slots1/2 retained MIPFILTER=NONE instead of the first eye's LINEAR.
        // Match the authoritative first-eye entry state; do not choose a new
        // filter or touch the game's software cache or camera constants.
        for(unsigned slot=0;slot<16;++slot)
        {
            DWORD value=0;
            const bool valid=SUCCEEDED(device->GetSamplerState(slot,D3DSAMP_MIPFILTER,&value));
            if(!eye) {gbufferMipValid[slot]=valid; gbufferMipFilters[slot]=value;}
            else if(valid && gbufferMipValid[slot] && value!=gbufferMipFilters[slot])
                device->SetSamplerState(slot,D3DSAMP_MIPFILTER,gbufferMipFilters[slot]);
        }
    }
    SafetyHookInline setPixelShader{},setPixelConstants{};
    SafetyHookInline presentHook{},presentExHook{},swapPresentHook{};
    thread_local unsigned presentDepth=0;

    HRESULT WINAPI DesktopPresent(IDirect3DDevice9* self,const RECT* source,const RECT* destination,HWND window,const RGNDATA* dirty)
    {
        const auto stamp=presentDepth++ ? VRProfile::Stamp{} : VRProfile::Begin();
        const auto result=presentHook.unsafe_stdcall<HRESULT>(self,source,destination,window,dirty);
        --presentDepth; VRProfile::End(VRProfile::Metric::DesktopPresent,stamp); return result;
    }
    HRESULT WINAPI DesktopPresentEx(IDirect3DDevice9Ex* self,const RECT* source,const RECT* destination,HWND window,const RGNDATA* dirty,DWORD flags)
    {
        const auto stamp=presentDepth++ ? VRProfile::Stamp{} : VRProfile::Begin();
        const auto result=presentExHook.unsafe_stdcall<HRESULT>(self,source,destination,window,dirty,flags);
        --presentDepth; VRProfile::End(VRProfile::Metric::DesktopPresent,stamp); return result;
    }
    HRESULT WINAPI DesktopSwapPresent(IDirect3DSwapChain9* self,const RECT* source,const RECT* destination,HWND window,const RGNDATA* dirty,DWORD flags)
    {
        const auto stamp=presentDepth++ ? VRProfile::Stamp{} : VRProfile::Begin();
        const auto result=swapPresentHook.unsafe_stdcall<HRESULT>(self,source,destination,window,dirty,flags);
        --presentDepth; VRProfile::End(VRProfile::Metric::DesktopPresent,stamp); return result;
    }
    thread_local bool cascadeShaderBound=false;
    float originalCascadeForward[4]{},cascadeForward[4]{},gameShadowForward[4]{};
    unsigned lightingProbeMask=0;
    float expectedCamera[16]{};
    thread_local bool rayShaderBound=false;
    float originalRayConstants[4]{},eyeRayConstants[4]{};

    // Native commands update RAGE's software state cache as they execute.
    // Restoring an earlier D3D9 state block here would rewind only the device,
    // leaving that cache inconsistent. Rebind(-1) restores routed resources
    // and native shader registers; retain the native pass's final state and
    // restore only our view-selection and first-eye sampling bookkeeping.
    struct ScopeState
    {
        float cascade[4]{},ray[4]{},camera[16]{};
        DWORD mipFilters[16]{};
        bool mipValid[16]{};
        bool Capture(IDirect3DDevice9*)
        {
            std::memcpy(cascade,cascadeForward,sizeof(cascade));
            std::memcpy(ray,eyeRayConstants,sizeof(ray));
            std::memcpy(camera,expectedCamera,sizeof(camera));
            std::memcpy(mipFilters,gbufferMipFilters,sizeof(mipFilters));
            std::memcpy(mipValid,gbufferMipValid,sizeof(mipValid));
            return true;
        }
        void Restore(IDirect3DDevice9*)
        {
            std::memcpy(cascadeForward,cascade,sizeof(cascade));
            std::memcpy(eyeRayConstants,ray,sizeof(ray));
            std::memcpy(expectedCamera,camera,sizeof(camera));
            std::memcpy(gbufferMipFilters,mipFilters,sizeof(mipFilters));
            std::memcpy(gbufferMipValid,mipValid,sizeof(mipValid));
        }
    } scopeState;
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

    // Diagnostic only: attach on a queued render marker, detach after five
    // seconds. No detour remains in ordinary captures. Counts are not timings.
    std::atomic<bool> vertexProbeRequested{false};
    SafetyHookMid vertexProbeHook;
    struct VertexCaller {uintptr_t caller=0; unsigned long long calls[7]{};};
    std::array<VertexCaller,512> vertexCallers{};
    unsigned long long vertexProbeDropped=0,vertexProbeStreams[6]{};
    ULONGLONG vertexProbeDeadline=0;
    DWORD vertexProbeThread=0;
    unsigned vertexProbePhase=6; // Outside the marked main eye streams.
    std::atomic<bool> vertexEmissionRequested{false};
    SafetyHookInline vertexEmissionHook;
    std::uint32_t *vertexEnabled=nullptr,*vertexCount=nullptr;
    std::uint32_t** vertexOutput=nullptr;

    void __cdecl EmitVertexWords(std::uint32_t,std::uint32_t,std::uint32_t,
        std::uint32_t,std::uint32_t,std::uint32_t,std::uint32_t,std::uint32_t,std::uint32_t)
    {
        // All nine cdecl words are contiguous above the return address;
        // reinterpret their bits without converting the caller's float ABI.
        VRVertex::Emit(*vertexEnabled,*vertexOutput,*vertexCount,
            reinterpret_cast<const std::uint32_t*>(_AddressOfReturnAddress())+1);
    }

    void UpdateVertexEmissionTest()
    {
        if(vertexProbeHook) return;
        if(!vertexEmissionRequested.load())
        {if(vertexEmissionHook) {vertexEmissionHook.reset(); Log("vertex-emission-test off");} return;}
        if(vertexEmissionHook) return;
        auto entry=hook::pattern("83 3D ? ? ? ? 00 74 ? A1 ? ? ? ? 85 C0 74 ? F3 0F 10 44 24 04 8B C8 83 C0 24 FF 05 ? ? ? ? A3 ? ? ? ?");
        if(entry.size()!=1) {vertexEmissionRequested=false; Log("vertex-emission-test unavailable: signature mismatch"); return;}
        auto* enabledAddress=*entry.get_first<std::uint32_t*>(2);
        auto** outputAddress=*entry.get_first<std::uint32_t**>(10);
        auto* countAddress=*entry.get_first<std::uint32_t*>(31);
        auto** advancedOutputAddress=*entry.get_first<std::uint32_t**>(36);
        if(reinterpret_cast<uintptr_t>(enabledAddress)!=reinterpret_cast<uintptr_t>(countAddress)+4 ||
            reinterpret_cast<uintptr_t>(outputAddress)!=reinterpret_cast<uintptr_t>(countAddress)+12 || outputAddress!=advancedOutputAddress)
        {vertexEmissionRequested=false; Log("vertex-emission-test unavailable: state layout mismatch"); return;}
        vertexEnabled=enabledAddress; vertexOutput=outputAddress; vertexCount=countAddress;
        vertexEmissionHook=safetyhook::create_inline(entry.get_first(),EmitVertexWords);
        if(!vertexEmissionHook) vertexEmissionRequested=false;
        Log(vertexEmissionHook ? "vertex-emission-test on: exact36-byte copies, diagnostic opt-in" : "vertex-emission-test hook unavailable");
    }

    void FinishVertexCallerProbe()
    {
        if(!vertexProbeHook) return;
        vertexProbeHook.reset();
        char line[512];
        const auto base=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        sprintf_s(line,"vertex-caller-probe complete moduleBase=%08X thread=%lu dropped=%llu streams=%llu,%llu,%llu,%llu,%llu,%llu",
            static_cast<unsigned>(base),vertexProbeThread,vertexProbeDropped,
            vertexProbeStreams[0],vertexProbeStreams[1],vertexProbeStreams[2],
            vertexProbeStreams[3],vertexProbeStreams[4],vertexProbeStreams[5]);
        Log(line);
        for(const auto& row:vertexCallers) if(row.caller)
        {
            // Store the return address; the candidate direct CALL is five
            // bytes earlier and must still be validated in live disassembly.
            sprintf_s(line,"vertex-caller returnRuntime=%08X returnAnalysis=%08X counts=%llu,%llu,%llu,%llu,%llu,%llu,%llu",
                static_cast<unsigned>(row.caller),static_cast<unsigned>(row.caller-base+0x400000),
                row.calls[0],row.calls[1],row.calls[2],row.calls[3],row.calls[4],row.calls[5],row.calls[6]);
            Log(line);
        }
        vertexProbeDeadline=0;
    }

    void UpdateVertexCallerProbe()
    {
        if(vertexProbeHook && GetTickCount64()>=vertexProbeDeadline) FinishVertexCallerProbe();
        if(!vertexProbeRequested.exchange(false) || vertexProbeHook) return;
        if(vertexEmissionHook) {Log("vertex-caller-probe unavailable: disable vertex-emission-test first"); return;}
        auto entry=hook::pattern("83 3D ? ? ? ? 00 74 ? A1 ? ? ? ? 85 C0 74 ? F3 0F 10 44 24 04 8B C8 83 C0 24");
        if(entry.size()!=1) {Log("vertex-caller-probe unavailable: unique entry signature not found"); return;}
        vertexCallers={}; vertexProbeDropped=0;
        for(auto& count:vertexProbeStreams) count=0;
        vertexProbeThread=GetCurrentThreadId(); vertexProbePhase=6;
        vertexProbeHook=safetyhook::create_mid(entry.get_first(),[](SafetyHookContext& regs)
        {
            if(GetCurrentThreadId()!=vertexProbeThread) return;
            const auto caller=*reinterpret_cast<const uintptr_t*>(regs.esp);
            auto index=(caller>>4)&(vertexCallers.size()-1);
            for(unsigned attempt=0;attempt<vertexCallers.size();++attempt)
            {
                auto& row=vertexCallers[index];
                if(!row.caller || row.caller==caller)
                {row.caller=caller; ++row.calls[vertexProbePhase]; return;}
                index=(index+1)&(vertexCallers.size()-1);
            }
            ++vertexProbeDropped;
        });
        if(vertexProbeHook)
        {vertexProbeDeadline=GetTickCount64()+5000; Log("vertex-caller-probe armed: five seconds, counts only; exclude from performance captures");}
        else Log("vertex-caller-probe unavailable: hook creation failed");
    }

    // Complete native executions, rather than a racing command-buffer prefix.
    // This bounded diagnostic changes no command, model or visibility decision.
    std::atomic<bool> modelProbeRequested{false};
    std::atomic<bool> billboardInstancingRequested{false};
    std::atomic<bool> rigidInstancingRequested{false};
    std::unique_ptr<VRRigidInstance::Renderer> rigidInstances;
    std::atomic<bool> meshPacketRequested{false};
    std::atomic<bool> meshOrderRequested{false};
    std::atomic<bool> meshReplayRequested{false};
    std::atomic<bool> meshBenchmarkRequested{false};
    std::atomic<unsigned> liveMeshRequested{0};
    std::atomic<unsigned> cpuOmitRequested{99},cpuOmitMode{0};
    std::atomic<ULONGLONG> cpuOmitDeadline{0},cpuOmitSkipped[2]{};
    SafetyHookInline cpuOmitHook;
    bool InCpuOmission()
    {return OnRoutingThread() && renderEye>=0 && renderEye<2 && (renderStage==1 || renderStage==3);}
    void __fastcall OmitModelWork(void* self,void* edx)
    {
        if(InCpuOmission()){++cpuOmitSkipped[renderEye];return;}
        cpuOmitHook.unsafe_fastcall(self,edx);
    }
    void __fastcall OmitMaterialWork(void* self,void* edx,uintptr_t a,uintptr_t b,uintptr_t c)
    {
        if(InCpuOmission()){++cpuOmitSkipped[renderEye];return;}
        cpuOmitHook.unsafe_fastcall(self,edx,a,b,c);
    }
    void UpdateCpuOmission();
    unsigned liveMeshMode=0;
    std::unique_ptr<VRMeshPacketProbe::Probe> liveMeshPackets;
    VRRigidInstance::Renderer liveMeshRenderer;
    LARGE_INTEGER liveMeshBegin{};
    SafetyHookInline liveMeshClear;
    SafetyHookInline liveMeshVBLock,liveMeshIBLock,liveMeshTextureLock,liveMeshUpdateTexture,liveMeshUpdateSurface,liveMeshColorFill,liveMeshGenerateMips;
    void FlushLiveMesh(IDirect3DDevice9* device);
    bool meshCompareOrder=false;
    std::unique_ptr<VRMeshPacketProbe::Probe> meshPacketProbe;
    SafetyHookInline meshPacketMaterialProbe;
    uintptr_t meshPacketMaterial=0;
    void __fastcall ObservePacketMaterial(void* self,void* edx,uintptr_t a,uintptr_t b,uintptr_t c,uintptr_t d)
    {
        meshPacketMaterialProbe.unsafe_fastcall(self,edx,a,b,c,d);
        if(meshPacketProbe && meshPacketProbe->Observing() && GetCurrentThreadId()==routingThread && renderEye==0 && renderStage==1)
            meshPacketMaterial=reinterpret_cast<uintptr_t>(self);
    }
    struct MeshPacketShader
    {
        ComPtr<IDirect3DVertexShader9> vs;ComPtr<IDirect3DPixelShader9> ps;
        ComPtr<IDirect3DVertexDeclaration9> declaration;
        VRMeshPacketProbe::ShaderReads reads{};bool valid=false;
    };
    std::vector<MeshPacketShader> meshPacketShaders;
    SafetyHookInline modelProbeHook;
    std::atomic<bool> constantBatchRequested{false};
    SafetyHookInline vertexBinderBatchHook;SafetyHookMid textureBinderBatchBoundary;
    thread_local VRConstantBatch::Buffer binderConstants;
    thread_local IDirect3DDevice9* binderDevice=nullptr;
    thread_local int binderEye=-1;
    unsigned long long binderBatchScopes[2]{},binderBatchInputs[2]{},binderBatchOutputs[2]{},binderBatchFailures=0;
    unsigned binderBatchValidations=0,binderBatchValidationFailures=0;
    void FlushBinderConstants()
    {
        if(!binderDevice)return;
        binderConstants.Flush([](unsigned first,const float* data,unsigned count)
        {if(FAILED(setVertexConstants.unsafe_stdcall<HRESULT>(binderDevice,first,data,count)))++binderBatchFailures;});
    }
    void __fastcall BatchVertexBinder(void* self,void* edx,uintptr_t a,uintptr_t b,uintptr_t c)
    {
        if(binderDevice || GetCurrentThreadId()!=routingThread || renderEye<0 || renderStage!=1 || !ready)
        {FlushBinderConstants();vertexBinderBatchHook.unsafe_fastcall(self,edx,a,b,c);return;}
        auto* device=RageDirect3DDevice9::m_pRealDevice ? *RageDirect3DDevice9::m_pRealDevice:nullptr;
        if(!device){vertexBinderBatchHook.unsafe_fastcall(self,edx,a,b,c);return;}
        binderConstants.Reset();binderDevice=device;binderEye=renderEye;
        vertexBinderBatchHook.unsafe_fastcall(self,edx,a,b,c);
        FlushBinderConstants();
        if(binderBatchValidations<64 && binderConstants.inputCalls)
        {
            float registers[256][4]{};++binderBatchValidations;
            if(FAILED(device->GetVertexShaderConstantF(0,registers[0],256)) || !binderConstants.Validate(registers[0]))
            {++binderBatchValidationFailures;constantBatchRequested=false;}
        }
        ++binderBatchScopes[binderEye];binderBatchInputs[binderEye]+=binderConstants.inputCalls;
        binderBatchOutputs[binderEye]+=binderConstants.outputCalls;
        binderDevice=nullptr;binderEye=-1;
    }
    void UpdateConstantBatchTest()
    {
        if(vertexBinderBatchHook && !constantBatchRequested)
        {
            textureBinderBatchBoundary.reset();vertexBinderBatchHook.reset();
            char line[300];sprintf_s(line,"constant-batch-test off scopes=%llu,%llu inputCalls=%llu,%llu outputCalls=%llu,%llu failures=%llu validations=%u validationFailures=%u",
                binderBatchScopes[0],binderBatchScopes[1],binderBatchInputs[0],binderBatchInputs[1],binderBatchOutputs[0],binderBatchOutputs[1],binderBatchFailures,binderBatchValidations,binderBatchValidationFailures);Log(line);
        }
        if(!constantBatchRequested || vertexBinderBatchHook)return;
        if(modelProbeHook || vertexProbeHook || vertexEmissionHook || rigidInstances || meshPacketProbe || VRProfile::ConstantObservationEnabled())
        {constantBatchRequested=false;Log("constant-batch-test unavailable: disable other native/constant experiments first");return;}
        auto binder=hook::pattern("83 EC 10 53 55 8B 6C 24 1C 8B D9 8B 55 08 57 89 5C 24 18 39 15");
        auto texture=hook::pattern("83 EC 34 53 57 8B FA 8B D9 85 FF 75 ? A1 ? ? ? ? 57 8B 10 53 50 FF 92 04 01 00 00");
        if(binder.size()!=1 || texture.size()!=1)
        {constantBatchRequested=false;Log("constant-batch-test unavailable: unique native binder/boundary signatures not found");return;}
        binderBatchScopes[0]=binderBatchScopes[1]=binderBatchInputs[0]=binderBatchInputs[1]=binderBatchOutputs[0]=binderBatchOutputs[1]=binderBatchFailures=0;
        binderBatchValidations=binderBatchValidationFailures=0;
        textureBinderBatchBoundary=safetyhook::create_mid(texture.get_first(),[](SafetyHookContext&){FlushBinderConstants();});
        if(textureBinderBatchBoundary)vertexBinderBatchHook=safetyhook::create_inline(binder.get_first(),BatchVertexBinder);
        if(!vertexBinderBatchHook)
        {textureBinderBatchBoundary.reset();constantBatchRequested=false;Log("constant-batch-test native hooks unavailable");return;}
        Log("constant-batch-test on: verified vertex parameter binder; adjacency/overlaps only; texture callback and binder-return flush boundaries");
    }
    SafetyHookMid billboardBatchHook;
    SafetyHookInline materialProbeHook;
    SafetyHookInline meshGeometryProbe;
    uintptr_t meshGeometryCaller=0;
    SafetyHookInline meshLoopProbe;
    uintptr_t meshLoopCaller=0;
    SafetyHookMid coronaDrawBeginProbe,coronaDrawEndProbe;
    SafetyHookMid coronaRecordProbe,coronaVerticesProbe;
    uintptr_t coronaProbePool=0,coronaProbeModuleBase=0;
    thread_local bool coronaDrawProbeActive=false;
    struct CoronaDrawSnapshot
    {
        bool captured=false,valid=false;
        unsigned type=0,start=0,count=0,stride=0,offset=0;
        std::vector<DWORD> vertexShader,pixelShader;
        D3DVERTEXELEMENT9 declaration[MAXD3DDECLLENGTH+1]{};
        UINT declarationCount=0;
        float vertexConstants[256][4]{},pixelConstants[224][4]{};
        DWORD states[7]{};
        bool recordCaptured=false;
        unsigned flags[2]{},vertexCount=0;
        DWORD record[8]{},vertices[6][9]{};
        float basis[9]{}; // Native right, negated up, camera position.
    };
    CoronaDrawSnapshot coronaSnapshots[6]{};
    constexpr D3DRENDERSTATETYPE coronaStates[]{D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,
        D3DRS_ZFUNC,D3DRS_ALPHABLENDENABLE,D3DRS_SRCBLEND,D3DRS_DESTBLEND,D3DRS_CULLMODE};

    struct MaterialProbeTotals
    {
        unsigned long long calls=0,ticks=0,stateEntries=0,vertexParameters=0,pixelParameters=0;
    };
    MaterialProbeTotals materialProbeTotals[6]{};
    struct ModelProbeRow
    {
        unsigned key=0,hash=0;
        unsigned long long calls[6]{},ticks[6]{};
    };
    std::array<ModelProbeRow,4096> modelProbeRows{};
    unsigned long long modelProbeStreams[6]{},modelProbeDropped=0;
    DWORD modelProbeThread=0;
    unsigned modelProbePhase=6;
    ULONGLONG modelProbeDeadline=0;
    long long modelProbeFrequency=0;
    void** modelProbeTable=nullptr;

    // One eye0 G-buffer stream only. Matching keys are batching candidates,
    // not proof that constants, textures, animation or ordering can be shared.
    struct MeshProbeRow {std::array<uintptr_t,14> key{};unsigned draws=0,primitives=0,runs=0,longestRun=0;};
    std::array<MeshProbeRow,8192> meshProbeRows{};
    unsigned meshProbeModel=0,meshProbeDraws=0,meshProbeDropped=0,meshProbeFailures=0;
    uintptr_t meshProbePass=0;
    unsigned meshProbePreviousRow=8192,meshProbeRun=0;
    struct MeshModelRuns {unsigned draws=0,runs=0,longest=0;};
    std::array<MeshModelRuns,65536> meshModelRuns{};
    unsigned meshPreviousModel=65536,meshModelRun=0,meshNonIndexed=0;
    SafetyHookInline meshQueryIssueProbe;
    SafetyHookInline meshPrimitiveUPProbe,meshIndexedUPProbe;
    bool meshQueriesObserved=false;
    unsigned meshQueryBegins=0,meshQueryEnds=0,meshQueryOther=0,meshUPDraws=0;
    bool InFirstMeshStream()
    {return modelProbeHook && GetCurrentThreadId()==modelProbeThread && modelProbePhase==0 && modelProbeStreams[0]==1;}
    bool InPacketStream()
    {return meshPacketProbe && meshPacketProbe->Observing() && GetCurrentThreadId()==routingThread && renderEye==0 && renderStage==1;}
    void __fastcall ObserveMeshGeometry(void* self,void* edx)
    {
        if(!InFirstMeshStream())
        {meshGeometryProbe.unsafe_fastcall(self,edx);return;}
        const auto previous=meshGeometryCaller;
        meshGeometryCaller=reinterpret_cast<uintptr_t>(_ReturnAddress());
        meshGeometryProbe.unsafe_fastcall(self,edx);
        meshGeometryCaller=previous;
    }
    void __fastcall ObserveMeshLoop(void* self,void* edx,uintptr_t a,uintptr_t b,uintptr_t c)
    {
        if(!InFirstMeshStream())
        {meshLoopProbe.unsafe_fastcall(self,edx,a,b,c);return;}
        const auto previous=meshLoopCaller;
        meshLoopCaller=reinterpret_cast<uintptr_t>(_ReturnAddress());
        meshLoopProbe.unsafe_fastcall(self,edx,a,b,c);
        meshLoopCaller=previous;
    }
    void MeshSequenceBarrier()
    {meshProbePreviousRow=8192;meshProbeRun=0;meshPreviousModel=65536;meshModelRun=0;}
    HRESULT WINAPI ObserveMeshQueryIssue(IDirect3DQuery9* query,DWORD flags)
    {
        if(liveMeshPackets && OnRoutingThread() && RageDirect3DDevice9::m_pRealDevice)FlushLiveMesh(*RageDirect3DDevice9::m_pRealDevice);
        if(InPacketStream())meshPacketProbe->QueryEvent();
        if(InFirstMeshStream())
        {
            if(query->GetType()==D3DQUERYTYPE_OCCLUSION)
            {if(flags==D3DISSUE_BEGIN)++meshQueryBegins;else if(flags==D3DISSUE_END)++meshQueryEnds;else ++meshQueryOther;}
            else ++meshQueryOther;
            MeshSequenceBarrier();
        }
        return meshQueryIssueProbe.unsafe_stdcall<HRESULT>(query,flags);
    }
    HRESULT WINAPI ObserveMeshPrimitiveUP(IDirect3DDevice9* device,D3DPRIMITIVETYPE type,
        UINT count,const void* vertices,UINT stride)
    {
        FlushLiveMesh(device);
        if(InPacketStream())meshPacketProbe->OtherDraw();
        if(InFirstMeshStream()){++meshUPDraws;MeshSequenceBarrier();}
        return meshPrimitiveUPProbe.unsafe_stdcall<HRESULT>(device,type,count,vertices,stride);
    }
    HRESULT WINAPI ObserveMeshIndexedUP(IDirect3DDevice9* device,D3DPRIMITIVETYPE type,
        UINT minimum,UINT vertexCount,UINT count,const void* indices,D3DFORMAT format,
        const void* vertices,UINT stride)
    {
        FlushLiveMesh(device);
        if(InPacketStream())meshPacketProbe->OtherDraw();
        if(InFirstMeshStream()){++meshUPDraws;MeshSequenceBarrier();}
        return meshIndexedUPProbe.unsafe_stdcall<HRESULT>(device,type,minimum,vertexCount,count,indices,format,vertices,stride);
    }
    constexpr D3DRENDERSTATETYPE meshProbeStates[]{D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,
        D3DRS_ZFUNC,D3DRS_ALPHATESTENABLE,D3DRS_ALPHAREF,D3DRS_ALPHAFUNC,
        D3DRS_ALPHABLENDENABLE,D3DRS_SRCBLEND,D3DRS_DESTBLEND,D3DRS_BLENDOP,
        D3DRS_SEPARATEALPHABLENDENABLE,D3DRS_SRCBLENDALPHA,D3DRS_DESTBLENDALPHA,
        D3DRS_BLENDOPALPHA,D3DRS_CULLMODE,D3DRS_STENCILENABLE,D3DRS_STENCILFUNC,
        D3DRS_STENCILREF,D3DRS_STENCILMASK,D3DRS_STENCILWRITEMASK,D3DRS_STENCILFAIL,
        D3DRS_STENCILZFAIL,D3DRS_STENCILPASS,D3DRS_TWOSIDEDSTENCILMODE,
        D3DRS_CCW_STENCILFUNC,D3DRS_CCW_STENCILFAIL,D3DRS_CCW_STENCILZFAIL,
        D3DRS_CCW_STENCILPASS,D3DRS_COLORWRITEENABLE,D3DRS_COLORWRITEENABLE1,
        D3DRS_COLORWRITEENABLE2,D3DRS_COLORWRITEENABLE3,D3DRS_SCISSORTESTENABLE,
        D3DRS_CLIPPLANEENABLE,D3DRS_DEPTHBIAS,D3DRS_SLOPESCALEDEPTHBIAS,
        D3DRS_MULTISAMPLEANTIALIAS,D3DRS_MULTISAMPLEMASK,D3DRS_SRGBWRITEENABLE};
    struct MeshStateProbe
    {
        unsigned row=0,samples=0,failures=0;bool selected=false;
        float vertex[256][4]{},pixel[224][4]{};
        unsigned vertexChanged[256]{},pixelChanged[224]{};
        uintptr_t textures[16]{};unsigned textureChanged=0;
        DWORD states[std::size(meshProbeStates)]{},sampler[13]{};
        bool stateChanged[std::size(meshProbeStates)]{},samplerChanged[13]{};
        uintptr_t streams[16]{},targets[5]{};UINT offsets[16]{},strides[16]{},frequencies[16]{};
        unsigned streamChanged=0,targetChanged=0;DWORD vertexUsage=0,indexUsage=0;
        D3DVIEWPORT9 viewport{};RECT scissor{};bool viewportChanged=false,scissorChanged=false;
        D3DVERTEXELEMENT9 declaration[MAXD3DDECLLENGTH+1]{};UINT declarationCount=0;
        void* callers[16]{};USHORT callerCount=0;
        std::vector<DWORD> shader,pixelShader;
    };
    std::array<MeshStateProbe,8> meshStateProbes{};
    std::array<MeshStateProbe,8> meshUnknownStateProbes{};
    void CaptureMeshState(IDirect3DDevice9* device,unsigned row)
    {
        auto& probes=meshProbeRows[row].key[0] ? meshStateProbes:meshUnknownStateProbes;
        auto selected=std::find_if(probes.begin(),probes.end(),
            [row](const auto& sample){return sample.selected && sample.row==row;});
        if(selected==probes.end())
        {
            if(meshProbeRows[row].draws!=32)return;
            selected=std::find_if(probes.begin(),probes.end(),
                [](const auto& sample){return !sample.selected;});
            if(selected==probes.end())return;
        }
        auto& sample=*selected;
        if(!sample.selected)
        {
            if(meshProbeRows[row].draws!=32)return;
            sample.selected=true;sample.row=row;
        }
        if(sample.row!=row)return;
        float vertex[256][4]{},pixel[224][4]{};
        if(FAILED(device->GetVertexShaderConstantF(0,vertex[0],256)) ||
            FAILED(device->GetPixelShaderConstantF(0,pixel[0],224)))
        {++sample.failures;return;}
        uintptr_t textures[16]{};
        for(unsigned i=0;i<16;++i)
        {
            ComPtr<IDirect3DBaseTexture9> texture;
            if(FAILED(device->GetTexture(i,&texture))){++sample.failures;return;}
            textures[i]=reinterpret_cast<uintptr_t>(texture.Get());
        }
        DWORD states[std::size(meshProbeStates)]{},sampler[13]{};
        uintptr_t streams[16]{},targets[5]{};UINT offsets[16]{},strides[16]{},frequencies[16]{};
        D3DVIEWPORT9 viewport{};RECT scissor{};
        for(unsigned i=0;i<std::size(meshProbeStates);++i)
            if(FAILED(device->GetRenderState(meshProbeStates[i],&states[i]))){++sample.failures;return;}
        for(unsigned i=1;i<=13;++i)
            if(FAILED(device->GetSamplerState(0,static_cast<D3DSAMPLERSTATETYPE>(i),&sampler[i-1])))
            {++sample.failures;return;}
        for(unsigned i=0;i<16;++i)
        {
            ComPtr<IDirect3DVertexBuffer9> buffer;
            if(FAILED(device->GetStreamSource(i,&buffer,&offsets[i],&strides[i])) ||
                FAILED(device->GetStreamSourceFreq(i,&frequencies[i]))){++sample.failures;return;}
            streams[i]=reinterpret_cast<uintptr_t>(buffer.Get());
            if(!sample.samples && i==0 && buffer)
            {D3DVERTEXBUFFER_DESC desc{};if(FAILED(buffer->GetDesc(&desc))){++sample.failures;return;}sample.vertexUsage=desc.Usage;}
        }
        for(unsigned i=0;i<4;++i)
        {
            ComPtr<IDirect3DSurface9> surface;const auto result=device->GetRenderTarget(i,&surface);
            if(FAILED(result) && result!=D3DERR_NOTFOUND){++sample.failures;return;}
            targets[i]=reinterpret_cast<uintptr_t>(surface.Get());
        }
        ComPtr<IDirect3DSurface9> depth;const auto depthResult=device->GetDepthStencilSurface(&depth);
        if((FAILED(depthResult) && depthResult!=D3DERR_NOTFOUND) ||
            FAILED(device->GetViewport(&viewport)) || FAILED(device->GetScissorRect(&scissor)))
        {++sample.failures;return;}
        targets[4]=reinterpret_cast<uintptr_t>(depth.Get());
        if(!sample.samples)
        {
            sample.callerCount=CaptureStackBackTrace(0,std::size(sample.callers),sample.callers,nullptr);
            if(meshGeometryCaller && sample.callerCount<std::size(sample.callers))
                sample.callers[sample.callerCount++]=reinterpret_cast<void*>(meshGeometryCaller);
            if(meshLoopCaller && sample.callerCount<std::size(sample.callers))
                sample.callers[sample.callerCount++]=reinterpret_cast<void*>(meshLoopCaller);
            std::memcpy(sample.states,states,sizeof(states));std::memcpy(sample.sampler,sampler,sizeof(sampler));
            std::memcpy(sample.streams,streams,sizeof(streams));std::memcpy(sample.targets,targets,sizeof(targets));
            std::memcpy(sample.offsets,offsets,sizeof(offsets));std::memcpy(sample.strides,strides,sizeof(strides));
            std::memcpy(sample.frequencies,frequencies,sizeof(frequencies));sample.viewport=viewport;sample.scissor=scissor;
            ComPtr<IDirect3DVertexDeclaration9> declaration;ComPtr<IDirect3DIndexBuffer9> index;
            D3DINDEXBUFFER_DESC indexDesc{};sample.declarationCount=std::size(sample.declaration);
            if(FAILED(device->GetVertexDeclaration(&declaration)) || !declaration ||
                FAILED(declaration->GetDeclaration(sample.declaration,&sample.declarationCount)) ||
                FAILED(device->GetIndices(&index)) || !index || FAILED(index->GetDesc(&indexDesc)))
            {++sample.failures;return;}
            sample.indexUsage=indexDesc.Usage;
            std::memcpy(sample.vertex,vertex,sizeof(vertex));std::memcpy(sample.pixel,pixel,sizeof(pixel));
            std::memcpy(sample.textures,textures,sizeof(textures));
            ComPtr<IDirect3DVertexShader9> shader;UINT bytes=0;
            if(SUCCEEDED(device->GetVertexShader(&shader)) && shader &&
                SUCCEEDED(shader->GetFunction(nullptr,&bytes)) && bytes && bytes<=65536 && !(bytes%4))
            {sample.shader.resize(bytes/4);if(FAILED(shader->GetFunction(sample.shader.data(),&bytes)))sample.shader.clear();}
            ComPtr<IDirect3DPixelShader9> ps;bytes=0;
            if(SUCCEEDED(device->GetPixelShader(&ps)) && ps &&
                SUCCEEDED(ps->GetFunction(nullptr,&bytes)) && bytes && bytes<=65536 && !(bytes%4))
            {sample.pixelShader.resize(bytes/4);if(FAILED(ps->GetFunction(sample.pixelShader.data(),&bytes)))sample.pixelShader.clear();}
        }
        else
        {
            for(unsigned i=0;i<std::size(meshProbeStates);++i)sample.stateChanged[i]|=sample.states[i]!=states[i];
            for(unsigned i=0;i<13;++i)sample.samplerChanged[i]|=sample.sampler[i]!=sampler[i];
            for(unsigned i=0;i<16;++i)if(sample.streams[i]!=streams[i] || sample.offsets[i]!=offsets[i] ||
                sample.strides[i]!=strides[i] || sample.frequencies[i]!=frequencies[i])sample.streamChanged|=1u<<i;
            for(unsigned i=0;i<5;++i)if(sample.targets[i]!=targets[i])sample.targetChanged|=1u<<i;
            sample.viewportChanged|=std::memcmp(&sample.viewport,&viewport,sizeof(viewport))!=0;
            sample.scissorChanged|=std::memcmp(&sample.scissor,&scissor,sizeof(scissor))!=0;
            for(unsigned i=0;i<256;++i)for(unsigned lane=0;lane<4;++lane)
                if(std::memcmp(&sample.vertex[i][lane],&vertex[i][lane],4))sample.vertexChanged[i]|=1u<<lane;
            for(unsigned i=0;i<224;++i)for(unsigned lane=0;lane<4;++lane)
                if(std::memcmp(&sample.pixel[i][lane],&pixel[i][lane],4))sample.pixelChanged[i]|=1u<<lane;
            for(unsigned i=0;i<16;++i)if(sample.textures[i]!=textures[i])sample.textureChanged|=1u<<i;
        }
        ++sample.samples;
    }
    void CaptureMeshBindings(IDirect3DDevice9* device,D3DPRIMITIVETYPE type,
        INT base,UINT minimum,UINT vertices,UINT start,UINT primitives)
    {
        if(!modelProbeHook || GetCurrentThreadId()!=modelProbeThread || modelProbePhase!=0 ||
            modelProbeStreams[0]!=1) return;
        ++meshProbeDraws;
        if(meshProbeModel<meshModelRuns.size())
        {
            auto& model=meshModelRuns[meshProbeModel];++model.draws;
            if(meshPreviousModel==meshProbeModel)++meshModelRun;
            else {++model.runs;meshModelRun=1;meshPreviousModel=meshProbeModel;}
            model.longest=std::max(model.longest,meshModelRun);
        }
        ComPtr<IDirect3DVertexShader9> vs;ComPtr<IDirect3DPixelShader9> ps;
        ComPtr<IDirect3DVertexDeclaration9> declaration;
        ComPtr<IDirect3DVertexBuffer9> vb;ComPtr<IDirect3DIndexBuffer9> ib;
        UINT offset=0,stride=0;
        if(FAILED(device->GetVertexShader(&vs)) || FAILED(device->GetPixelShader(&ps)) ||
            FAILED(device->GetVertexDeclaration(&declaration)) ||
            FAILED(device->GetStreamSource(0,&vb,&offset,&stride)) || FAILED(device->GetIndices(&ib)))
        {++meshProbeFailures;meshProbePreviousRow=8192;meshProbeRun=0;return;}
        const std::array<uintptr_t,14> key{meshProbeModel,meshProbePass,
            reinterpret_cast<uintptr_t>(vs.Get()),reinterpret_cast<uintptr_t>(ps.Get()),
            reinterpret_cast<uintptr_t>(declaration.Get()),reinterpret_cast<uintptr_t>(vb.Get()),
            reinterpret_cast<uintptr_t>(ib.Get()),offset,stride,static_cast<uintptr_t>(type),
            static_cast<uintptr_t>(base),minimum,vertices,start};
        unsigned hash=2166136261u;
        for(auto value:key)hash=(hash^static_cast<unsigned>(value))*16777619u;
        hash=(hash^primitives)*16777619u;
        auto index=hash&(meshProbeRows.size()-1);
        for(unsigned attempt=0;attempt<meshProbeRows.size();++attempt)
        {
            auto& row=meshProbeRows[index];
            if(!row.draws || (row.key==key && row.primitives==primitives))
            {if(!row.draws){row.key=key;row.primitives=primitives;}++row.draws;
                if(meshProbePreviousRow==index)++meshProbeRun;
                else {++row.runs;meshProbeRun=1;meshProbePreviousRow=static_cast<unsigned>(index);}
                row.longestRun=std::max(row.longestRun,meshProbeRun);
                CaptureMeshState(device,static_cast<unsigned>(index));return;}
            index=(index+1)&(meshProbeRows.size()-1);
        }
        ++meshProbeDropped;
        meshProbePreviousRow=8192;meshProbeRun=0;
    }

    // Capture once per marked phase, inside the verified native corona draw call.
    // Only getters; defer shader disassembly/logging until the hooks are detached.
    void CaptureCoronaDraw(IDirect3DDevice9* device,D3DPRIMITIVETYPE type,UINT start,UINT count)
    {
        if(!coronaDrawProbeActive || GetCurrentThreadId()!=modelProbeThread || modelProbePhase>=6) return;
        auto& sample=coronaSnapshots[modelProbePhase];
        if(sample.captured) return;
        sample.captured=true; sample.type=type; sample.start=start; sample.count=count;
        ComPtr<IDirect3DVertexDeclaration9> declaration;
        ComPtr<IDirect3DVertexShader9> vs;
        ComPtr<IDirect3DPixelShader9> ps;
        ComPtr<IDirect3DVertexBuffer9> buffer;
        bool valid=SUCCEEDED(device->GetVertexDeclaration(&declaration)) && declaration &&
            SUCCEEDED(device->GetVertexShader(&vs)) && vs &&
            SUCCEEDED(device->GetPixelShader(&ps)) && ps;
        sample.declarationCount=MAXD3DDECLLENGTH+1;
        if(valid) valid=SUCCEEDED(declaration->GetDeclaration(sample.declaration,&sample.declarationCount));
        auto capture=[](auto* shader,std::vector<DWORD>& code)
        {
            UINT bytes=0;
            if(FAILED(shader->GetFunction(nullptr,&bytes)) || !bytes || bytes>65536 || bytes%4) return false;
            code.resize(bytes/4);
            return SUCCEEDED(shader->GetFunction(code.data(),&bytes));
        };
        if(valid) valid=capture(vs.Get(),sample.vertexShader) && capture(ps.Get(),sample.pixelShader);
        valid=SUCCEEDED(device->GetStreamSource(0,&buffer,&sample.offset,&sample.stride)) && valid;
        valid=SUCCEEDED(device->GetVertexShaderConstantF(0,sample.vertexConstants[0],256)) && valid;
        valid=SUCCEEDED(device->GetPixelShaderConstantF(0,sample.pixelConstants[0],224)) && valid;
        for(unsigned i=0;i<std::size(coronaStates);++i)
            valid=SUCCEEDED(device->GetRenderState(coronaStates[i],&sample.states[i])) && valid;
        sample.valid=valid;
    }

    void __fastcall ObserveMaterialBind(void* self,void* edx,uintptr_t a,uintptr_t b,uintptr_t c,uintptr_t d)
    {
        if(GetCurrentThreadId()!=modelProbeThread || modelProbePhase>=6)
        {materialProbeHook.unsafe_fastcall(self,edx,a,b,c,d); return;}
        const auto phase=modelProbePhase;
        const auto* pass=static_cast<const char*>(self);
        meshProbePass=reinterpret_cast<uintptr_t>(self);
        auto& total=materialProbeTotals[phase];
        ++total.calls;
        total.stateEntries+=*reinterpret_cast<const uint16_t*>(pass+0x1C);
        total.vertexParameters+=*reinterpret_cast<const uint16_t*>(pass+0x08);
        total.pixelParameters+=*reinterpret_cast<const uint16_t*>(pass+0x14);
        LARGE_INTEGER before{},after{};
        QueryPerformanceCounter(&before);
        materialProbeHook.unsafe_fastcall(self,edx,a,b,c,d);
        QueryPerformanceCounter(&after);
        total.ticks+=after.QuadPart-before.QuadPart;
    }

    void __fastcall ObserveModelCommand(void* self,void* edx)
    {
        if(GetCurrentThreadId()!=modelProbeThread || modelProbePhase>=6)
        {modelProbeHook.unsafe_fastcall(self,edx); return;}
        const auto phase=modelProbePhase;
        const unsigned model=*reinterpret_cast<const uint16_t*>(static_cast<const char*>(self)+0x30);
        const auto previousModel=meshProbeModel;
        const auto previousPass=meshProbePass;
        meshProbeModel=model+1;meshProbePass=0;
        const unsigned key=model+1;
        ModelProbeRow* selected=nullptr;
        auto index=model&(modelProbeRows.size()-1);
        for(unsigned attempt=0;attempt<modelProbeRows.size();++attempt)
        {
            auto& row=modelProbeRows[index];
            if(!row.key || row.key==key)
            {
                if(!row.key)
                {
                    row.key=key;
                    const auto* info=static_cast<const char*>(modelProbeTable[model]);
                    if(info) std::memcpy(&row.hash,info+0x3C,sizeof(row.hash));
                }
                selected=&row; break;
            }
            index=(index+1)&(modelProbeRows.size()-1);
        }
        if(!selected) ++modelProbeDropped;
        LARGE_INTEGER before{},after{};
        QueryPerformanceCounter(&before);
        modelProbeHook.unsafe_fastcall(self,edx);
        meshProbeModel=previousModel;meshProbePass=previousPass;
        QueryPerformanceCounter(&after);
        if(selected)
        {++selected->calls[phase]; selected->ticks[phase]+=after.QuadPart-before.QuadPart;}
    }

    void FinishModelCommandProbe()
    {
        if(!modelProbeHook) return;
        coronaDrawBeginProbe.reset(); coronaDrawEndProbe.reset(); coronaDrawProbeActive=false;
        coronaRecordProbe.reset(); coronaVerticesProbe.reset();
        meshQueryIssueProbe.reset();
        meshPrimitiveUPProbe.reset();meshIndexedUPProbe.reset();
        materialProbeHook.reset();
        meshGeometryProbe.reset();meshGeometryCaller=0;
        meshLoopProbe.reset();meshLoopCaller=0;
        modelProbeHook.reset();
        modelProbePhase=6; modelProbeDeadline=0;
        char line[650];
        sprintf_s(line,"model-command-probe complete thread=%lu frequency=%lld dropped=%llu streams=%llu,%llu,%llu,%llu,%llu,%llu",
            modelProbeThread,modelProbeFrequency,modelProbeDropped,modelProbeStreams[0],modelProbeStreams[1],
            modelProbeStreams[2],modelProbeStreams[3],modelProbeStreams[4],modelProbeStreams[5]);
        Log(line);
        unsigned meshGroups=0,meshRepeatDraws=0;
        for(const auto& row:meshProbeRows)if(row.draws)
        {
            ++meshGroups;meshRepeatDraws+=row.draws-1;
            if(row.draws>1)
            {
                sprintf_s(line,"mesh-binding model=%u pass=%08X vs=%08X ps=%08X decl=%08X vb=%08X ib=%08X offset=%u stride=%u type=%u base=%d min=%u vertices=%u start=%u primitives=%u draws=%u indexedRuns=%u longestIndexedRun=%u",
                    row.key[0],row.key[1],row.key[2],row.key[3],row.key[4],row.key[5],row.key[6],
                    row.key[7],row.key[8],row.key[9],static_cast<int>(row.key[10]),row.key[11],row.key[12],row.key[13],row.primitives,row.draws,row.runs,row.longestRun);
                Log(line);
            }
        }
        sprintf_s(line,"mesh-binding complete streams=%llu draws=%u groups=%u repeatedDraws=%u failures=%u dropped=%u",
            modelProbeStreams[0],meshProbeDraws,meshGroups,meshRepeatDraws,meshProbeFailures,meshProbeDropped);Log(line);
        sprintf_s(line,"mesh-order queryObserved=%u occlusionBegin=%u occlusionEnd=%u otherQuery=%u nonIndexed=%u UP=%u",
            meshQueriesObserved,meshQueryBegins,meshQueryEnds,meshQueryOther,meshNonIndexed,meshUPDraws);Log(line);
        for(unsigned i=0;i<meshModelRuns.size();++i)if(meshModelRuns[i].draws>=32)
        {const auto& row=meshModelRuns[i];sprintf_s(line,"mesh-order modelKey=%u draws=%u runs=%u longest=%u",i,row.draws,row.runs,row.longest);Log(line);}
        for(const auto* probes:{&meshStateProbes,&meshUnknownStateProbes})
        for(const auto& meshState:*probes)if(meshState.selected)
        {
        sprintf_s(line,"mesh-state selected=%u row=%u modelKey=%u samples=%u failures=%u textureChanged=%04X",
            meshState.selected,meshState.row,meshProbeRows[meshState.row].key[0],meshState.samples,meshState.failures,meshState.textureChanged);Log(line);
        for(unsigned i=0;i<meshState.callerCount;++i)
        {sprintf_s(line,"mesh-state caller modelKey=%u index=%u live=%p",meshProbeRows[meshState.row].key[0],i,meshState.callers[i]);Log(line);}
        sprintf_s(line,"mesh-state compatibility streamChanged=%04X targetChanged=%02X viewportChanged=%u scissorChanged=%u vertexUsage=%08X indexUsage=%08X",
            meshState.streamChanged,meshState.targetChanged,meshState.viewportChanged,meshState.scissorChanged,meshState.vertexUsage,meshState.indexUsage);Log(line);
        for(unsigned i=0;i<std::size(meshProbeStates);++i)
        {sprintf_s(line,"mesh-state render state=%u value=%08X changed=%u",meshProbeStates[i],meshState.states[i],meshState.stateChanged[i]);Log(line);}
        for(unsigned i=0;i<13;++i)
        {sprintf_s(line,"mesh-state sampler0 state=%u value=%08X changed=%u",i+1,meshState.sampler[i],meshState.samplerChanged[i]);Log(line);}
        for(unsigned i=0;i<16;++i)if(meshState.streams[i] || i==0)
        {sprintf_s(line,"mesh-state stream=%u vb=%08X offset=%u stride=%u frequency=%08X",i,meshState.streams[i],meshState.offsets[i],meshState.strides[i],meshState.frequencies[i]);Log(line);}
        for(unsigned i=0;i<meshState.declarationCount;++i)
        {const auto& e=meshState.declaration[i];sprintf_s(line,"mesh-state declaration stream=%u offset=%u type=%u method=%u usage=%u index=%u",e.Stream,e.Offset,e.Type,e.Method,e.Usage,e.UsageIndex);Log(line);}
        for(unsigned i=0;i<256;++i)if(meshState.vertexChanged[i])
        {sprintf_s(line,"mesh-state VS changed c%u lanes=%X",i,meshState.vertexChanged[i]);Log(line);}
        for(unsigned i=0;i<224;++i)if(meshState.pixelChanged[i])
        {sprintf_s(line,"mesh-state PS changed c%u lanes=%X",i,meshState.pixelChanged[i]);Log(line);}
        if(!meshState.shader.empty())
        {
            ComPtr<ID3DXBuffer> assembly;
            if(SUCCEEDED(D3DXDisassembleShader(meshState.shader.data(),FALSE,nullptr,&assembly)))
            {Log("mesh-state VS assembly begin");Log(static_cast<const char*>(assembly->GetBufferPointer()));Log("mesh-state VS assembly end");}
        }
        if(!meshState.pixelShader.empty())
        {
            ComPtr<ID3DXBuffer> assembly;
            if(SUCCEEDED(D3DXDisassembleShader(meshState.pixelShader.data(),FALSE,nullptr,&assembly)))
            {Log("mesh-state PS assembly begin");Log(static_cast<const char*>(assembly->GetBufferPointer()));Log("mesh-state PS assembly end");}
        }
        }
        for(unsigned phase=0;phase<6;++phase)
        {
            const auto& sample=coronaSnapshots[phase];
            sprintf_s(line,"corona shader capture phase=%u captured=%u valid=%u type=%u start=%u count=%u stride=%u offset=%u",
                phase,sample.captured,sample.valid,sample.type,sample.start,sample.count,sample.stride,sample.offset); Log(line);
            if(sample.recordCaptured)
            {
                sprintf_s(line,"corona record phase=%u flags=%u,%u words=%08X,%08X,%08X,%08X,%08X,%08X,%08X,%08X",
                    phase,sample.flags[0],sample.flags[1],sample.record[0],sample.record[1],sample.record[2],sample.record[3],
                    sample.record[4],sample.record[5],sample.record[6],sample.record[7]); Log(line);
                sprintf_s(line,"corona basis phase=%u (%.9g %.9g %.9g) (%.9g %.9g %.9g) (%.9g %.9g %.9g)",phase,
                    sample.basis[0],sample.basis[1],sample.basis[2],sample.basis[3],sample.basis[4],sample.basis[5],
                    sample.basis[6],sample.basis[7],sample.basis[8]); Log(line);
                for(unsigned i=0;i<sample.vertexCount;++i)
                {
                    const auto* v=sample.vertices[i];
                    sprintf_s(line,"corona vertex phase=%u index=%u words=%08X,%08X,%08X,%08X,%08X,%08X,%08X,%08X,%08X",
                        phase,i,v[0],v[1],v[2],v[3],v[4],v[5],v[6],v[7],v[8]); Log(line);
                }
            }
            if(!sample.valid) continue;
            for(UINT i=0;i<sample.declarationCount;++i)
            {
                const auto& e=sample.declaration[i];
                sprintf_s(line,"corona declaration phase=%u stream=%u offset=%u type=%u method=%u usage=%u index=%u",
                    phase,e.Stream,e.Offset,e.Type,e.Method,e.Usage,e.UsageIndex); Log(line);
            }
            for(unsigned i=0;i<std::size(coronaStates);++i)
            {sprintf_s(line,"corona state phase=%u state=%u value=%u",phase,coronaStates[i],sample.states[i]); Log(line);}
            for(unsigned stage=0;stage<2;++stage)
            {
                const auto& code=stage ? sample.pixelShader : sample.vertexShader;
                ComPtr<ID3DXBuffer> assembly;
                sprintf_s(line,"corona shader phase=%u stage=%s bytes=%zu",phase,stage ? "PS":"VS",code.size()*4); Log(line);
                if(SUCCEEDED(D3DXDisassembleShader(code.data(),FALSE,nullptr,&assembly)))
                {
                    const std::string text(static_cast<const char*>(assembly->GetBufferPointer()),
                        std::min<size_t>(assembly->GetBufferSize(),48000)); Log(text.c_str());
                }
                const auto* constants=stage ? sample.pixelConstants : sample.vertexConstants;
                for(unsigned i=0;i<(stage ? 224u:256u);++i)
                {sprintf_s(line,"corona %s phase=%u c%u=(%.9g %.9g %.9g %.9g)",stage ? "PS":"VS",phase,i,
                    constants[i][0],constants[i][1],constants[i][2],constants[i][3]); Log(line);}
            }
        }
        for(unsigned phase=0;phase<6;++phase)
        {
            const auto& total=materialProbeTotals[phase];
            sprintf_s(line,"material-bind phase=%u calls=%llu ticks=%llu stateEntries=%llu vertexParameters=%llu pixelParameters=%llu",
                phase,total.calls,total.ticks,total.stateEntries,total.vertexParameters,total.pixelParameters);
            Log(line);
        }
        for(const auto& row:modelProbeRows) if(row.key)
        {
            sprintf_s(line,"model-command model=%u hash=%08X calls=%llu,%llu,%llu,%llu,%llu,%llu ticks=%llu,%llu,%llu,%llu,%llu,%llu",
                row.key-1,row.hash,row.calls[0],row.calls[1],row.calls[2],row.calls[3],row.calls[4],row.calls[5],
                row.ticks[0],row.ticks[1],row.ticks[2],row.ticks[3],row.ticks[4],row.ticks[5]);
            Log(line);
        }
    }

    void UpdateModelCommandProbe(IDirect3DDevice9* device)
    {
        if(modelProbeHook && GetTickCount64()>=modelProbeDeadline) FinishModelCommandProbe();
        if(!modelProbeRequested.exchange(false) || modelProbeHook) return;
        if(vertexProbeHook || vertexEmissionHook || billboardBatchHook || rigidInstances || meshPacketProbe || vertexBinderBatchHook)
        {Log("model-command-probe unavailable: disable other native probes first"); return;}
        // Verified CE008DCBF0: thiscall, command model ushort+30, plain RET.
        auto entry=hook::pattern("55 8B EC 83 E4 F0 83 EC 44 53 56 8B F1 57 0F B7 46 30 8B 04 85 ? ? ? ? 85 C0");
        if(entry.size()!=1)
        {Log("model-command-probe unavailable: unique entry signature not found"); return;}
        modelProbeTable=*entry.get_first<void**>(21);
        modelProbeRows={}; modelProbeDropped=0;
        for(auto& row:meshProbeRows)row={};
        for(auto& sample:meshStateProbes)sample={};
        for(auto& sample:meshUnknownStateProbes)sample={};
        meshProbeModel=meshProbeDraws=meshProbeDropped=meshProbeFailures=0;meshProbePass=0;
        meshProbePreviousRow=8192;meshProbeRun=0;
        meshModelRuns={};MeshSequenceBarrier();meshNonIndexed=0;
        meshQueriesObserved=false;meshQueryBegins=meshQueryEnds=meshQueryOther=0;
        meshUPDraws=0;
        for(auto& sample:coronaSnapshots) sample={};
        for(auto& total:materialProbeTotals) total={};
        for(auto& count:modelProbeStreams) count=0;
        LARGE_INTEGER frequency{}; QueryPerformanceFrequency(&frequency);
        modelProbeFrequency=frequency.QuadPart;
        modelProbeThread=GetCurrentThreadId(); modelProbePhase=6;
        modelProbeHook=safetyhook::create_inline(entry.get_first(),ObserveModelCommand);
        if(modelProbeHook)
        {
            // A throwaway query identifies this backend's Issue implementation.
            // Observe only: forward every native query and detach with the probe.
            ComPtr<IDirect3DQuery9> query;
            if(SUCCEEDED(device->CreateQuery(D3DQUERYTYPE_OCCLUSION,&query)) && query)
            {auto** table=*reinterpret_cast<void***>(query.Get());meshQueryIssueProbe=safetyhook::create_inline(table[6],ObserveMeshQueryIssue);}
            meshQueriesObserved=static_cast<bool>(meshQueryIssueProbe);
            auto** deviceTable=*reinterpret_cast<void***>(device);
            meshPrimitiveUPProbe=safetyhook::create_inline(deviceTable[83],ObserveMeshPrimitiveUP);
            meshIndexedUPProbe=safetyhook::create_inline(deviceTable[84],ObserveMeshIndexedUP);
            Log(meshPrimitiveUPProbe && meshIndexedUPProbe ? "mesh UP barriers observed":"mesh UP barriers unavailable");
            // Verified CE004373E0: thiscall and four stack arguments, RET10.
            // Pure observation; this function binds states and both shader stages.
            auto material=hook::pattern("53 55 8B E9 56 0F B7 5D 1C 33 F6 57 85 DB");
            if(material.size()==1)
                materialProbeHook=safetyhook::create_inline(material.get_first(),ObserveMaterialBind);
            Log(materialProbeHook ? "material-bind observer attached" : "material-bind observer unavailable");
            // Live CE00463490: geometry thiscall, no stack arguments, plain RET.
            // Capture the virtual dispatch return before the frame-pointer chain is lost.
            auto geometry=hook::pattern("57 8B F9 8B 57 04 85 D2 0F 84 ? ? ? ? 53 33 DB 39 5F 48 55 8B 6F 44");
            if(geometry.size()==1)
                meshGeometryProbe=safetyhook::create_inline(geometry.get_first(),ObserveMeshGeometry);
            Log(meshGeometryProbe ? "mesh geometry caller observer attached":"mesh geometry caller observer unavailable");
            // Live CE00444C50: thiscall, three stack arguments, RET0C.
            auto loop=hook::pattern("55 8B EC 83 E4 F8 83 EC 20 A1 ? ? ? ? 56 57 8B F9 89 44 24 24 8B 47 0C");
            if(loop.size()==1)
                meshLoopProbe=safetyhook::create_inline(loop.get_first(),ObserveMeshLoop);
            Log(meshLoopProbe ? "mesh loop caller observer attached":"mesh loop caller observer unavailable");
            // CE00C1F940 CALL / 00C1F945 continuation, bracket the native batch draw.
            auto corona=hook::pattern("E8 ? ? ? ? 8B 45 AC 2B C6 89 45 AC 0F 85 ? ? ? ? E8 ? ? ? ? 51 C7 04 24 00 00 80 3F");
            if(corona.size()==1)
            {
                coronaDrawBeginProbe=safetyhook::create_mid(corona.get_first(),[](SafetyHookContext&)
                {coronaDrawProbeActive=GetCurrentThreadId()==modelProbeThread && modelProbePhase<6;});
                coronaDrawEndProbe=safetyhook::create_mid(corona.get_first<char>(5),[](SafetyHookContext&)
                {coronaDrawProbeActive=false;});
            }
            if(!coronaDrawBeginProbe || !coronaDrawEndProbe)
            {coronaDrawBeginProbe.reset(); coronaDrawEndProbe.reset(); Log("corona shader observer unavailable");}
            else Log("corona shader observer attached: getters once per phase");
            auto record=hook::pattern("0F 28 81 ? ? ? ? 81 CE 00 FF FF FF C1 E6 08 0B F0");
            auto writer=hook::pattern("83 3D ? ? ? ? 00 74 ? A1 ? ? ? ? 85 C0 74 ? F3 0F 10 44 24 04 8B C8 83 C0 24");
            if(record.size()==1 && writer.size()==1)
            {
                coronaProbePool=*record.get_first<uintptr_t>(3);
                coronaRecordProbe=safetyhook::create_mid(record.get_first(),[](SafetyHookContext& regs)
                {
                    if(GetCurrentThreadId()!=modelProbeThread || modelProbePhase>=6) return;
                    auto& sample=coronaSnapshots[modelProbePhase];
                    if(sample.recordCaptured) return;
                    sample.recordCaptured=true;
                    std::memcpy(sample.record,reinterpret_cast<const void*>(coronaProbePool+regs.ecx),32);
                    sample.flags[0]=*reinterpret_cast<const unsigned char*>(regs.ebp-0xAC);
                    sample.flags[1]=*reinterpret_cast<const unsigned char*>(regs.ebp-0xCC);
                    constexpr unsigned offsets[]{0xA0,0xA8,0xC8,0xC0,0xA4,0x88,0x140,0x148,0x130};
                    for(unsigned i=0;i<9;++i) std::memcpy(&sample.basis[i],reinterpret_cast<const void*>(regs.ebp-offsets[i]),4);
                });
                coronaProbeModuleBase=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
                coronaVerticesProbe=safetyhook::create_mid(writer.get_first(),[](SafetyHookContext& regs)
                {
                    if(GetCurrentThreadId()!=modelProbeThread || modelProbePhase>=6) return;
                    auto& sample=coronaSnapshots[modelProbePhase];
                    if(!sample.recordCaptured || sample.vertexCount>=6) return;
                    const auto caller=*reinterpret_cast<const uintptr_t*>(regs.esp);
                    if(caller<coronaProbeModuleBase+0x81F170 || caller>=coronaProbeModuleBase+0x81F993) return;
                    std::memcpy(sample.vertices[sample.vertexCount++],reinterpret_cast<const void*>(regs.esp+4),36);
                });
            }
            if(!coronaRecordProbe || !coronaVerticesProbe)
            {coronaRecordProbe.reset();coronaVerticesProbe.reset();Log("corona record observer unavailable");}
            else Log("corona record observer attached: first compact record and six vertices per phase");
            modelProbeDeadline=GetTickCount64()+5000;
            Log("model-command-probe armed: five seconds; exclude from performance captures");
        }
        else Log("model-command-probe unavailable: hook creation failed");
    }

    struct GpuBillboardRecord {float center[3],radius,brightness,push;DWORD color,flags;};
    struct GpuBillboardCorner {float corner[4],right[3],up[3];};
    static_assert(sizeof(GpuBillboardRecord)==32);
    struct GpuBillboards
    {
        static constexpr unsigned capacity=2730;
        IDirect3DDevice9* device=nullptr;
        ComPtr<IDirect3DVertexBuffer9> corners,instances;
        ComPtr<IDirect3DIndexBuffer9> indices;
        ComPtr<IDirect3DVertexDeclaration9> declaration;
        ComPtr<IDirect3DVertexShader9> sourceShader,shader,rejectedShader;
        std::array<GpuBillboardRecord,capacity> records{};
        uintptr_t pool=0,continuation=0;
        unsigned long long batches=0,lights=0,fallbacks=0;
    };
    std::unique_ptr<GpuBillboards> gpuBillboards;

    bool DrawGpuBillboards(IDirect3DDevice9* device,GpuBillboards& state,unsigned count,const float* basis)
    {
        ComPtr<IDirect3DVertexShader9> vs;
        ComPtr<IDirect3DVertexDeclaration9> declaration;
        ComPtr<IDirect3DVertexBuffer9> streams[2];
        ComPtr<IDirect3DIndexBuffer9> indices;
        UINT offsets[2]{},strides[2]{},frequencies[2]{};
        if(FAILED(device->GetVertexShader(&vs)) || !vs || FAILED(device->GetVertexDeclaration(&declaration)) ||
            FAILED(device->GetIndices(&indices))) return false;
        for(unsigned i=0;i<2;++i)
            if(FAILED(device->GetStreamSource(i,&streams[i],&offsets[i],&strides[i])) ||
                FAILED(device->GetStreamSourceFreq(i,&frequencies[i]))) return false;
        if(state.sourceShader.Get()!=vs.Get())
        {
            if(state.rejectedShader.Get()==vs.Get()) return false;
            state.rejectedShader=vs;
            UINT bytes=0;vs->GetFunction(nullptr,&bytes);
            if(!bytes || bytes>65536 || bytes%4) return false;
            std::vector<DWORD> code(bytes/4);ComPtr<ID3DXBuffer> assembly,assembled,errors;
            if(FAILED(vs->GetFunction(code.data(),&bytes)) || FAILED(D3DXDisassembleShader(code.data(),FALSE,nullptr,&assembly))) return false;
            std::string output;
            if(!VRBillboardShader::AddInstanceExpansion(static_cast<const char*>(assembly->GetBufferPointer()),output,true) ||
                FAILED(D3DXAssembleShader(output.data(),static_cast<UINT>(output.size()),nullptr,nullptr,0,&assembled,&errors))) return false;
            ComPtr<IDirect3DVertexShader9> shader;
            if(FAILED(device->CreateVertexShader(static_cast<const DWORD*>(assembled->GetBufferPointer()),&shader))) return false;
            state.shader=std::move(shader);state.sourceShader=vs;
            state.rejectedShader.Reset();
            Log("billboard-instancing-test native shader adapted");
        }
        constexpr float unit[4][4]{{-1,1,0,1},{1,1,1,1},{1,-1,1,0},{-1,-1,0,0}};
        GpuBillboardCorner corners[4]{};
        for(unsigned i=0;i<4;++i)
        {std::memcpy(corners[i].corner,unit[i],16);std::memcpy(corners[i].right,basis,12);std::memcpy(corners[i].up,basis+3,12);}
        void* mapped=nullptr;
        if(FAILED(state.corners->Lock(0,sizeof(corners),&mapped,D3DLOCK_DISCARD))) return false;
        std::memcpy(mapped,corners,sizeof(corners));if(FAILED(state.corners->Unlock())) return false;
        if(FAILED(state.instances->Lock(0,count*32,&mapped,D3DLOCK_DISCARD))) return false;
        std::memcpy(mapped,state.records.data(),count*32);if(FAILED(state.instances->Unlock())) return false;
        const bool configured=SUCCEEDED(device->SetVertexShader(state.shader.Get())) &&
            SUCCEEDED(device->SetVertexDeclaration(state.declaration.Get())) &&
            SUCCEEDED(device->SetStreamSource(0,state.corners.Get(),0,sizeof(GpuBillboardCorner))) &&
            SUCCEEDED(device->SetStreamSource(1,state.instances.Get(),0,32)) &&
            SUCCEEDED(device->SetStreamSourceFreq(0,D3DSTREAMSOURCE_INDEXEDDATA|count)) &&
            SUCCEEDED(device->SetStreamSourceFreq(1,static_cast<UINT>(D3DSTREAMSOURCE_INSTANCEDATA)|1u)) &&
            SUCCEEDED(device->SetIndices(state.indices.Get()));
        const bool drawn=configured && SUCCEEDED(device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,0,0,4,0,2));
        // Restore actual bindings so native RAGE caches remain consistent.
        const auto restoredShader=device->SetVertexShader(vs.Get());
        const auto restoredDeclaration=device->SetVertexDeclaration(declaration.Get());
        bool restored=SUCCEEDED(restoredShader) && SUCCEEDED(restoredDeclaration);
        for(unsigned i=0;i<2;++i)
        {
            const auto a=device->SetStreamSource(i,streams[i].Get(),offsets[i],strides[i]);
            const auto b=device->SetStreamSourceFreq(i,frequencies[i]);
            restored=SUCCEEDED(a) && SUCCEEDED(b) && restored;
        }
        restored=SUCCEEDED(device->SetIndices(indices.Get())) && restored;
        if(!restored) {billboardInstancingRequested=false;Log("billboard-instancing-test binding restoration failed; disabling");}
        return drawn;
    }

    void GpuBillboardBatch(SafetyHookContext& regs)
    {
        if(!gpuBillboards || GetCurrentThreadId()!=routingThread || renderEye<0 || renderStage!=3) return;
        auto& state=*gpuBillboards;
        const auto read=[&](unsigned offset){return *reinterpret_cast<const unsigned*>(regs.ebp-offset);};
        const auto remaining=regs.eax,total=read(0xD4),phase=read(0x114);
        if(!remaining || remaining>49152 || total>49152 || phase>=3 || regs.edi>total ||
            *reinterpret_cast<const unsigned char*>(regs.ebp-0xAC) || *reinterpret_cast<const unsigned char*>(regs.ebp-0xCC))
        {++state.fallbacks;return;}
        const unsigned count=std::min<unsigned>(remaining,GpuBillboards::capacity);
        const auto* mask=*reinterpret_cast<const unsigned char**>(regs.ebp-0x124);
        unsigned next=regs.edi,selected=0;
        while(selected<count && next<total)
        {
            if(mask[next]==1)
                std::memcpy(&state.records[selected++],reinterpret_cast<const void*>(state.pool+(phase*49152+next)*32),32);
            ++next;
        }
        if(selected!=count) {++state.fallbacks;return;}
        float basis[6]{};constexpr unsigned offsets[]{0xA0,0xA8,0xC8,0xC0,0xA4,0x88};
        for(unsigned i=0;i<6;++i)std::memcpy(&basis[i],reinterpret_cast<const void*>(regs.ebp-offsets[i]),4);
        if(!DrawGpuBillboards(state.device,state,count,basis)) {++state.fallbacks;return;}
        ++state.batches;state.lights+=count;
        regs.edi=next;regs.esi=count;regs.eip=state.continuation;
    }

    void StopGpuBillboards()
    {
        billboardBatchHook.reset();
        if(gpuBillboards)
        {
            char line[200];sprintf_s(line,"billboard-instancing-test off batches=%llu lights=%llu fallbacks=%llu",
                gpuBillboards->batches,gpuBillboards->lights,gpuBillboards->fallbacks);Log(line);
        }
        gpuBillboards.reset();
    }

    void UpdateGpuBillboards(IDirect3DDevice9* device)
    {
        if(!billboardInstancingRequested.load()) {if(gpuBillboards || billboardBatchHook)StopGpuBillboards();return;}
        if(billboardBatchHook) return;
        if(modelProbeHook || vertexProbeHook || vertexEmissionHook || meshPacketProbe || vertexBinderBatchHook) return;
        auto batch=hook::pattern("BE AA 0A 00 00 3B C6 0F 4E F0 89 B5 E4 FE FF FF");
        auto record=hook::pattern("0F 28 81 ? ? ? ? 81 CE 00 FF FF FF C1 E6 08 0B F0");
        auto draw=hook::pattern("E8 ? ? ? ? 8B 45 AC 2B C6 89 45 AC 0F 85 ? ? ? ? E8 ? ? ? ? 51 C7 04 24 00 00 80 3F");
        if(batch.size()!=1 || record.size()!=1 || draw.size()!=1)
        {billboardInstancingRequested=false;Log("billboard-instancing-test unavailable: native signature mismatch");return;}
        auto owned=std::make_unique<GpuBillboards>();owned->device=device;
        owned->pool=*record.get_first<uintptr_t>(3);owned->continuation=reinterpret_cast<uintptr_t>(draw.get_first<char>(5));
        const D3DVERTEXELEMENT9 declaration[]{
            {0,0,D3DDECLTYPE_FLOAT4,0,D3DDECLUSAGE_POSITION,0},{0,16,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_TEXCOORD,1},
            {0,28,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_TEXCOORD,2},{1,0,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_NORMAL,0},
            {1,12,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_TEXCOORD,0},{1,24,D3DDECLTYPE_D3DCOLOR,0,D3DDECLUSAGE_COLOR,0},D3DDECL_END()};
        const WORD indices[]{0,1,2,2,3,0};void* mapped=nullptr;
        const bool allocated=SUCCEEDED(device->CreateVertexBuffer(4*sizeof(GpuBillboardCorner),D3DUSAGE_DYNAMIC|D3DUSAGE_WRITEONLY,0,D3DPOOL_DEFAULT,&owned->corners,nullptr)) &&
            SUCCEEDED(device->CreateVertexBuffer(GpuBillboards::capacity*32,D3DUSAGE_DYNAMIC|D3DUSAGE_WRITEONLY,0,D3DPOOL_DEFAULT,&owned->instances,nullptr)) &&
            SUCCEEDED(device->CreateIndexBuffer(sizeof(indices),D3DUSAGE_WRITEONLY,D3DFMT_INDEX16,D3DPOOL_DEFAULT,&owned->indices,nullptr)) &&
            SUCCEEDED(device->CreateVertexDeclaration(declaration,&owned->declaration)) &&
            SUCCEEDED(owned->indices->Lock(0,sizeof(indices),&mapped,0));
        if(!allocated) {billboardInstancingRequested=false;Log("billboard-instancing-test allocation failed");return;}
        std::memcpy(mapped,indices,sizeof(indices));
        if(FAILED(owned->indices->Unlock())) {billboardInstancingRequested=false;return;}
        gpuBillboards=std::move(owned);billboardBatchHook=safetyhook::create_mid(batch.get_first(),GpuBillboardBatch);
        if(!billboardBatchHook){billboardInstancingRequested=false;gpuBillboards.reset();Log("billboard-instancing-test hook unavailable");return;}
        Log("billboard-instancing-test on: main eyes only, normal native mode, compact32-byte records; native fallback retained");
    }

    bool AdaptRigidShader(IDirect3DDevice9* device,const std::vector<DWORD>& code,bool tangent,ComPtr<IDirect3DVertexShader9>& shader)
    {
        ComPtr<ID3DXBuffer> assembly,assembled,errors;std::string output;
        if(FAILED(D3DXDisassembleShader(code.data(),FALSE,nullptr,&assembly)) ||
            !VRRigidInstanceShader::AddInstanceMatrices(static_cast<const char*>(assembly->GetBufferPointer()),output,tangent) ||
            FAILED(D3DXAssembleShader(output.data(),static_cast<UINT>(output.size()),nullptr,nullptr,0,&assembled,&errors)))return false;
        return SUCCEEDED(device->CreateVertexShader(static_cast<const DWORD*>(assembled->GetBufferPointer()),&shader));
    }
    void FlushLiveMesh(IDirect3DDevice9* device)
    {
        if(!device || !liveMeshPackets || !OnRoutingThread() || renderEye<0 || renderStage!=1)return;
        const auto result=liveMeshPackets->FlushLive(device,renderEye,liveMeshRenderer,
            [device](const auto& code,bool tangent,auto& shader){return AdaptRigidShader(device,code,tangent,shader);},
            [device](D3DPRIMITIVETYPE type,INT base,UINT first,UINT vertices,UINT start,UINT primitives)
            {return drawIndexedPrimitive.unsafe_stdcall<HRESULT>(device,type,base,first,vertices,start,primitives);});
        if(FAILED(result))Log("mesh-live-probe submission/restoration failed; remaining draws stay native");
    }
    HRESULT WINAPI ClearLiveMesh(IDirect3DDevice9* device,DWORD count,const D3DRECT* rects,DWORD flags,D3DCOLOR color,float z,DWORD stencil)
    {
        FlushLiveMesh(device);
        return liveMeshClear.unsafe_stdcall<HRESULT>(device,count,rects,flags,color,z,stencil);
    }
    void LiveMeshResourceBarrier()
    {
        if(liveMeshPackets && OnRoutingThread() && RageDirect3DDevice9::m_pRealDevice)
            FlushLiveMesh(*RageDirect3DDevice9::m_pRealDevice);
    }
    HRESULT WINAPI LockLiveMeshVB(IDirect3DVertexBuffer9* buffer,UINT offset,UINT bytes,void** data,DWORD flags)
    {LiveMeshResourceBarrier();return liveMeshVBLock.unsafe_stdcall<HRESULT>(buffer,offset,bytes,data,flags);}
    HRESULT WINAPI LockLiveMeshIB(IDirect3DIndexBuffer9* buffer,UINT offset,UINT bytes,void** data,DWORD flags)
    {LiveMeshResourceBarrier();return liveMeshIBLock.unsafe_stdcall<HRESULT>(buffer,offset,bytes,data,flags);}
    HRESULT WINAPI LockLiveMeshTexture(IDirect3DTexture9* texture,UINT level,D3DLOCKED_RECT* data,const RECT* rect,DWORD flags)
    {LiveMeshResourceBarrier();return liveMeshTextureLock.unsafe_stdcall<HRESULT>(texture,level,data,rect,flags);}
    HRESULT WINAPI UpdateLiveMeshTexture(IDirect3DDevice9* device,IDirect3DBaseTexture9* source,IDirect3DBaseTexture9* destination)
    {FlushLiveMesh(device);return liveMeshUpdateTexture.unsafe_stdcall<HRESULT>(device,source,destination);}
    HRESULT WINAPI UpdateLiveMeshSurface(IDirect3DDevice9* device,IDirect3DSurface9* source,const RECT* rect,IDirect3DSurface9* destination,const POINT* point)
    {FlushLiveMesh(device);return liveMeshUpdateSurface.unsafe_stdcall<HRESULT>(device,source,rect,destination,point);}
    HRESULT WINAPI FillLiveMeshColor(IDirect3DDevice9* device,IDirect3DSurface9* surface,const RECT* rect,D3DCOLOR color)
    {FlushLiveMesh(device);return liveMeshColorFill.unsafe_stdcall<HRESULT>(device,surface,rect,color);}
    void WINAPI GenerateLiveMeshMips(IDirect3DTexture9* texture)
    {LiveMeshResourceBarrier();liveMeshGenerateMips.unsafe_stdcall(texture);}
    void RemoveLiveMeshResourceHooks()
    {liveMeshVBLock.reset();liveMeshIBLock.reset();liveMeshTextureLock.reset();liveMeshUpdateTexture.reset();liveMeshUpdateSurface.reset();liveMeshColorFill.reset();liveMeshGenerateMips.reset();}
    const VRMeshPacketProbe::ShaderReads* InspectMeshPacketShaders(IDirect3DVertexShader9* vs,IDirect3DPixelShader9* ps,IDirect3DVertexDeclaration9* decl)
    {
        auto found=std::find_if(meshPacketShaders.begin(),meshPacketShaders.end(),[&](const auto& s)
            {return s.vs.Get()==vs && s.ps.Get()==ps && s.declaration.Get()==decl;});
        if(found!=meshPacketShaders.end())return found->valid ? &found->reads:nullptr;
        if(meshPacketShaders.size()==(meshCompareOrder ? 128u:64u))return nullptr;
        meshPacketShaders.emplace_back();auto& selected=meshPacketShaders.back();
        selected.vs=vs;selected.ps=ps;selected.declaration=decl;
        D3DVERTEXELEMENT9 elements[MAXD3DDECLLENGTH+1]{};UINT count=std::size(elements);
        if(FAILED(decl->GetDeclaration(elements,&count)) || (count!=5 && count!=6))return nullptr;
        constexpr BYTE usages[]{D3DDECLUSAGE_POSITION,D3DDECLUSAGE_NORMAL,D3DDECLUSAGE_COLOR,D3DDECLUSAGE_TEXCOORD,D3DDECLUSAGE_TANGENT};
        constexpr BYTE types[]{D3DDECLTYPE_FLOAT3,D3DDECLTYPE_FLOAT3,D3DDECLTYPE_D3DCOLOR,D3DDECLTYPE_FLOAT2,D3DDECLTYPE_FLOAT4};
        constexpr WORD offsets[]{0,12,24,28,36};
        for(unsigned i=0;i<count-1;++i)
            if(elements[i].Stream || elements[i].Offset!=offsets[i] || elements[i].Type!=types[i] ||
                elements[i].Method || elements[i].Usage!=usages[i] || elements[i].UsageIndex)return nullptr;
        if(elements[count-1].Stream!=0xff || elements[count-1].Type!=D3DDECLTYPE_UNUSED)return nullptr;
        const auto assembly=[](auto* shader,std::string& output)
        {
            UINT bytes=0;if(FAILED(shader->GetFunction(nullptr,&bytes)) || !bytes || bytes>65536 || bytes%4)return false;
            std::vector<DWORD> code(bytes/4);ComPtr<ID3DXBuffer> text;
            if(FAILED(shader->GetFunction(code.data(),&bytes)) || FAILED(D3DXDisassembleShader(code.data(),FALSE,nullptr,&text)))return false;
            output=static_cast<const char*>(text->GetBufferPointer());return true;
        };
        std::string vertex,pixel,prefix;
        if(!assembly(vs,vertex) || !assembly(ps,pixel) || !VRRigidInstanceShader::AddInstanceMatrices(vertex,prefix,count==6))return nullptr;
        selected.valid=VRMeshPacketProbe::ReadAssembly(vertex,selected.reads.vertex,std::span(selected.reads.samplers).subspan(16,4)) &&
            VRMeshPacketProbe::ReadAssembly(pixel,selected.reads.pixel,std::span(selected.reads.samplers).first(16));
        return selected.valid ? &selected.reads:nullptr;
    }
    void UpdateCpuOmission()
    {
        auto request=cpuOmitRequested.exchange(99);
        if(request==99 && cpuOmitMode.load() && GetTickCount64()>=cpuOmitDeadline.load())request=0;
        if(request==99)return;
        cpuOmitHook.reset();cpuOmitMode=0;cpuOmitDeadline=0;
        if(!request){Log("cpu-omit-test off: native callbacks restored");return;}
        if(modelProbeHook || vertexProbeHook || vertexEmissionHook || rigidInstances || gpuBillboards || meshPacketProbe || liveMeshMode || vertexBinderBatchHook)
        {Log("cpu-omit-test unavailable: another experiment is active");return;}
        if(request>=3)
        {
            cpuOmitSkipped[0]=0;cpuOmitSkipped[1]=0;cpuOmitDeadline=GetTickCount64()+90000;cpuOmitMode=request;
            Log("cpu-omit-test phase omission active for90s: native main-eye list construction and execution removed; images deliberately invalid");return;
        }
        // Upper-bound experiment, not a GPU replacement. Preserve simulation,
        // list building, command ownership and non-main rendering callbacks.
        // Deliberately omit selected native preparation AND its draw descendants.
        auto model=hook::pattern("55 8B EC 83 E4 F0 83 EC 44 53 56 8B F1 57 0F B7 46 30 8B 04 85 ? ? ? ? 85 C0");
        auto material=hook::pattern("55 8B EC 83 E4 F8 83 EC 20 A1 ? ? ? ? 56 57 8B F9 89 44 24 24 8B 47 0C");
        auto& entry=request==2 ? model:material;
        if(entry.size()!=1){Log("cpu-omit-test unavailable: unique native signature missing");return;}
        cpuOmitHook=safetyhook::create_inline(entry.get_first(),request==2 ? reinterpret_cast<void*>(OmitModelWork):reinterpret_cast<void*>(OmitMaterialWork));
        if(!cpuOmitHook){Log("cpu-omit-test unavailable: hook failed");return;}
        cpuOmitSkipped[0]=0;cpuOmitSkipped[1]=0;cpuOmitDeadline=GetTickCount64()+90000;cpuOmitMode=request;
        Log(request==2 ? "cpu-omit-test models: main eye GBuffer/DrawScene callback008DCBF0 omitted for90s; deliberately missing geometry; CPU+draw removal upper bound":
            "cpu-omit-test materials: main eye GBuffer/DrawScene loop00444C50 omitted for90s; deliberately missing geometry; CPU+draw removal upper bound");
    }
    void UpdateMeshPacketProbe(IDirect3DDevice9* device)
    {
        const bool benchmark=meshBenchmarkRequested.exchange(false);
        const bool packet=meshPacketRequested.exchange(false),replay=meshReplayRequested.exchange(false) || benchmark;
        const bool order=meshOrderRequested.exchange(false) || replay;
        if(!packet && !order)return;
        if(meshPacketProbe || modelProbeHook || vertexProbeHook || vertexEmissionHook || rigidInstances || gpuBillboards || vertexBinderBatchHook)
        {Log("mesh-packet-probe unavailable: disable other experiments first");return;}
        meshCompareOrder=order;meshPacketMaterial=0;
        // Verified CE004373E0, thiscall four stack arguments RET10. Observe
        // native pass identity after its original state/resource binding runs.
        auto material=hook::pattern("53 55 8B E9 56 0F B7 5D 1C 33 F6 57 85 DB");
        if(material.size()==1)meshPacketMaterialProbe=safetyhook::create_inline(material.get_first(),ObservePacketMaterial);
        Log(meshPacketMaterialProbe ? "mesh-recipe-probe native material identity observer attached":"mesh-recipe-probe material identity unavailable");
        meshPacketShaders.clear();
        try {meshPacketProbe=std::make_unique<VRMeshPacketProbe::Probe>(order,replay,benchmark);}
        catch(const std::bad_alloc&){meshPacketMaterialProbe.reset();Log("mesh-packet-probe unavailable: allocation failed");return;}
        ComPtr<IDirect3DQuery9> query;
        if(SUCCEEDED(device->CreateQuery(D3DQUERYTYPE_OCCLUSION,&query)) && query)
        {auto** table=*reinterpret_cast<void***>(query.Get());meshQueryIssueProbe=safetyhook::create_inline(table[6],ObserveMeshQueryIssue);}
        auto** table=*reinterpret_cast<void***>(device);
        meshPrimitiveUPProbe=safetyhook::create_inline(table[83],ObserveMeshPrimitiveUP);
        meshIndexedUPProbe=safetyhook::create_inline(table[84],ObserveMeshIndexedUP);
        if(!meshQueryIssueProbe || !meshPrimitiveUPProbe || !meshIndexedUPProbe)
        {meshPacketProbe.reset();meshPacketShaders.clear();meshPacketMaterialProbe.reset();meshPacketMaterial=0;meshQueryIssueProbe.reset();meshPrimitiveUPProbe.reset();meshIndexedUPProbe.reset();
            Log("mesh-packet-probe unavailable: query/UP side-effect observers missing");return;}
        Log(replay ? "mesh-replay-probe armed: all indexed packets, unsupported ordering barriers, actual native color comparison; scratch only":
            order ? "mesh-order-probe armed: captured subset original-order/grouped scratch comparison; exclude from timing":
            "mesh-packet-probe armed: one eye0 GBuffer capture; scratch comparison; exclude from timing");
    }
    void UpdateRigidInstances(IDirect3DDevice9* device)
    {
        if(rigidInstances && (!rigidInstancingRequested || rigidInstances->restorationFailed))
        {
            char line[240];sprintf_s(line,"rigid-instancing-test off draws=%llu,%llu fallbacks=%llu,%llu failures=%llu restorationFailed=%u",
                rigidInstances->draws[0],rigidInstances->draws[1],rigidInstances->fallbacks[0],
                rigidInstances->fallbacks[1],rigidInstances->failures,rigidInstances->restorationFailed);
            Log(line);rigidInstances.reset();rigidInstancingRequested=false;
        }
        if(!rigidInstancingRequested || rigidInstances)return;
        if(modelProbeHook || vertexProbeHook || vertexEmissionHook || meshPacketProbe || vertexBinderBatchHook)
        {rigidInstancingRequested=false;Log("rigid-instancing-test unavailable: disable native observers first");return;}
        D3DCAPS9 caps{};
        if(FAILED(device->GetDeviceCaps(&caps)) || caps.VertexShaderVersion<D3DVS_VERSION(3,0) ||
            caps.MaxStreams<2 || caps.MaxStreamStride<128)
        {rigidInstancingRequested=false;Log("rigid-instancing-test unsupported device caps");return;}
        rigidInstances=std::make_unique<VRRigidInstance::Renderer>();
        Log("rigid-instancing-test on: main-eye GBuffer only, one instance, native order and fallback; compatibility test, not batching");
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

    thread_local VRProfile::Total routingTime[2]{};
    IDirect3DSurface9* MapSurfaceImpl(IDirect3DSurface9* input,int eye)
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

    IDirect3DBaseTexture9* MapTextureImpl(IDirect3DBaseTexture9* input,int eye)
    {
        if(!input || !resources) return input;
        if(!textureCacheBypass)
            return resources->textureRoutes.Map(input,eye,[](IDirect3DBaseTexture9* texture)
            {
                for(auto& target:resources->targets)
                    if(texture==target.original.Get() || texture==target.texture[0].Get() || texture==target.texture[1].Get())
                        return std::array<IDirect3DBaseTexture9*,3>{target.original.Get(),target.texture[0].Get(),target.texture[1].Get()};
                return std::array<IDirect3DBaseTexture9*,3>{texture,texture,texture};
            });
        for(auto& target:resources->targets)
            if(input==target.original.Get() || input==target.texture[0].Get() || input==target.texture[1].Get())
                return eye<0 ? target.original.Get() : target.texture[eye].Get();
        return input;
    }

    IDirect3DSurface9* MapSurface(IDirect3DSurface9* input,int eye)
    {
        // Keep disabled profiling out of this frequent routing path, including
        // the temporary stamp initialization emitted by the x86 compiler.
        if(!routingProbeActive || eye<0 || eye>1) return MapSurfaceImpl(input,eye);
        const auto stamp=VRProfile::Begin();
        auto* result=MapSurfaceImpl(input,eye);
        if(stamp.epoch) VRProfile::Accumulate(routingTime[eye],stamp);
        return result;
    }
    IDirect3DBaseTexture9* MapTexture(IDirect3DBaseTexture9* input,int eye)
    {
        if(!routingProbeActive || eye<0 || eye>1) return MapTextureImpl(input,eye);
        const auto stamp=VRProfile::Begin();
        auto* result=MapTextureImpl(input,eye);
        if(stamp.epoch) VRProfile::Accumulate(routingTime[eye],stamp);
        return result;
    }

    HRESULT WINAPI SetTarget(IDirect3DDevice9* self,DWORD index,IDirect3DSurface9* surface)
    {FlushLiveMesh(self);return setTarget.unsafe_stdcall<HRESULT>(self,index,OnRoutingThread() && !composingHud ? MapSurface(surface,renderEye) : surface);}
    HRESULT WINAPI SetDepth(IDirect3DDevice9* self,IDirect3DSurface9* surface)
    {FlushLiveMesh(self);return setDepth.unsafe_stdcall<HRESULT>(self,OnRoutingThread() && !composingHud ? MapSurface(surface,renderEye) : surface);}
    HRESULT WINAPI SetTexture(IDirect3DDevice9* self,DWORD stage,IDirect3DBaseTexture9* texture)
    {
        texture=OnRoutingThread() ? MapTexture(texture,renderEye):texture;
        return ObserveStateCall(VRProfile::StateKind::Texture,[&](){return setTexture.unsafe_stdcall<HRESULT>(self,stage,texture);});
    }
    HRESULT WINAPI SetRenderState(IDirect3DDevice9* self,D3DRENDERSTATETYPE state,DWORD value)
    {return ObserveStateCall(VRProfile::StateKind::RenderState,[&](){return setRenderState.unsafe_stdcall<HRESULT>(self,state,value);});}
    HRESULT WINAPI SetSamplerState(IDirect3DDevice9* self,DWORD sampler,D3DSAMPLERSTATETYPE state,DWORD value)
    {return ObserveStateCall(VRProfile::StateKind::SamplerState,[&](){return setSamplerState.unsafe_stdcall<HRESULT>(self,sampler,state,value);});}
    HRESULT WINAPI SetStreamSource(IDirect3DDevice9* self,UINT stream,IDirect3DVertexBuffer9* buffer,UINT offset,UINT stride)
    {return ObserveStateCall(VRProfile::StateKind::StreamSource,[&](){return setStreamSource.unsafe_stdcall<HRESULT>(self,stream,buffer,offset,stride);});}
    HRESULT WINAPI SetIndices(IDirect3DDevice9* self,IDirect3DIndexBuffer9* indices)
    {return ObserveStateCall(VRProfile::StateKind::Indices,[&](){return setIndices.unsafe_stdcall<HRESULT>(self,indices);});}
    HRESULT WINAPI SetVertexDeclaration(IDirect3DDevice9* self,IDirect3DVertexDeclaration9* declaration)
    {return ObserveStateCall(VRProfile::StateKind::VertexDeclaration,[&](){return setVertexDeclaration.unsafe_stdcall<HRESULT>(self,declaration);});}

    void ConfigureStateHooks(IDirect3DDevice9* device,bool wanted)
    {
        // Called at the owning render thread's eye0 marker, outside every
        // observed setter. Ordinary gameplay should pay no extra detours for
        // the five methods that only exist to support this diagnostic.
        if(!wanted)
        {
            if(stateHooksReady)
            {
                setRenderState.reset(); setSamplerState.reset(); setStreamSource.reset();
                setIndices.reset(); setVertexDeclaration.reset();
                stateHooksReady=false; Log("Sparse state-only hooks removed");
            }
            return;
        }
        if(stateHooksReady || stateHooksFailed) return;
        const auto table=*reinterpret_cast<void***>(device);
        setRenderState=safetyhook::create_inline(table[57],SetRenderState);
        setSamplerState=safetyhook::create_inline(table[69],SetSamplerState);
        setStreamSource=safetyhook::create_inline(table[100],SetStreamSource);
        setIndices=safetyhook::create_inline(table[104],SetIndices);
        setVertexDeclaration=safetyhook::create_inline(table[87],SetVertexDeclaration);
        stateHooksReady=setRenderState && setSamplerState && setStreamSource && setIndices && setVertexDeclaration;
        if(!stateHooksReady)
        {
            setRenderState.reset(); setSamplerState.reset(); setStreamSource.reset();
            setIndices.reset(); setVertexDeclaration.reset(); stateHooksFailed=true;
            Log("Sparse state-only hooks unavailable; observer disabled until reset");
        }
        else Log("Sparse state-only hooks installed");
    }
    HRESULT WINAPI Stretch(IDirect3DDevice9* self,IDirect3DSurface9* source,const RECT* sourceRect,
        IDirect3DSurface9* destination,const RECT* destinationRect,D3DTEXTUREFILTERTYPE filter)
    {FlushLiveMesh(self);const bool route=GetCurrentThreadId()==routingThread && !composingHud;
     return stretch.unsafe_stdcall<HRESULT>(self,route ? MapSurface(source,renderEye) : source,sourceRect,
        route ? MapSurface(destination,renderEye) : destination,destinationRect,filter);}

    HRESULT WINAPI SetVertexShader(IDirect3DDevice9* self,IDirect3DVertexShader9* shader)
    {
        if(composingHud || GetCurrentThreadId()!=routingThread || !resources)
            return setVertexShader.unsafe_stdcall<HRESULT>(self,shader);
        if(renderEye>=0) constantProbes[renderEye][0].observer.Invalidate(209);
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
        return ObserveStateCall(VRProfile::StateKind::VertexShader,[&](){return setVertexShader.unsafe_stdcall<HRESULT>(self,shader);});
    }

    HRESULT WINAPI SetVertexConstants(IDirect3DDevice9* self,UINT start,const float* data,UINT count)
    {
        if(composingHud) return setVertexConstants.unsafe_stdcall<HRESULT>(self,start,data,count);
        if(binderDevice)
        {
            if(self==binderDevice && binderConstants.Write(start,data,count))return S_OK;
            FlushBinderConstants();
        }
        // Marker-owned TLS avoids thread/observer queries for every ordinary
        // constant write. Forward every call; no value cache or suppression.
        if(!rayShaderBound && !constantProbeActive)
            return ObserveStateCall(VRProfile::StateKind::VertexConstants,
                [&](){return setVertexConstants.unsafe_stdcall<HRESULT>(self,start,data,count);});
        const auto stamp=constantProbeActive ? VRProfile::Begin() : VRProfile::Stamp{};
        const auto result=ObserveStateCall(VRProfile::StateKind::VertexConstants,
            [&](){return setVertexConstants.unsafe_stdcall<HRESULT>(self,start,data,count);});
        if(stamp.epoch) constantProbes[renderEye][0].Record(stamp,start,data,count,result);
        if(SUCCEEDED(result) && rayShaderBound &&
            start<=209 && count>209-start)
        {
            std::memcpy(originalRayConstants,data+(209-start)*4,sizeof(originalRayConstants));
            if(renderEye>=0) constantProbes[renderEye][0].observer.Invalidate(209);
            return setVertexConstants.unsafe_stdcall<HRESULT>(self,209,eyeRayConstants,1);
        }
        return result;
    }

    void ProbeNormalDraw(IDirect3DDevice9* device)
    {
        if(!normalProbeActive || renderEye<0 || normalProbeKeys[renderEye].size()>=64) return;
        ComPtr<IDirect3DPixelShader9> shader;
        if(FAILED(device->GetPixelShader(&shader)) || !shader) return;
        NormalProbeKey key{}; key.shader=shader.Get();
        ComPtr<IDirect3DBaseTexture9> textures[8];
        for(unsigned i=0;i<8;++i) {device->GetTexture(i,&textures[i]); key.textures[i]=textures[i].Get();}
        auto& keys=normalProbeKeys[renderEye];
        if(std::any_of(keys.begin(),keys.end(),[&](const auto& old){return old.shader==key.shader && old.textures==key.textures;})) return;
        keys.push_back(key);
        char line[256];
        sprintf_s(line,"normal draw eye=%d record=%zu shader=%p",renderEye,keys.size(),shader.Get()); Log(line);
        if(normalProbeShaders.size()<32 && std::none_of(normalProbeShaders.begin(),normalProbeShaders.end(),
            [&](const auto& old){return old.Get()==shader.Get();}))
        {
            normalProbeShaders.push_back(shader);
            UINT bytes=0; shader->GetFunction(nullptr,&bytes);
            if(bytes && bytes<=65536)
            {
                std::vector<DWORD> code((bytes+3)/4); ComPtr<ID3DXBuffer> assembly;
                if(SUCCEEDED(shader->GetFunction(code.data(),&bytes)) &&
                    SUCCEEDED(D3DXDisassembleShader(code.data(),FALSE,nullptr,&assembly)))
                {
                    sprintf_s(line,"normal pixel shader=%p (bounded capture)",shader.Get()); Log(line);
                    const std::string text(static_cast<const char*>(assembly->GetBufferPointer()),
                        std::min<size_t>(assembly->GetBufferSize(),24000)); Log(text.c_str());
                }
            }
        }
        for(unsigned slot=0;slot<4;++slot)
        {
            ComPtr<IDirect3DSurface9> target; D3DSURFACE_DESC desc{};
            if(SUCCEEDED(device->GetRenderTarget(slot,&target)) && target && SUCCEEDED(target->GetDesc(&desc)))
            {sprintf_s(line,"normal target slot=%u surface=%p format=%u",slot,target.Get(),desc.Format); Log(line);}
        }
        for(auto state:{D3DRS_SRGBWRITEENABLE,D3DRS_DITHERENABLE,D3DRS_ALPHABLENDENABLE,
            D3DRS_SRCBLEND,D3DRS_DESTBLEND,D3DRS_BLENDOP,D3DRS_COLORWRITEENABLE1})
        {DWORD value=0; if(SUCCEEDED(device->GetRenderState(state,&value)))
            {sprintf_s(line,"normal render state=%u value=%u",state,value); Log(line);}}
        for(unsigned slot=0;slot<8;++slot)
        {
            sprintf_s(line,"normal texture slot=%u texture=%p",slot,textures[slot].Get()); Log(line);
            for(auto state:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER,D3DSAMP_MIPFILTER,D3DSAMP_SRGBTEXTURE,
                D3DSAMP_MAXANISOTROPY,D3DSAMP_MIPMAPLODBIAS,D3DSAMP_MAXMIPLEVEL,D3DSAMP_ADDRESSU,D3DSAMP_ADDRESSV})
            {DWORD value=0; if(SUCCEEDED(device->GetSamplerState(slot,state,&value)))
                {sprintf_s(line,"normal sampler slot=%u state=%u value=%u",slot,state,value); Log(line);}}
        }
        float constants[224][4]{};
        if(SUCCEEDED(device->GetPixelShaderConstantF(0,constants[0],224)))
            for(unsigned i=0;i<224;++i)
            {sprintf_s(line,"normal PS c%u=(%.9g %.9g %.9g %.9g)",i,
                constants[i][0],constants[i][1],constants[i][2],constants[i][3]); Log(line);}
    }

    HRESULT WINAPI DrawIndexedPrimitive(IDirect3DDevice9* device,D3DPRIMITIVETYPE type,
        INT base,UINT minimum,UINT vertices,UINT start,UINT primitives)
    {
        FlushBinderConstants();
        if(liveMeshPackets && OnRoutingThread() && renderEye>=0 && renderStage==1 && liveMeshPackets->Observing())
        {
            bool queued=false;
            try {queued=liveMeshPackets->Record(device,type,base,minimum,vertices,start,primitives,InspectMeshPacketShaders);}
            catch(const std::bad_alloc&){liveMeshPackets->AllocationFailure();}
            if(queued)return S_OK; // The actual native submission is replaced.
            FlushLiveMesh(device); // Earlier packets precede this native fallback.
        }
        if(meshPacketProbe && GetCurrentThreadId()==routingThread && renderEye==0 && renderStage==1)
        {
            try {meshPacketProbe->Record(device,type,base,minimum,vertices,start,primitives,InspectMeshPacketShaders,meshPacketMaterial);}
            catch(const std::bad_alloc&){meshPacketProbe->AllocationFailure();Log("mesh-packet-probe capture allocation failed; native draw still executes");}
        }
        CaptureMeshBindings(device,type,base,minimum,vertices,start,primitives);
        if(normalProbeActive) ProbeNormalDraw(device);
        const auto stamp=drawProbeActive ? BeginDrawProbe(true,primitives,vertices):VRProfile::Stamp{};
        HRESULT result=S_OK;bool executed=false;
        if(rigidInstances && GetCurrentThreadId()==routingThread && renderEye>=0 && renderStage==1)
        {
            executed=rigidInstances->TryDraw(device,renderEye,type,base,minimum,vertices,start,primitives,
                [device](const std::vector<DWORD>& code,bool tangent,ComPtr<IDirect3DVertexShader9>& shader)
                {
                    if(!AdaptRigidShader(device,code,tangent,shader))return false;
                    Log("rigid-instancing-test native shader adapted");return true;
                },[device](D3DPRIMITIVETYPE kind,INT offset,UINT first,UINT count,UINT index,UINT triangles)
                {return drawIndexedPrimitive.unsafe_stdcall<HRESULT>(device,kind,offset,first,count,index,triangles);},result);
            if(!executed)++rigidInstances->fallbacks[renderEye];
        }
        if(!executed)result=drawIndexedPrimitive.unsafe_stdcall<HRESULT>(device,type,base,minimum,vertices,start,primitives);
        if(drawProbeActive) EndDrawProbe(stamp,result);
        return result;
    }

    HRESULT WINAPI DrawPrimitive(IDirect3DDevice9* device,D3DPRIMITIVETYPE type,UINT start,UINT count)
    {
        FlushLiveMesh(device);
        if(InPacketStream())meshPacketProbe->OtherDraw();
        FlushBinderConstants();
        if(InFirstMeshStream()){++meshNonIndexed;MeshSequenceBarrier();}
        CaptureCoronaDraw(device,type,start,count);
        if(normalProbeActive) ProbeNormalDraw(device);
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
        const auto stamp=drawProbeActive ? BeginDrawProbe(false,count,0):VRProfile::Stamp{};
        const auto result=drawPrimitive.unsafe_stdcall<HRESULT>(device,type,start,count);
        if(drawProbeActive) EndDrawProbe(stamp,result);
        return result;
    }

    void ConfigureDrawHooks(IDirect3DDevice9* device,bool wanted)
    {
        // Both draw detours only collect diagnostics. Change them at the eye0
        // marker, never from within a draw callback, just like state hooks.
        if(!wanted)
        {
            if(drawPrimitive || drawIndexedPrimitive)
            {
                drawPrimitive.reset(); drawIndexedPrimitive.reset();
                Log("Diagnostic draw hooks removed");
            }
            return;
        }
        if(drawPrimitive && drawIndexedPrimitive) return;
        const auto table=*reinterpret_cast<void***>(device);
        drawPrimitive=safetyhook::create_inline(table[81],DrawPrimitive);
        drawIndexedPrimitive=safetyhook::create_inline(table[82],DrawIndexedPrimitive);
        if(!drawPrimitive || !drawIndexedPrimitive)
        {
            drawPrimitive.reset(); drawIndexedPrimitive.reset();
            Log("Diagnostic draw hooks unavailable");
        }
        else Log("Diagnostic draw hooks installed");
    }

    HRESULT WINAPI SetPixelShader(IDirect3DDevice9* device,IDirect3DPixelShader9* shader)
    {
        if(composingHud || GetCurrentThreadId()!=routingThread || !resources)
            return setPixelShader.unsafe_stdcall<HRESULT>(device,shader);
        if(renderEye>=0) constantProbes[renderEye][1].observer.Invalidate(14);
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
                    char header[128];
                    sprintf_s(header,"lighting pixel shader=%p frame=%u eye=%d (bounded discovery)",shader,completedFrame,renderEye);
                    Log(header);
                    Log(text.substr(0,24000).c_str());
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
        return ObserveStateCall(VRProfile::StateKind::PixelShader,[&](){return setPixelShader.unsafe_stdcall<HRESULT>(device,shader);});
    }

    HRESULT WINAPI SetPixelConstants(IDirect3DDevice9* device,UINT start,const float* data,UINT count)
    {
        if(composingHud) return setPixelConstants.unsafe_stdcall<HRESULT>(device,start,data,count);
        if(!cascadeShaderBound && !constantProbeActive)
            return ObserveStateCall(VRProfile::StateKind::PixelConstants,
                [&](){return setPixelConstants.unsafe_stdcall<HRESULT>(device,start,data,count);});
        const auto stamp=constantProbeActive ? VRProfile::Begin() : VRProfile::Stamp{};
        const auto result=ObserveStateCall(VRProfile::StateKind::PixelConstants,
            [&](){return setPixelConstants.unsafe_stdcall<HRESULT>(device,start,data,count);});
        if(stamp.epoch) constantProbes[renderEye][1].Record(stamp,start,data,count,result);
        if(SUCCEEDED(result) && cascadeShaderBound && start<=14 && count>14-start)
        {
            std::memcpy(originalCascadeForward,data+(14-start)*4,sizeof(originalCascadeForward));
            if(renderEye>=0) constantProbes[renderEye][1].observer.Invalidate(14);
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
        bool optical;
        OpenXRVR::EyeView views[2]{};
        float camera[16]{};
        VRMath::Matrix viewProjection{},wristSnapshot{},dashboardSnapshot{};
        bool wristValid=false,dashboardValid=false;
        float shadowForward[4]{};
        float opticalRay[4]{};
        VRMath::Vector logDepth{};
        long long poseTime;
    public:
        EyeMarker(int value,unsigned role,unsigned id,bool start):CBaseDC(),eye(value),stage(role),frame(id),begin(start),optical(buildingScope),poseTime(viewTime)
        {views[0]=xrViews[0]; views[1]=xrViews[1]; std::memcpy(camera,eyeViewports[value].mViewInverseMatrix,sizeof(camera));
         std::memcpy(shadowForward,gameShadowForward,sizeof(shadowForward));
         VRMath::Matrix cameraWorld{},projection{};
         std::memcpy(cameraWorld.data(),eyeViewports[value].mViewInverseMatrix,sizeof(cameraWorld));
         std::memcpy(projection.data(),eyeViewports[value].mProjectionMatrix,sizeof(projection));
         opticalRay[0]=1.f/projection[0][0];opticalRay[1]=1.f/projection[1][1];
         viewProjection=VRMath::Multiply(VRMath::InverseRigid(cameraWorld),projection);
         logDepth=VRScope::DepthParameters(eyeViewports[value].mNearClip,eyeViewports[value].mFarClip);
         wristValid=VRHands::GetWrist(0,wristSnapshot);
         dashboardValid=VRCamera::GetVehicleHudAnchor(dashboardSnapshot);}
        int32_t GetSize() override {return sizeof(EyeMarker);}
        void DrawCommand() override
        {
            if(!ready || !RageDirect3DDevice9::m_pRealDevice) return;
            auto* device=*RageDirect3DDevice9::m_pRealDevice;
            if(!device) return;
            if(optical)
            {
                if(scopeRenders.load()<3)
                {
                    char line[120];sprintf_s(line,"scope execute frame=%u stage=%u begin=%u",frame,stage,begin);Log(line);
                }
                if(begin && stage==1)
                {
                    if(!scopeState.Capture(device)) {++scopeFailures;return;}
                    // Switch only on the command executor, after returning
                    // bound eye surfaces to their native identities.
                    Rebind(device,-1);
                    mainResources=resources;
                    if(!CreateScopeResources(device)) {++scopeFailures;mainResources=nullptr;scopeState.Restore(device);return;}
                    scopeResources->symmetricRayShader=resources->symmetricRayShader;
                    scopeResources->asymmetricRayShader=resources->asymmetricRayShader;
                    scopeResources->cascadeShaders=resources->cascadeShaders;
                    resources=scopeResources;
                }
                if(resources!=scopeResources) return;
                constantProbeActive=routingProbeActive=normalProbeActive=false;
                stateProbeActive=drawProbeActive=false;
                textureCacheBypass=false;
                renderEye=begin ? 0:-1;renderStage=begin ? stage:0;
                routingPhaseGeneration=begin && GetCurrentThreadId()==routingThread ? routingGeneration.load(std::memory_order_relaxed):0;
                if(begin)
                {
                    std::memcpy(expectedCamera,camera,sizeof(camera));
                    std::memcpy(cascadeForward,shadowForward,sizeof(shadowForward));
                    std::memcpy(eyeRayConstants,opticalRay,sizeof(opticalRay));
                    if(stage==1) PrepareGBufferMipFilters(device,0);
                }
                if(!begin && scopeCaptureRequested.load())
                {
                    ComPtr<IDirect3DSurface9> current;device->GetRenderTarget(0,&current);
                    char line[220];sprintf_s(line,"scope output stage=%u target=%p final=%p main=%p,%p",stage,current.Get(),
                        scopeResources->finalSurface[0].Get(),mainResources->finalSurface[0].Get(),mainResources->finalSurface[1].Get());Log(line);
                }
                Rebind(device,renderEye);
                if(!begin && stage==3)
                {
                    scopeCompletedFrame=frame;
                    std::memcpy(completedScopeCamera.data(),camera,sizeof(camera));
                    ++scopeRenders;
                    if(scopeCaptureRequested.exchange(false))
                    {
                        captureNextPair=true; // Include one scene-depth/lens comparison.
                        wchar_t path[MAX_PATH]{};GetModuleFileNameW(nullptr,path,MAX_PATH);
                        if(auto* last=wcsrchr(path,L'\\')) last[1]=0;
                        wcscat_s(path,L"plugins\\GTAIV.VR.scope.bmp");
                        const bool wasComposing=composingHud;composingHud=true;
                        const auto result=D3DXSaveSurfaceToFileW(path,D3DXIFF_BMP,scopeResources->finalSurface[0].Get(),nullptr,nullptr);
                        composingHud=wasComposing;
                        char line[120];sprintf_s(line,"scope source capture frame=%u result=%08X",frame,unsigned(result));Log(line);
                        for(const auto& target:scopeResources->targets)
                        {
                            if(target.name!="_DEFERRED_GBUFFER_0_" && !target.name.starts_with("FullScreen")) continue;
                            wchar_t targetPath[MAX_PATH]{};GetModuleFileNameW(nullptr,targetPath,MAX_PATH);
                            if(auto* last=wcsrchr(targetPath,L'\\'))last[1]=0;
                            std::wstring name(target.name.begin(),target.name.end());
                            wcscat_s(targetPath,(L"plugins\\GTAIV.VR.scope-"+name+L".bmp").c_str());
                            composingHud=true;
                            const auto saved=D3DXSaveSurfaceToFileW(targetPath,D3DXIFF_BMP,target.surface[0].Get(),nullptr,nullptr);
                            composingHud=wasComposing;
                            sprintf_s(line,"scope intermediate %s result=%08X",target.name.c_str(),unsigned(saved));Log(line);
                        }
                    }
                    resources=mainResources;mainResources=nullptr;
                    scopeState.Restore(device);
                }
                return;
            }
            if(begin && stage==1 && eye==0)
            {
                UpdateCpuOmission();
                UpdateVertexCallerProbe();
                UpdateVertexEmissionTest();
                UpdateRigidInstances(device);
                UpdateMeshPacketProbe(device);
                UpdateConstantBatchTest();
                UpdateModelCommandProbe(device);
                UpdateGpuBillboards(device);
                liveMeshMode=liveMeshRequested.exchange(0);
                if(liveMeshMode && (meshPacketProbe || modelProbeHook || rigidInstances || gpuBillboards || vertexBinderBatchHook || vertexProbeHook || vertexEmissionHook))
                {liveMeshMode=0;Log("mesh-live-probe unavailable: another experiment is active");}
                if(liveMeshMode==2)
                {
                    auto material=hook::pattern("53 55 8B E9 56 0F B7 5D 1C 33 F6 57 85 DB");
                    if(material.size()==1)meshPacketMaterialProbe=safetyhook::create_inline(material.get_first(),ObservePacketMaterial);
                    // Keep CE004373E0/0042C8F0 binding and state-only cleanup
                    // running. Owned packets restore those states at emission.
                    // Resource contents are a separate ordering dependency.
                    ComPtr<IDirect3DVertexBuffer9> vb;ComPtr<IDirect3DIndexBuffer9> ib;ComPtr<IDirect3DTexture9> texture;
                    if(SUCCEEDED(device->CreateVertexBuffer(128,D3DUSAGE_WRITEONLY,0,D3DPOOL_DEFAULT,&vb,nullptr)) &&
                        SUCCEEDED(device->CreateIndexBuffer(16,D3DUSAGE_WRITEONLY,D3DFMT_INDEX16,D3DPOOL_DEFAULT,&ib,nullptr)) &&
                        SUCCEEDED(device->CreateTexture(1,1,1,0,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,&texture,nullptr)))
                    {
                        const auto vt=*reinterpret_cast<void***>(vb.Get()),it=*reinterpret_cast<void***>(ib.Get()),tt=*reinterpret_cast<void***>(texture.Get());
                        liveMeshVBLock=safetyhook::create_inline(vt[11],LockLiveMeshVB);
                        if(it[11]!=vt[11])liveMeshIBLock=safetyhook::create_inline(it[11],LockLiveMeshIB);
                        liveMeshTextureLock=safetyhook::create_inline(tt[19],LockLiveMeshTexture);
                        liveMeshGenerateMips=safetyhook::create_inline(tt[16],GenerateLiveMeshMips);
                        // A shared VB/IB entry point is already covered by the
                        // same ABI-compatible VB hook; no duplicate detour.
                        if(it[11]==vt[11])Log("mesh-live-probe shared VB/IB lock entry");
                    }
                    ComPtr<IDirect3DQuery9> query;
                    if(SUCCEEDED(device->CreateQuery(D3DQUERYTYPE_OCCLUSION,&query)) && query)
                        meshQueryIssueProbe=safetyhook::create_inline((*reinterpret_cast<void***>(query.Get()))[6],ObserveMeshQueryIssue);
                    auto** table=*reinterpret_cast<void***>(device);
                    meshPrimitiveUPProbe=safetyhook::create_inline(table[83],ObserveMeshPrimitiveUP);
                    meshIndexedUPProbe=safetyhook::create_inline(table[84],ObserveMeshIndexedUP);
                    liveMeshClear=safetyhook::create_inline(table[43],ClearLiveMesh);
                    liveMeshUpdateSurface=safetyhook::create_inline(table[30],UpdateLiveMeshSurface);
                    liveMeshUpdateTexture=safetyhook::create_inline(table[31],UpdateLiveMeshTexture);
                    liveMeshColorFill=safetyhook::create_inline(table[35],FillLiveMeshColor);
                    const bool indexLockCovered=ib && vb && (*reinterpret_cast<void***>(ib.Get()))[11]==(*reinterpret_cast<void***>(vb.Get()))[11];
                    if(!meshPacketMaterialProbe || !liveMeshVBLock || (!liveMeshIBLock && !indexLockCovered) || !liveMeshTextureLock || !liveMeshGenerateMips ||
                        !liveMeshUpdateSurface || !liveMeshUpdateTexture || !liveMeshColorFill || !meshQueryIssueProbe || !meshPrimitiveUPProbe || !meshIndexedUPProbe || !liveMeshClear)
                    {liveMeshMode=0;meshPacketMaterialProbe.reset();RemoveLiveMeshResourceHooks();meshQueryIssueProbe.reset();meshPrimitiveUPProbe.reset();meshIndexedUPProbe.reset();liveMeshClear.reset();Log("mesh-live-probe unavailable: required boundary hook missing");}
                }
                ConfigureStateHooks(device,VRProfile::StateObservationEnabled());
                ConfigureDrawHooks(device,!captured || captureNextPair.load() || modelProbeHook || rigidInstances || meshPacketProbe || liveMeshMode==2 || vertexBinderBatchHook || VRProfile::DrawObservationEnabled() || VRProfile::DrawForwardingEnabled());
            }
            if(begin && stage==1 && liveMeshMode)
            {
                if(liveMeshMode==2)
                {
                    try {liveMeshPackets=std::make_unique<VRMeshPacketProbe::Probe>(true,true,false,true);}
                    catch(const std::bad_alloc&){Log("mesh-live-probe allocation failed; native rendering retained");}
                }
                QueryPerformanceCounter(&liveMeshBegin);
            }
            if(!begin && stage==1 && liveMeshMode)
            {
                FlushLiveMesh(device);
                LARGE_INTEGER end{},frequency{};QueryPerformanceCounter(&end);QueryPerformanceFrequency(&frequency);
                char line[320];
                sprintf_s(line,"mesh-live-probe mode=%s eye=%d gbufferCpuMs=%.6f observed=%u queued=%u emitted=%u batches=%u flushes=%u failures=%u",
                    liveMeshMode==2 ? "replacement":"native",eye,1000.0*(end.QuadPart-liveMeshBegin.QuadPart)/frequency.QuadPart,
                    liveMeshPackets ? liveMeshPackets->observed:0,liveMeshPackets ? liveMeshPackets->liveQueued:0,
                    liveMeshPackets ? liveMeshPackets->liveEmitted:0,liveMeshPackets ? liveMeshPackets->liveBatches:0,
                    liveMeshPackets ? liveMeshPackets->liveFlushes:0,liveMeshPackets ? liveMeshPackets->failures:0);Log(line);
                if(liveMeshPackets)
                {
                    const double ms=1000.0/frequency.QuadPart;
                    sprintf_s(line,"mesh-live-probe costs eye=%d collectMs=%.6f stateCaptureMs=%.6f emitMs=%.6f stateApplyMs=%.6f instanceMs=%.6f",
                        eye,liveMeshPackets->liveCollectTicks*ms,liveMeshPackets->liveStateCaptureTicks*ms,
                        liveMeshPackets->liveEmitTicks*ms,liveMeshPackets->liveApplyTicks*ms,liveMeshPackets->liveInstanceTicks*ms);Log(line);
                }
                liveMeshPackets.reset();
                if(eye==1)
                {liveMeshMode=0;meshPacketMaterialProbe.reset();RemoveLiveMeshResourceHooks();meshQueryIssueProbe.reset();meshPrimitiveUPProbe.reset();meshIndexedUPProbe.reset();liveMeshClear.reset();}
            }
            if(!begin && stage==1 && eye==0 && meshPacketProbe)
            {
                meshPacketProbe->Compare(device,
                    [device](const auto& code,bool tangent,auto& shader){return AdaptRigidShader(device,code,tangent,shader);},
                    [device](D3DPRIMITIVETYPE type,INT base,UINT first,UINT vertices,UINT start,UINT primitives)
                    {return drawIndexedPrimitive.unsafe_stdcall<HRESULT>(device,type,base,first,vertices,start,primitives);},Log,meshCompareOrder);
                meshPacketProbe.reset();meshPacketShaders.clear();meshPacketMaterialProbe.reset();meshPacketMaterial=0;
                meshQueryIssueProbe.reset();meshPrimitiveUPProbe.reset();meshIndexedUPProbe.reset();
            }
            // Queued markers execute on the render thread, after list building.
            if(vertexProbeHook)
            {
                vertexProbePhase=begin && stage>=1 && stage<=3 ? eye*3+stage-1 : 6;
                if(vertexProbePhase<6) ++vertexProbeStreams[vertexProbePhase];
            }
            static thread_local VRProfile::Stamp commandStart[2]{};
            if(modelProbeHook)
            {
                modelProbePhase=begin && stage>=1 && stage<=3 ? eye*3+stage-1 : 6;
                if(modelProbePhase<6) ++modelProbeStreams[modelProbePhase];
            }
            static thread_local VRProfile::Stamp outsideStart{},interEyeStart{};
            if(begin && stage==1)
            {
                if(!eye) VRProfile::EyeExecutionStarted();
                VRProfile::End(eye ? VRProfile::Metric::InterEyeGap : VRProfile::Metric::FrameOutsideEyes,
                    eye ? interEyeStart : outsideStart);
                routingTime[eye]={};
            }
            static thread_local VRProfile::Stamp phaseStart[2][3]{};
            constexpr VRProfile::Metric phaseMetrics[2][3]{
                {VRProfile::Metric::Eye0GBuffer,VRProfile::Metric::Eye0Lighting,VRProfile::Metric::Eye0DrawScene},
                {VRProfile::Metric::Eye1GBuffer,VRProfile::Metric::Eye1Lighting,VRProfile::Metric::Eye1DrawScene}};
            if(begin && stage>=1 && stage<=3) phaseStart[eye][stage-1]=VRProfile::Begin();
            if(begin && stage==1)
            {
                stateProbeActive=stateHooksReady && VRProfile::StateObservationEnabled();
                stateProbeEpoch=stateProbeActive ? phaseStart[eye][0].epoch:0;
                stateProbeOffset=(frame+eye*17)&63;
                for(auto& counters:stateProbeCounters) counters={};
            }
            if(!begin && stage==3)
            {
                if(stateProbeActive)
                    for(unsigned kind=0;kind<static_cast<unsigned>(VRProfile::StateKind::Count);++kind)
                        VRProfile::StateCalls(eye,static_cast<VRProfile::StateKind>(kind),stateProbeEpoch,stateProbeCounters[kind]);
                stateProbeActive=false; stateProbeEpoch=0;
            }
            if(begin)
            {
                drawProbeActive=drawPrimitive && drawIndexedPrimitive && stage>=1 && stage<=3 && VRProfile::DrawObservationEnabled();
                drawProbeEpoch=drawProbeActive ? phaseStart[eye][stage-1].epoch:0;
                drawProbeOffset=(frame+eye*17+stage*7)&63;
                drawProbeCounters={};
            }
            else
            {
                if(drawProbeActive) VRProfile::DrawCalls(eye,stage,drawProbeEpoch,drawProbeCounters);
                drawProbeActive=false; drawProbeEpoch=0;
            }
            if(begin && stage==1) commandStart[eye]=VRProfile::Begin();
            if(stage==1 && begin) GPUTime(device,eye,true);
            if(stage==3 && !begin) GPUTime(device,eye,false);
            if(begin && stage==1 && eye==0 && captureNextPair.exchange(false)) {captured=false; lightingProbeMask=0;}
            if(begin && stage==1 && eye==0 && !captured)
            {for(auto& keys:normalProbeKeys) keys.clear(); normalProbeShaders.clear();}
            normalProbeActive=begin && stage==1 && !captured;
            constantProbeActive=begin && stage>=1 && stage<=3 && VRProfile::ConstantObservationEnabled();
            routingProbeActive=begin && stage>=1 && stage<=3 && phaseStart[eye][stage-1].epoch!=0;
            textureCacheBypass=begin && VRProfile::TextureLinearEnabled();
            if(begin && stage==1) PrepareGBufferMipFilters(device,eye);
            if(!begin && stage==1)
            {
                device->GetDepthStencilSurface(&resources->worldDepth[eye]);
                device->GetViewport(&resources->worldViewport[eye]);
            }
            renderEye=begin ? eye : -1;
            renderStage=begin ? stage : 0;
            routingPhaseGeneration=begin && GetCurrentThreadId()==routingThread ?
                routingGeneration.load(std::memory_order_relaxed) : 0;
            if(begin)
            {
                for(auto& probe:constantProbes[eye])
                {
                    if(stage==1) probe.Reset(); else probe.observer.Reset();
                }
                std::memcpy(expectedCamera,camera,sizeof(expectedCamera));
                std::memcpy(cascadeForward,shadowForward,sizeof(cascadeForward));
                const auto& fov=views[eye].fov;
                const float left=std::tan(fov[0]),right=std::tan(fov[1]),
                    up=std::tan(fov[2]),down=std::tan(fov[3]);
                eyeRayConstants[0]=(right-left)*.5f; eyeRayConstants[1]=(up-down)*.5f;
                eyeRayConstants[2]=(right+left)*.5f; eyeRayConstants[3]=(up+down)*.5f;
            }
            Rebind(device,renderEye);
            if(!begin && stage>=1 && stage<=3)
                VRProfile::End(phaseMetrics[eye][stage-1],phaseStart[eye][stage-1]);
            if(!begin && stage==3)
            {
                for(unsigned kind=0;kind<2;++kind)
                {
                    auto& probe=constantProbes[eye][kind];
                    VRProfile::ConstantWrites(eye,kind==0,probe.time.epoch,probe.calls,probe.vectors,probe.repeatedCandidates);
                    const auto metric=kind==0 ? (eye ? VRProfile::Metric::Eye1VertexConstants:VRProfile::Metric::Eye0VertexConstants)
                        : (eye ? VRProfile::Metric::Eye1PixelConstants:VRProfile::Metric::Eye0PixelConstants);
                    VRProfile::Flush(metric,probe.time);
                    probe.Reset();
                }
                VRProfile::End(eye ? VRProfile::Metric::Eye1Commands : VRProfile::Metric::Eye0Commands,commandStart[eye]);
                VRProfile::Flush(eye ? VRProfile::Metric::Eye1Routing : VRProfile::Metric::Eye0Routing,routingTime[eye]);
                if(eye) outsideStart=VRProfile::Begin(); else interEyeStart=VRProfile::Begin();
                if(completedFrame!=frame) {completedFrame=frame; completedEyes=0;}
                completedEyes|=1u<<eye;
                completedViewProjection[eye]=viewProjection;
                completedLogDepth[eye]=logDepth;
                std::memcpy(completedEyeCamera[eye].data(),camera,sizeof(camera));
                completedWrist[eye]=wristSnapshot;completedWristValid[eye]=wristValid;
                completedDashboard[eye]=dashboardSnapshot;completedDashboardValid[eye]=dashboardValid;
                std::memcpy(completedEyePosition[eye].data(),camera+12,sizeof(VRMath::Vector));
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

    uint32_t __fastcall ScopeObjectRender(void* self,void* edx,uint32_t shader,
        uint32_t flags,uint32_t alpha,uint32_t unknown)
    {
        // CE CObject virtual +8C, B12E80: thiscall, four arguments, RET10.
        // Skip the owned rifle/magazine before generating scope draw commands.
        // Main eyes and shared shadow/reflection builds retain these objects.
        if(buildingScope && buildingStage && VRReloadRuntime::IsScopeOccluder(self))
        {++scopeHiddenObjects;return 0;}
        const auto result=scopeObjectRender.unsafe_fastcall<uint32_t>(self,edx,shader,flags,alpha,unknown);
        // The attachment can update after SceneSetup. Capture the object
        // transform at the actual main-eye command-building call, so the
        // optical view and aperture use the pose queued for the rifle.
        if(scopeSettings.enabled && !buildingScope && buildingEye==0 && buildingStage==1 && VRReloadRuntime::IsScopeGun(self))
        {
            VRMath::Matrix gun{};
            if(VRReloadRuntime::GetRenderedSniperPose(gun))
            {
                queuedScopeGun=gun;queuedScopeGunValid=true;
                if(scopePoseSamples++<8)
                {
                    const auto current=VRScope::Camera(gun,scopeSettings);
                    char line[180];sprintf_s(line,"scope queued pose delta=%.6g,%.6g,%.6g frame=%u",
                        current[3][0]-scopeCamera[3][0],current[3][1]-scopeCamera[3][1],current[3][2]-scopeCamera[3][2],gameFrame);Log(line);
                }
            }
        }
        return result;
    }

    uint32_t __fastcall PedRender(void* self,void* edx,uint32_t shader,
        uint32_t flags,uint32_t alpha,uint32_t unknown)
    {
        // CE CPed virtual +0x8C builds entity draw commands (ret 0x10).
        // Restrict filtering to the two world streams; shared shadows and
        // auxiliary views retain the character. Never alter entity flags.
        if(buildingEye>=0 && buildingStage && (VRCamera::HideLocalCharacter(self) ||
            (buildingScope && VRCamera::ShowLocalBody(self))))
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
            if(scopeResources && scopeCompletedFrame==completedFrame)
            {
                const auto world=VRMath::Transform({0,0,scopeSettings.surfaceOffset,1},completedScopeCamera);
                const auto clip=VRMath::Transform(world,completedViewProjection[eye]);
                if(std::isfinite(clip[0]+clip[1]+clip[2]+clip[3]) && clip[3]>.01f)
                {
                    const float x=(clip[0]/clip[3]+1)*desc.Width*.5f;
                    const float y=(1-clip[1]/clip[3])*desc.Height*.5f;
                    if(x>=0 && y>=0 && x<desc.Width && y<desc.Height)
                    {
                        float scene=0;std::memcpy(&scene,static_cast<unsigned char*>(locked.pBits)+unsigned(y)*locked.Pitch+unsigned(x)*4,4);
                        char line[256];sprintf_s(line,"scope depth comparison eye=%u pixel=%u,%u lensNdc=%.9g lensLogDepth=%.9g sceneLogDepth=%.9g lensW=%.9g",
                            eye,unsigned(x),unsigned(y),clip[2]/clip[3],VRScope::EncodeDepth(clip[3],completedLogDepth[eye]),scene,clip[3]);Log(line);
                    }
                }
            }
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

    void CaptureLightingInputs(IDirect3DDevice9* device)
    {
        // Read already-produced clones. No draw, routing or state mutation.
        // Keep unsupported formats explicit rather than misinterpreting bytes.
        for(const auto& target:resources->targets)
        {
            if(target.name!="_DEFERRED_GBUFFER_0_" && target.name!="_DEFERRED_GBUFFER_1_" &&
                target.name!="_DEFERRED_GBUFFER_2_") continue;
            for(unsigned eye=0;eye<2;++eye)
            {
                D3DSURFACE_DESC desc{};
                if(FAILED(target.surface[eye]->GetDesc(&desc))) continue;
                char line[200];
                sprintf_s(line,"lighting input frame=%u eye=%u name=%s format=%u size=%ux%u",
                    completedFrame,eye,target.name.c_str(),desc.Format,desc.Width,desc.Height); Log(line);
                if(desc.Format!=D3DFMT_A8R8G8B8 && desc.Format!=D3DFMT_X8R8G8B8 && desc.Format!=D3DFMT_A2R10G10B10)
                {Log("Lighting input readback skipped: unsupported color encoding"); continue;}
                ComPtr<IDirect3DSurface9> staging;
                if(!Check("lighting input staging",device->CreateOffscreenPlainSurface(desc.Width,desc.Height,
                    desc.Format,D3DPOOL_SYSTEMMEM,&staging,nullptr)) ||
                    !Check("lighting input readback",device->GetRenderTargetData(target.surface[eye].Get(),staging.Get()))) continue;
                D3DLOCKED_RECT locked{};
                if(!Check("lighting input lock",staging->LockRect(&locked,nullptr,D3DLOCK_READONLY))) continue;
                wchar_t path[MAX_PATH]{};
                GetModuleFileNameW(nullptr,path,MAX_PATH);
                auto* name=wcsrchr(path,L'\\'); if(name) name[1]=0;
                wchar_t suffix[100];
                const unsigned channel=target.name=="_DEFERRED_GBUFFER_0_" ? 0 : target.name=="_DEFERRED_GBUFFER_1_" ? 1 : 2;
                swprintf_s(suffix,L"plugins\\GTAIV.VR.gbuffer%u.eye%u.bmp",channel,eye);
                wcscat_s(path,suffix);
                FILE* file=nullptr; _wfopen_s(&file,path,L"wb");
                const unsigned bytes=desc.Width*desc.Height*4;
                BITMAPFILEHEADER header{}; header.bfType=0x4D42;
                header.bfOffBits=sizeof(header)+sizeof(BITMAPINFOHEADER); header.bfSize=header.bfOffBits+bytes;
                BITMAPINFOHEADER info{}; info.biSize=sizeof(info); info.biWidth=desc.Width;
                info.biHeight=-static_cast<LONG>(desc.Height); info.biPlanes=1; info.biBitCount=32;
                info.biCompression=BI_RGB; info.biSizeImage=bytes;
                if(file)
                {
                    fwrite(&header,sizeof(header),1,file); fwrite(&info,sizeof(info),1,file);
                    std::vector<unsigned> row(desc.Width);
                    for(unsigned y=0;y<desc.Height;++y)
                    {
                        const auto* source=static_cast<const unsigned char*>(locked.pBits)+y*locked.Pitch;
                        if(desc.Format==D3DFMT_A2R10G10B10)
                        {
                            for(unsigned x=0;x<desc.Width;++x)
                            {
                                unsigned pixel; std::memcpy(&pixel,source+x*4,4);
                                const unsigned blue=(pixel&1023)*255/1023,green=((pixel>>10)&1023)*255/1023,
                                    red=((pixel>>20)&1023)*255/1023,alpha=(pixel>>30)*85;
                                row[x]=blue|(green<<8)|(red<<16)|(alpha<<24);
                            }
                            fwrite(row.data(),desc.Width*4,1,file);
                        }
                        else fwrite(source,desc.Width*4,1,file);
                    }
                    fclose(file);
                }
                else Log("Lighting input file could not be created");
                staging->UnlockRect();
            }
        }
    }

    void CaptureEyes(IDirect3DDevice9* device)
    {
        if(captured || completedEyes!=3 || !resources) return;
        captured=true;
        CaptureLightingInputs(device);
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

    void DrawScopeLens(IDirect3DDevice9* device,unsigned eye)
    {
        if(!scopeResources || scopeCompletedFrame!=completedFrame || !scopeSettings.enabled ||
            !VRScope::InEyeBox(completedScopeCamera,completedEyePosition[eye],scopeSettings)) return;
        if(!scopeLensShader)
        {
            constexpr char code[]=
                "sampler2D scene:register(s0);float4 parameters:register(c0);float4 logDepth:register(c1);"
                "struct Output{float4 color:COLOR0;float depth:DEPTH;};"
                "Output main(float2 uv:TEXCOORD0,float2 clipDepth:TEXCOORD1){"
                "float2 p=uv*2-1;clip(1-dot(p,p));"
                "float4 color=tex2D(scene,float2(.5+(uv.x-.5)*parameters.x,uv.y));"
                "float crosshair=(min(abs(p.x),abs(p.y))<.008 && max(abs(p.x),abs(p.y))<.65);"
                "Output o;o.color=float4(lerp(lerp(color.rgb,float3(0,0,0),crosshair),float3(0,1,0),parameters.y),1);"
                "o.depth=saturate(log2(clipDepth.y*logDepth.x)*logDepth.y);return o;}";
            ComPtr<ID3DXBuffer> compiled,errors;
            if(FAILED(D3DXCompileShader(code,sizeof(code)-1,nullptr,nullptr,"main","ps_3_0",0,&compiled,&errors,nullptr)) ||
                FAILED(device->CreatePixelShader(static_cast<const DWORD*>(compiled->GetBufferPointer()),&scopeLensShader)))
                {++scopeFailures;return;}
        }
        if(!scopeLensVertexShader)
        {
            constexpr char code[]=
                "row_major float4x4 vp:register(c0);"
                "struct Output{float4 position:POSITION;float2 uv:TEXCOORD0;float2 clipDepth:TEXCOORD1;};"
                "Output main(float3 position:POSITION,float2 uv:TEXCOORD0){"
                "Output o;o.position=mul(float4(position,1),vp);o.uv=uv;o.clipDepth=o.position.zw;return o;}";
            ComPtr<ID3DXBuffer> compiled,errors;
            if(FAILED(D3DXCompileShader(code,sizeof(code)-1,nullptr,nullptr,"main","vs_3_0",0,&compiled,&errors,nullptr)) ||
                FAILED(device->CreateVertexShader(static_cast<const DWORD*>(compiled->GetBufferPointer()),&scopeLensVertexShader)))
                {++scopeFailures;return;}
        }
        ComPtr<IDirect3DStateBlock9> state;
        ComPtr<IDirect3DSurface9> targets[4],depth,lensDepth;
        D3DVIEWPORT9 viewport{};D3DSURFACE_DESC desc{};
        if(FAILED(resources->finalSurface[eye]->GetDesc(&desc))) return;
        // The native deferred renderer keeps geometry depth in GBuffer3
        // (INTZ). The attachment left bound after the scene phase can instead
        // be its stencil/post-process buffer and is not authoritative depth.
        // Prefer the named geometry surface, validating that it really is a
        // compatible depth attachment on this backend.
        for(const auto& target:resources->targets)
        {
            if(target.name!="_DEFERRED_GBUFFER_3_") continue;
            D3DSURFACE_DESC candidate{};
            if(SUCCEEDED(target.surface[eye]->GetDesc(&candidate)) && (candidate.Usage&D3DUSAGE_DEPTHSTENCIL) &&
                candidate.Width==desc.Width && candidate.Height==desc.Height)
                lensDepth=target.surface[eye];
        }
        if(!lensDepth)
        {
            D3DSURFACE_DESC candidate{};
            if(resources->worldDepth[eye] && SUCCEEDED(resources->worldDepth[eye]->GetDesc(&candidate)) &&
                candidate.Width==desc.Width && candidate.Height==desc.Height)
                lensDepth=resources->worldDepth[eye];
        }
        for(const auto& target:resources->targets)
        {
            if(lensDepth) break;
            D3DSURFACE_DESC candidate{};
            if(SUCCEEDED(target.surface[eye]->GetDesc(&candidate)) && (candidate.Usage&D3DUSAGE_DEPTHSTENCIL) &&
                candidate.Width==desc.Width && candidate.Height==desc.Height)
                {lensDepth=target.surface[eye];break;}
        }
        // The lens obeys scene depth rather than displaying over walls.
        if(!lensDepth && scopeSettings.depthTest) {++scopeFailures;return;}
        struct Vertex {float x,y,z,u,v;};Vertex vertices[4]{};
        for(unsigned i=0;i<4;++i)
        {
            const float u=(i&1) ? 1.f:0.f,v=(i&2) ? 1.f:0.f;
            const auto world=VRMath::Transform({(u-.5f)*scopeSettings.radius*2,
                (.5f-v)*scopeSettings.radius*2,scopeSettings.surfaceOffset,1},completedScopeCamera);
            if(!std::isfinite(world[0]+world[1]+world[2])) return;
            vertices[i]={world[0],world[1],world[2],u,v};
        }
        if(FAILED(device->CreateStateBlock(D3DSBT_ALL,&state)) || FAILED(state->Capture())) return;
        for(unsigned i=0;i<4;++i) device->GetRenderTarget(i,&targets[i]);
        device->GetDepthStencilSurface(&depth);device->GetViewport(&viewport);
        for(unsigned i=1;i<4;++i) device->SetRenderTarget(i,nullptr);
        device->SetDepthStencilSurface(nullptr);device->SetRenderTarget(0,resources->finalSurface[eye].Get());
        if(FAILED(device->SetDepthStencilSurface(lensDepth.Get())))
        {
            ++scopeFailures;
            for(unsigned i=0;i<4;++i) device->SetRenderTarget(i,targets[i].Get());
            device->SetDepthStencilSurface(depth.Get());state->Apply();device->SetViewport(&viewport);
            return;
        }
        // Use the geometry pass's depth range, not an assumed 0..1 range.
        // Keep homogeneous projection on the GPU so a near-plane crossing
        // clips the aperture rather than rejecting its entire CPU quad.
        const auto& worldViewport=resources->worldViewport[eye];
        const D3DVIEWPORT9 full{0,0,desc.Width,desc.Height,worldViewport.MinZ,worldViewport.MaxZ};device->SetViewport(&full);
        device->SetVertexShader(scopeLensVertexShader.Get());device->SetPixelShader(scopeLensShader.Get());
        device->SetVertexShaderConstantF(0,completedViewProjection[eye][0].data(),4);
        device->SetFVF(D3DFVF_XYZ|D3DFVF_TEX1);device->SetTexture(0,scopeResources->finalTexture[0].Get());
        if(scopeRenders.load()<4)
        {
            char line[200];sprintf_s(line,"scope lens eye=%u geometryDepth=%p phaseEndDepth=%p depthRange=%.6f..%.6f",eye,lensDepth.Get(),resources->worldDepth[eye].Get(),full.MinZ,full.MaxZ);Log(line);
        }
        const float parameters[]{float(desc.Height)/desc.Width,scopeSettings.debugLens ? 1.f:0.f,0,0};device->SetPixelShaderConstantF(0,parameters,1);
        device->SetPixelShaderConstantF(1,completedLogDepth[eye].data(),1);
        device->SetRenderState(D3DRS_ZENABLE,scopeSettings.depthTest);device->SetRenderState(D3DRS_ZWRITEENABLE,FALSE);
        device->SetRenderState(D3DRS_ZFUNC,D3DCMP_LESSEQUAL);
        // Native material draws can leave a depth bias behind. The aperture
        // has its own physical surface offset and must use unbiased depth.
        device->SetRenderState(D3DRS_DEPTHBIAS,0);
        device->SetRenderState(D3DRS_SLOPESCALEDEPTHBIAS,0);
        device->SetRenderState(D3DRS_FOGENABLE,FALSE);device->SetRenderState(D3DRS_CLIPPLANEENABLE,0);
        device->SetRenderState(D3DRS_STENCILENABLE,FALSE);device->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE);
        device->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);device->SetRenderState(D3DRS_FILLMODE,D3DFILL_SOLID);
        device->SetRenderState(D3DRS_COLORWRITEENABLE,15);device->SetRenderState(D3DRS_SRGBWRITEENABLE,FALSE);
        device->SetRenderState(D3DRS_ALPHATESTENABLE,FALSE);device->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE);
        device->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_LINEAR);device->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR);
        device->SetSamplerState(0,D3DSAMP_MIPFILTER,D3DTEXF_NONE);device->SetSamplerState(0,D3DSAMP_SRGBTEXTURE,FALSE);
        device->SetSamplerState(0,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);device->SetSamplerState(0,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);
        if(SUCCEEDED(device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,vertices,sizeof(Vertex)))) ++scopeLensDraws;else ++scopeFailures;
        device->SetDepthStencilSurface(nullptr);
        for(unsigned i=0;i<4;++i) device->SetRenderTarget(i,targets[i].Get());
        device->SetDepthStencilSurface(depth.Get());state->Apply();device->SetViewport(&viewport);
    }

    void PublishEyes(IDirect3DDevice9* device)
    {
        if(!submit || !resources || completedEyes!=3 || publishedFrame==completedFrame) return;
        // STOPPING/reset is temporary. Retain the completed pair until the
        // session can accept it instead of permanently disabling submission.
        if(!OpenXRVR::CanAcceptEyeImages()) return;
        VRProfile::Scope profile(VRProfile::Metric::Publish);
        VRHud::CaptureRadar(device,resources->originalBack.Get());
        composingHud=true;
        for(unsigned eye=0;eye<2;++eye) DrawScopeLens(device,eye);
        for(unsigned eye=0;eye<2;++eye) VRHud::Draw(device,resources->finalSurface[eye].Get(),completedViewProjection[eye],completedEyePosition[eye],completedWristValid[eye] ? &completedWrist[eye]:nullptr,completedDashboardValid[eye] ? &completedDashboard[eye]:nullptr);
        const auto hudPose=VRMath::BlendRigidPose(completedEyeCamera[0],completedEyeCamera[1],.5f);
        for(unsigned eye=0;eye<2;++eye) VRHud::DrawOverlay(device,resources->finalSurface[eye].Get(),completedViewProjection[eye],hudPose);
        VRHud::EndOverlayFrame();
        composingHud=false;
        if(mirror && maxFrames!=1)
            mirror=Check("desktop eye mirror",stretch.unsafe_stdcall<HRESULT>(device,
                resources->finalSurface[0].Get(),nullptr,resources->originalBack.Get(),nullptr,D3DTEXF_NONE));
        if(OpenXRVR::UsesVulkan())
        {
            D3DSURFACE_DESC desc{}; resources->finalSurface[0]->GetDesc(&desc);
            IDirect3DTexture9* textures[2]{resources->finalTexture[0].Get(),resources->finalTexture[1].Get()};
            if(OpenXRVR::SetVulkanEyeImages(textures,desc.Width,desc.Height,completedViews,completedPoseTime))
                publishedFrame=completedFrame;
            else {Log("Vulkan eye pair publication unavailable; retaining desktop");submit=false;}
            return;
        }
        ComPtr<IDirect3DQuery9> query;
        const auto fenceStart=VRProfile::Begin();
        if(!Check("eye D3D9 event",device->CreateQuery(D3DQUERYTYPE_EVENT,&query)) ||
            !Check("eye D3D9 event issue",query->Issue(D3DISSUE_END))) {submit=false; return;}
        const auto deadline=GetTickCount64()+1000;
        HRESULT status=S_FALSE;
        do {status=query->GetData(nullptr,0,D3DGETDATA_FLUSH); if(status==S_FALSE) Sleep(0);}
        while(status==S_FALSE && GetTickCount64()<deadline);
        VRProfile::End(VRProfile::Metric::D3D9Fence,fenceStart);
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
        if(!OpenXRVR::UsesVulkan() && !Check("live Ex device",device->QueryInterface(__uuidof(IDirect3DDevice9Ex),
            reinterpret_cast<void**>(ex.GetAddressOf())))) return;
        if(!Check("back buffer",device->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&owned->originalBack))) return;
        D3DSURFACE_DESC back{};
        if(!Check("back description",owned->originalBack->GetDesc(&back)) || back.MultiSampleType!=D3DMULTISAMPLE_NONE) return;
        worldTargetAspect=float(back.Width)/back.Height;
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
                D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,&owned->finalTexture[eye],OpenXRVR::UsesVulkan() ? nullptr : &owned->shared[eye])) ||
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
        // Hook the actual rendering device/chain rather than the startup dummy.
        if(!presentHook) presentHook=safetyhook::create_inline(table[17],DesktopPresent);
        if(ex)
        {
            auto** exTable=*reinterpret_cast<void***>(ex.Get());
            if(!presentExHook) presentExHook=safetyhook::create_inline(exTable[121],DesktopPresentEx);
        }
        if(desktopChain && !swapPresentHook)
            swapPresentHook=safetyhook::create_inline((*reinterpret_cast<void***>(desktopChain.Get()))[3],DesktopSwapPresent);
        if(!setTarget) setTarget=safetyhook::create_inline(table[37],SetTarget);
        if(!setDepth) setDepth=safetyhook::create_inline(table[39],SetDepth);
        if(!setTexture) setTexture=safetyhook::create_inline(table[65],SetTexture);
        if(!stretch) stretch=safetyhook::create_inline(table[34],Stretch);
        if(!setVertexShader) setVertexShader=safetyhook::create_inline(table[92],SetVertexShader);
        if(!setVertexConstants) setVertexConstants=safetyhook::create_inline(table[94],SetVertexConstants);
        if(!drawPrimitive) drawPrimitive=safetyhook::create_inline(table[81],DrawPrimitive);
        if(!drawIndexedPrimitive) drawIndexedPrimitive=safetyhook::create_inline(table[82],DrawIndexedPrimitive);
        if(!setPixelShader) setPixelShader=safetyhook::create_inline(table[107],SetPixelShader);
        if(!setPixelConstants) setPixelConstants=safetyhook::create_inline(table[109],SetPixelConstants);
        if(!setTarget || !setDepth || !setTexture || !stretch || !setVertexShader || !setVertexConstants || !drawPrimitive || !drawIndexedPrimitive || !setPixelShader || !setPixelConstants) {Log("Device routing hook unavailable"); return;}
        char line[160];
        sprintf_s(line,"allocated two eye sets targets=%zu finalColor=%ux%u format=21 shared=%u,%u",
            owned->targets.size(),back.Width,back.Height,owned->shared[0]!=nullptr,owned->shared[1]!=nullptr);
        Log(line);
        resources=owned.release();
        routingThread=GetCurrentThreadId();
        routingGeneration.fetch_add(1,std::memory_order_relaxed);
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
        if(!ready || !prepared || preparedFrame!=gameFrame)
            return sphereCull.unsafe_fastcall<int>(self,edx,x,y,z,radius,distance);
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
            return sphereCull.unsafe_fastcall<int>(self,edx,x,y,z,radius,distance);
        }
        ++sphereQueries[owner];
        const float center[]{x,y,z};
        const bool fast=sphereClassificationFast && std::isfinite(x) && std::isfinite(y) &&
            std::isfinite(z) && std::isfinite(radius) && radius>=0;
        // The original query only writes its optional distance output. The
        // legacy call/copy is retained as a reversible control and reference.
        const int original=fast ? 0 : sphereCull.unsafe_fastcall<int>(self,edx,x,y,z,radius,distance);
        auto classifyNative=[&]()
        {
            alignas(16) rage::grcViewport visibility=*self;
            std::memcpy(visibility.mFrustumClipPlanes,visibilityPlanes.data(),sizeof(visibility.mFrustumClipPlanes));
            return sphereCull.unsafe_fastcall<int>(&visibility,edx,x,y,z,radius,nullptr);
        };
        // The CE optional output is plane[0].dot(center)+radius. Plane zero
        // in our world AABB is an X boundary, not the engine's near plane.
        // Keep union visibility separate from the headset near-plane distance.
        int result=fast ? VRMath::ClassifySphere(visibilityPlanes,center,radius) : classifyNative();
        if(sphereClassificationProbeFrames)
        {
            ++sphereClassificationProbeQueries;
            const int reference=fast ? classifyNative() : result;
            if(reference!=VRMath::ClassifySphere(visibilityPlanes,center,radius))
                ++sphereClassificationProbeMismatches;
        }
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
        // Preserve native accepted classifications and near-plane output.
        // Only narrow entity tests; streaming/portal traversal keeps its broad
        // bounds, and auxiliary viewports are excluded by the owner checks.
        if(tightVisibility && result && VRMath::OutsideBothEyes(eyeVisibilityPlanes,center,radius)) result=0;
        if(!fast && !original && result) ++sphereCorrections[owner];
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
        VRProfile::Scope profile(VRProfile::Metric::Visibility);
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
        sphereClassificationFast=sphereClassificationTest.load(std::memory_order_relaxed);
        if(sphereClassificationProbeRequested.exchange(false))
        {
            sphereClassificationProbeFrames=120;
            sphereClassificationProbeQueries=sphereClassificationProbeMismatches=0;
            Log("VR sphere classification probe started; decisions unchanged");
        }
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
        scopeActive=false;
        queuedScopeGunValid=false;
        ReadScopeSettings();
        if(scopeProbeRequested.exchange(false)) scopeProbeFrames=30;
        VRMath::Matrix gun{};
        if(scopeSettings.enabled && scopeObjectRender && VRReloadRuntime::GetRenderedSniperPose(gun))
        {
            scopeCamera=VRScope::Camera(gun,scopeSettings);
            for(const auto& eye:eyeViewports)
            {
                VRMath::Vector position{};std::memcpy(position.data(),eye.mViewInverseMatrix[3],sizeof(position));
                if(VRScope::InEyeBox(scopeCamera,position,scopeSettings)) scopeActive=true;
            }
            alignas(16) auto opticalBase=*base;opticalBase.mAspect=worldTargetAspect;
            if(scopeProbeFrames) {scopeActive=true;--scopeProbeFrames;}
            scopeActive=scopeActive && VRCamera::GenerateOpticalView(opticalBase,scopeCamera,scopeSettings.verticalFov,scopeViewport);
        }
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
        tightVisibility=visibilityTest.load(std::memory_order_relaxed) && !VRProfile::BroadVisibilityEnabled();
        for(unsigned eye=0;eye<2;++eye)
            std::memcpy(eyeVisibilityPlanes[eye].data(),eyeViewports[eye].mFrustumClipPlanes,sizeof(eyeViewports[eye].mFrustumClipPlanes));
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
        VRProfile::Scope profile(VRProfile::Metric::FramePrepare);
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
            VRProfile::Scope profile(eye ? VRProfile::Metric::Eye1Build : VRProfile::Metric::Eye0Build);
            buildFrame.unsafe_fastcall(self,edx);
            if(stereoFrames==0) Log(eye ? "build eye 1 end" : "build eye 0 end");
        }
        // Main-eye construction has now finalized the weapon attachment.
        // Recompute the scope from that same queued pose, not the earlier
        // setup snapshot or a newer independently sampled controller pose.
        if(scopeSettings.enabled && queuedScopeGunValid)
        {
            scopeCamera=VRScope::Camera(queuedScopeGun,scopeSettings);
            scopeActive=false;
            for(const auto& eye:eyeViewports)
            {
                VRMath::Vector position{};std::memcpy(position.data(),eye.mViewInverseMatrix[3],sizeof(position));
                if(VRScope::InEyeBox(scopeCamera,position,scopeSettings)) scopeActive=true;
            }
            alignas(16) auto opticalBase=saved[0][0];opticalBase.mAspect=worldTargetAspect;
            if(scopeProbeFrames) scopeActive=true;
            scopeActive=scopeActive && VRCamera::GenerateOpticalView(opticalBase,scopeCamera,scopeSettings.verticalFov,scopeViewport);
        }
        if(scopeActive)
        {
            // Reuse only world phase dispatch; never repeat simulation, UI,
            // shared shadow/reflection construction or XR frame submission.
            const auto ordinaryEye=eyeViewports[0];
            const auto ordinaryOtherEye=eyeViewports[1];
            const auto ordinaryPlanes=visibilityPlanes;
            const auto ordinaryEyePlanes=eyeVisibilityPlanes[0];
            eyeViewports[0]=scopeViewport;
            eyeViewports[1]=scopeViewport;
            VRMath::Matrix cameras[2]{scopeCamera,scopeCamera};
            const float half=scopeSettings.verticalFov*.00872664626f;
            const float horizontal=std::atan(std::tan(half)*scopeViewport.mAspect);
            float fov[2][4]{{-horizontal,horizontal,half,-half},{-horizontal,horizontal,half,-half}};
            visibilityPlanes=VRMath::UnionBounds(cameras,fov,scopeViewport.mNearClip,scopeViewport.mFarClip);
            std::memcpy(eyeVisibilityPlanes[0].data(),scopeViewport.mFrustumClipPlanes,sizeof(scopeViewport.mFrustumClipPlanes));
            for(unsigned phase=0;phase<3;++phase)
                for(unsigned slot=0;slot<2;++slot)
                    std::memcpy(static_cast<char*>(phases[phase])+(slot ? 0x4A0 : 0xB0),&scopeViewport,sizeof(scopeViewport));
            phaseArray=eyeWorldPhases.data();phaseCount=static_cast<uint16_t>(eyeWorldPhases.size());
            buildingEye=0;buildingScope=true;
            buildFrame.unsafe_fastcall(self,edx);
            buildingScope=false;++scopeBuilds;
            eyeViewports[0]=ordinaryEye;eyeViewports[1]=ordinaryOtherEye;
            visibilityPlanes=ordinaryPlanes;eyeVisibilityPlanes[0]=ordinaryEyePlanes;
        }
        buildingEye=-1;
        buildingStage=0;
        if(sphereClassificationProbeFrames && --sphereClassificationProbeFrames==0)
        {
            char line[240];
            sprintf_s(line,"VR sphere classification probe complete frames=120 queries=%u mismatches=%u; decisions unchanged",
                sphereClassificationProbeQueries,sphereClassificationProbeMismatches);
            Log(line);
        }
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
            sprintf_s(line,"VR sphere queries scene=%u secondary=%u parent=%u legacyCorrected=%u,%u,%u",
                sphereQueries[0],sphereQueries[1],sphereQueries[2],sphereCorrections[0],sphereCorrections[1],sphereCorrections[2]);
            Log(line);
        }
    }
}

void VRStereo::Shutdown()
{
    VRHud::Reset();
    scopeActive=buildingScope=false;scopeCompletedFrame=~0u;scopeConfigRevision=~0u;
    scopeProbeRequested=false;scopeCaptureRequested=false;scopeProbeFrames=0;
    scopeLensShader.Reset();scopeLensVertexShader.Reset();delete scopeResources;scopeResources=nullptr;mainResources=nullptr;
    cpuOmitRequested=99;cpuOmitMode=0;cpuOmitDeadline=0;cpuOmitHook.reset();
    liveMeshRequested=0;liveMeshMode=0;liveMeshPackets.reset();liveMeshRenderer=VRRigidInstance::Renderer{};liveMeshClear.reset();RemoveLiveMeshResourceHooks();
    billboardInstancingRequested=false;
    StopGpuBillboards();
    rigidInstancingRequested=false;
    rigidInstances.reset();
    meshBenchmarkRequested=false;meshPacketRequested=false;meshOrderRequested=false;meshReplayRequested=false;meshCompareOrder=false;meshPacketProbe.reset();meshPacketShaders.clear();meshPacketMaterialProbe.reset();meshPacketMaterial=0;
    constantBatchRequested=false;textureBinderBatchBoundary.reset();vertexBinderBatchHook.reset();binderDevice=nullptr;binderEye=-1;binderConstants.Reset();
    modelProbeRequested=false;
    FinishModelCommandProbe();
    vertexProbeRequested=false;
    FinishVertexCallerProbe();
    vertexEmissionRequested=false;
    vertexEmissionHook.reset();
    routingGeneration.fetch_add(1,std::memory_order_relaxed);
    routingPhaseGeneration=0;
    visibilityTest=true;
    sphereClassificationProbeRequested=false;
    sphereClassificationProbeFrames=0;
    sphereClassificationTest=false;
    sphereClassificationFast=false;
    normalProbeActive=false;
    routingProbeActive=false;
    textureCacheBypass=false;
    drawPrimitive.reset(); drawIndexedPrimitive.reset();
    drawProbeActive=false; drawProbeEpoch=0; drawProbeCounters={};
    stateProbeActive=false; stateProbeEpoch=0;
    for(auto& counters:stateProbeCounters) counters={};
    setRenderState.reset(); setSamplerState.reset(); setStreamSource.reset();
    setIndices.reset(); setVertexDeclaration.reset();
    stateHooksReady=false; stateHooksFailed=false;
    for(auto& keys:normalProbeKeys) keys.clear();
    normalProbeShaders.clear();
    enabled=false;
    ready=false;
    renderEye=-1;
    renderStage=0;
    rayShaderBound=false;
    cascadeShaderBound=false;
    constantProbeActive=false;
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

bool VRStereo::SetVisibilityTest(bool value)
{
    if(value && (!enabled || !ready)) return false;
    visibilityTest=value;
    return true;
}

bool VRStereo::RequestSphereClassificationProbe()
{
    if(!enabled || !ready) return false;
    sphereClassificationProbeRequested=true;
    return true;
}

bool VRStereo::SetSphereClassificationTest(bool value)
{
    if(value && (!enabled || !ready)) return false;
    sphereClassificationTest=value;
    return true;
}

bool VRStereo::RequestVertexCallerProbe()
{
    if(!enabled || !ready) return false;
    vertexProbeRequested=true;
    return true;
}

bool VRStereo::SetVertexEmissionTest(bool value)
{
    if(value && (!enabled || !ready)) return false;
    vertexEmissionRequested=value;
    return true;
}

bool VRStereo::SetBillboardInstancingTest(bool value)
{
    if(value && (!enabled || !ready)) return false;
    billboardInstancingRequested=value;
    return true;
}

bool VRStereo::SetRigidInstancingTest(bool value)
{
    if(value && (!enabled || !ready))return false;
    rigidInstancingRequested=value;return true;
}

bool VRStereo::RequestMeshPacketProbe()
{
    if(!enabled || !ready)return false;
    meshPacketRequested=true;return true;
}

bool VRStereo::RequestMeshOrderProbe()
{
    if(!enabled || !ready)return false;
    meshOrderRequested=true;return true;
}

bool VRStereo::RequestMeshReplayProbe()
{
    if(!enabled || !ready)return false;
    meshReplayRequested=true;return true;
}

bool VRStereo::RequestMeshBenchmarkProbe()
{
    if(!enabled || !ready)return false;
    meshBenchmarkRequested=true;return true;
}

bool VRStereo::RequestLiveMeshProbe(bool replace)
{
    if(!enabled || !ready)return false;
    liveMeshRequested=replace ? 2:1;return true;
}

bool VRStereo::RequestCpuOmission(unsigned mode)
{
    if(mode>6 || (mode && (!enabled || !ready)))return false;
    cpuOmitRequested=mode;return true;
}

std::string VRStereo::CpuOmissionStatus()
{
    const auto now=GetTickCount64(),deadline=cpuOmitDeadline.load();char line[260];
    sprintf_s(line,"{\"ok\":true,\"mode\":%u,\"remainingMs\":%llu,\"skipped\":[%llu,%llu],\"upperBoundOnly\":true,\"gpuReplacement\":false}",
        cpuOmitMode.load(),deadline>now ? deadline-now:0,cpuOmitSkipped[0].load(),cpuOmitSkipped[1].load());
    return line;
}

bool VRStereo::SetConstantBatchTest(bool value)
{
    if(value && (!enabled || !ready))return false;
    constantBatchRequested=value;return true;
}

bool VRStereo::RequestModelCommandProbe()
{
    if(!enabled || !ready) return false;
    modelProbeRequested=true;
    return true;
}

bool VRStereo::RequestScopeProbe()
{
    if(!ready || !enabled) return false;
    scopeCaptureRequested=true;scopeProbeRequested=true;return true;
}

std::string VRStereo::Status()
{
    char line[400];
    AcquireSRWLockShared(&modelReferencesLock);
    sprintf_s(line,"{\"enabled\":%s,\"ready\":%s,\"builtFrames\":%u,\"frameBudget\":%u,\"continuous\":%s,\"modelReferencePeak\":%u,\"duplicateModelReferences\":%u,\"commandBufferCapacity\":%u,\"commandBufferPeak\":%u,\"commandBufferOverflows\":%u}",
        enabled ? "true" : "false",ready ? "true" : "false",stereoFrames.load(),maxFrames,maxFrames ? "false" : "true",
        retainedModelPeak,duplicateModelReferences,commandBufferCapacity,commandBufferPeak.load(),commandBufferOverflows.load());
    ReleaseSRWLockShared(&modelReferencesLock);
    std::string result=line;result.pop_back();
    result+=",\"scopeBuilds\":"+std::to_string(scopeBuilds.load())+",\"scopeRenders\":"+std::to_string(scopeRenders.load())+
        ",\"scopeLensDraws\":"+std::to_string(scopeLensDraws.load())+",\"scopeFailures\":"+std::to_string(scopeFailures.load())+
        ",\"scopeHiddenObjects\":"+std::to_string(scopeHiddenObjects.load())+"}";
    return result;
}

void VRStereo::Install()
{
    VRHud::SetOverlayRouting([](bool begin){
        if(begin && (!OnRoutingThread() || renderEye>=0 || composingHud)) return false;
        composingHud=begin;return true;
    });
    CIniReader ini("");
    if(!ini.ReadInteger("VR","ExperimentalStereo",0)) return;
    if(CIniReader(CSettings::d3d9cfgPath).ReadInteger("MAIN","API",0)!=0 && !OpenXRVR::UsesVulkan())
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
        auto object=hook::pattern("83 EC 08 56 8B F1 F7 86 10 02 00 00 00 00 10 00 0F 85 ? ? ? ? 8B 86");
        if(object.size()==1) scopeObjectRender=safetyhook::create_inline(object.get_first(),ScopeObjectRender);
        Log(scopeObjectRender ? "Scope owned-object filter installed" : "Scope owned-object filter unavailable");
        VRCamera::SetVisibilityAvailable(static_cast<bool>(pedRender));
        if(ini.ReadInteger("DEVELOPMENT","DebugFirstPersonBody",0) || ini.ReadInteger("VR","ExperimentalHandIK",0))
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
        if(!enabled || !RageDirect3DDevice9::m_pRealDevice) return;
        auto* device=*RageDirect3DDevice9::m_pRealDevice;
        if(VRHud::PublishScreen(device,!ready)) return;
        if(!ready) return;
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
    // Development-only phase omission: keep markers, command ownership and
    // auxiliary phase lists. Remove main-eye construction plus its commands.
    auto omitPhase=[](CBaseDC* phase,unsigned role)
    {
        const auto mode=cpuOmitMode.load();
        if(buildingEye<0 || buildingEye>1 || !ready || !prepared || preparedFrame!=gameFrame)return false;
        if(phase!=(role==1 ? scenePhase:role==2 ? lightingPhase:drawPhase))return false;
        const bool omit=mode==6 || (mode==3 && role==1) || (mode==4 && role==3) || (mode==5 && role==2);
        if(omit)++cpuOmitSkipped[buildingEye];
        return omit;
    };
    // Noncapturing wrappers access the same predicate through a static value.
    static const auto phasePredicate=omitPhase;
    CRenderPhaseDeferredLighting_SceneToGBuffer::OmitRenderList=[](CBaseDC* phase){return phasePredicate(phase,1);};
    CRenderPhaseDeferredLighting_LightsToScreen::OmitRenderList=[](CBaseDC* phase){return phasePredicate(phase,2);};
    CRenderPhaseDrawScene::OmitRenderList=[](CBaseDC* phase){return phasePredicate(phase,3);};
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
        if(ready) {if(device && !VRHud::WantsScreen(false)) {CaptureEyes(device); PublishEyes(device);} return;}
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
