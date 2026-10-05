module;
#include <windows.h>
#include <array>
#include <atomic>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

export module vrprofile;

// CPU wall-clock spans, including driver/runtime waits. Never label these GPU work.
export namespace VRProfile
{
    enum class Metric { ProcessStub, FramePrepare, Visibility, Eye0Build, Eye1Build,
        Eye0Commands, Eye1Commands, Publish, D3D9Fence, XRWait, XRCopy0, XRCopy1,
        XREnd, SubmissionInterval, DesktopPresent, Eye0GPU, Eye1GPU,
        XRImageWait0, XRImageWait1, XRCopyFence0, XRCopyFence1, ProcessCallbacks,
        GameplayLimiter, CutsceneLimiter, LoadingLimiter, LoadingSafety60, MinigameLimiter,
        RuntimePacingBypass, Eye0GBuffer, Eye1GBuffer, Eye0Lighting, Eye1Lighting,
        Eye0DrawScene, Eye1DrawScene, XRCopyIssue0, XRCopyIssue1,
        XRQueryCreate0, XRQueryCreate1, XRFlush0, XRFlush1, XRPoll0, XRPoll1,
        Eye0Routing, Eye1Routing, FrameOutsideEyes, InterEyeGap,
        RenderBeforeXRWait, XRBegin, XRLocateViews, XRQueueBegin, XRQueueAcquire,
        XRQueueRelease, XRQueueEnd, XRFrameWall, PosePredictionLag, AmbientOcclusion,
        Eye0VertexConstants, Eye1VertexConstants, Eye0PixelConstants, Eye1PixelConstants, Count };
    struct Stamp { long long ticks=0; unsigned epoch=0; };
    struct Total { unsigned epoch=0; double ms=0; };
    Stamp Begin();
    bool GPUQueriesEnabled();
    bool NonflushingPoll(); // Bounded diagnostic override; inactive captures retain normal behavior.
    bool DisableAmbientOcclusion(); // Bounded isolation; never changes saved settings.
    bool ConstantObservationEnabled();
    bool DrawObservationEnabled();
    bool DrawForwardingEnabled(); // Diagnostic control: detours without draw counters.
    bool TextureLinearEnabled(); // Diagnostic control: original texture scan.
    bool BroadVisibilityEnabled(); // Bounded control: original broad entity acceptance.
    bool StateObservationEnabled();
    enum class StateKind { Texture, RenderState, SamplerState, VertexShader, PixelShader,
        VertexConstants, PixelConstants, StreamSource, Indices, VertexDeclaration, Count };
    struct StateCounters {
        unsigned long long calls=0,failedCalls=0,timedCalls=0;
        double timedMs=0,maxTimedMs=0;
    };
    void StateCalls(unsigned eye,StateKind kind,unsigned capture,const StateCounters& counters);
    struct DrawCounters {
        unsigned long long calls=0,indexedCalls=0,primitives=0,vertices=0,failedCalls=0,timedCalls=0;
        double timedMs=0,maxTimedMs=0;
    };
    void DrawCalls(unsigned eye,unsigned stage,unsigned capture,const DrawCounters& counters);
    void End(Metric metric, Stamp stamp);
    void Record(Metric metric,unsigned capture,double ms);
    void Accumulate(Total& total,Stamp stamp); // Calling thread owns total; no per-call lock.
    void Flush(Metric metric,Total& total);
    void Submission();
    void EyeExecutionStarted(); // Render-thread boundary; one eye pair per sample.
    void BeforeXRWait();
    void ConstantWrites(unsigned eye,bool vertex,unsigned capture,unsigned long long calls,
        unsigned long long vectors,unsigned long long repeatedCandidates);
    std::string Command(const std::string& command);
    class Scope
    {
        Metric metric;
        Stamp stamp;
    public:
        explicit Scope(Metric value):metric(value),stamp(Begin()) {}
        ~Scope() {End(metric,stamp);}
        Scope(const Scope&)=delete;
        Scope& operator=(const Scope&)=delete;
    };
}

namespace
{
    constexpr unsigned capacity=8192;
    constexpr const char* names[]{"processStub","framePrepare","visibility","eye0Build","eye1Build",
        "eye0Commands","eye1Commands","publish","d3d9Fence","xrWait","xrCopy0","xrCopy1",
        "xrEnd","submissionInterval","desktopPresent","eye0GPU","eye1GPU",
        "xrImageWait0","xrImageWait1","xrCopyFence0","xrCopyFence1","processCallbacks",
        "gameplayLimiter","cutsceneLimiter","loadingLimiter","loadingSafety60","minigameLimiter",
        "runtimePacingBypass","eye0GBuffer","eye1GBuffer","eye0Lighting","eye1Lighting",
        "eye0DrawScene","eye1DrawScene","xrCopyIssue0","xrCopyIssue1",
        "xrQueryCreate0","xrQueryCreate1","xrFlush0","xrFlush1","xrPoll0","xrPoll1",
        "eye0Routing","eye1Routing","frameOutsideEyes","interEyeGap",
        "renderBeforeXRWait","xrBegin","xrLocateViews","xrQueueBegin","xrQueueAcquire",
        "xrQueueRelease","xrQueueEnd","xrFrameWall","posePredictionLag","ambientOcclusion",
        "eye0VertexConstants","eye1VertexConstants","eye0PixelConstants","eye1PixelConstants"};
    static_assert(std::size(names)==static_cast<unsigned>(VRProfile::Metric::Count));
    struct Samples { std::array<double,capacity> values{}; unsigned count=0,dropped=0; };
    std::array<Samples,std::size(names)> samples;
    SRWLOCK lock=SRWLOCK_INIT;
    std::atomic<unsigned> activeEpoch{0};
    std::atomic<bool> gpuQueries{true};
    std::atomic<bool> noFlushPoll{false};
    std::atomic<bool> noAO{false};
    std::atomic<bool> observeConstants{false};
    std::atomic<bool> observeDraws{false};
    std::atomic<bool> forwardDraws{false};
    std::atomic<bool> textureLinear{false};
    std::atomic<bool> broadVisibility{false};
    std::atomic<bool> observeStates{false};
    std::atomic<ULONGLONG> effectDeadline{0};
    unsigned epoch=0;
    ULONGLONG started=0;
    long long lastSubmission=0;
    thread_local VRProfile::Stamp eyeExecutionStart{};
    struct ConstantCounters {unsigned long long streams=0,calls=0,vectors=0,repeatedCandidates=0;};
    ConstantCounters constantCounters[2][2]{};
    struct DrawTotals {unsigned long long streams=0; VRProfile::DrawCounters counters;};
    DrawTotals drawTotals[2][3]{};
    constexpr const char* stateNames[]{"texture","renderState","samplerState","vertexShader","pixelShader",
        "vertexConstants","pixelConstants","streamSource","indices","vertexDeclaration"};
    static_assert(std::size(stateNames)==static_cast<unsigned>(VRProfile::StateKind::Count));
    struct StateTotals {unsigned long long streams=0; VRProfile::StateCounters counters;};
    StateTotals stateTotals[2][std::size(stateNames)]{};
    const double ticksPerMs=[] {LARGE_INTEGER frequency{}; QueryPerformanceFrequency(&frequency); return frequency.QuadPart/1000.0;}();
    long long Now() {LARGE_INTEGER value{}; QueryPerformanceCounter(&value); return value.QuadPart;}
    void Add(unsigned index,double ms)
    {
        auto& set=samples[index];
        if(set.count<capacity) set.values[set.count++]=ms;
        else ++set.dropped;
    }
    bool Expired()
    {
        if(GetTickCount64()-started>=30000) activeEpoch.store(0);
        return activeEpoch.load()==0;
    }
}

VRProfile::Stamp VRProfile::Begin()
{
    const auto current=activeEpoch.load(std::memory_order_relaxed);
    return current ? Stamp{Now(),current} : Stamp{};
}
bool VRProfile::GPUQueriesEnabled() {return gpuQueries.load(std::memory_order_relaxed);}
bool VRProfile::NonflushingPoll()
{return activeEpoch.load(std::memory_order_relaxed)!=0 && noFlushPoll.load(std::memory_order_relaxed);}
bool VRProfile::DisableAmbientOcclusion()
{return activeEpoch.load()!=0 && noAO.load() && GetTickCount64()<effectDeadline.load();}
bool VRProfile::ConstantObservationEnabled()
{return activeEpoch.load(std::memory_order_relaxed)!=0 && observeConstants.load(std::memory_order_relaxed);}
bool VRProfile::DrawObservationEnabled()
{return activeEpoch.load(std::memory_order_relaxed)!=0 && observeDraws.load(std::memory_order_relaxed);}
bool VRProfile::DrawForwardingEnabled()
{return activeEpoch.load(std::memory_order_relaxed)!=0 && forwardDraws.load(std::memory_order_relaxed);}
bool VRProfile::TextureLinearEnabled()
{return activeEpoch.load(std::memory_order_relaxed)!=0 && textureLinear.load(std::memory_order_relaxed);}
bool VRProfile::BroadVisibilityEnabled()
{return activeEpoch.load(std::memory_order_relaxed)!=0 && broadVisibility.load(std::memory_order_relaxed) && GetTickCount64()<effectDeadline.load(std::memory_order_relaxed);}
bool VRProfile::StateObservationEnabled()
{return activeEpoch.load(std::memory_order_relaxed)!=0 && observeStates.load(std::memory_order_relaxed);}
void VRProfile::Accumulate(Total& total,Stamp stamp)
{
    if(!stamp.ticks || activeEpoch.load(std::memory_order_relaxed)!=stamp.epoch) return;
    if(total.epoch!=stamp.epoch) total={stamp.epoch,0};
    total.ms+=(Now()-stamp.ticks)/ticksPerMs;
}
void VRProfile::Flush(Metric metric,Total& total)
{
    Record(metric,total.epoch,total.ms);
    total={};
}
void VRProfile::End(Metric metric,Stamp stamp)
{
    if(!stamp.ticks || activeEpoch.load(std::memory_order_relaxed)!=stamp.epoch) return;
    const auto now=Now();
    AcquireSRWLockExclusive(&lock);
    if(activeEpoch.load()==stamp.epoch && !Expired())
        Add(static_cast<unsigned>(metric),(now-stamp.ticks)/ticksPerMs);
    ReleaseSRWLockExclusive(&lock);
}
void VRProfile::EyeExecutionStarted() {eyeExecutionStart=Begin();}
void VRProfile::BeforeXRWait()
{
    End(Metric::RenderBeforeXRWait,eyeExecutionStart);
    eyeExecutionStart={}; // No duplicate or stale sample on a later EndScene.
}
void VRProfile::Submission()
{
    const auto capture=activeEpoch.load(std::memory_order_relaxed);
    if(!capture) return;
    const auto now=Now();
    AcquireSRWLockExclusive(&lock);
    if(activeEpoch.load()==capture && !Expired())
    {
        if(lastSubmission) Add(static_cast<unsigned>(Metric::SubmissionInterval),(now-lastSubmission)/ticksPerMs);
        lastSubmission=now;
    }
    ReleaseSRWLockExclusive(&lock);
}
void VRProfile::Record(Metric metric,unsigned capture,double ms)
{
    if(!capture || !std::isfinite(ms) || ms<0) return;
    AcquireSRWLockExclusive(&lock);
    if(activeEpoch.load()==capture && !Expired()) Add(static_cast<unsigned>(metric),ms);
    ReleaseSRWLockExclusive(&lock);
}
void VRProfile::ConstantWrites(unsigned eye,bool vertex,unsigned capture,unsigned long long calls,
    unsigned long long vectors,unsigned long long repeatedCandidates)
{
    if(eye>1 || !capture || activeEpoch.load()!=capture) return;
    AcquireSRWLockExclusive(&lock);
    if(activeEpoch.load()==capture && !Expired())
    {
        auto& counters=constantCounters[eye][vertex ? 0:1];
        ++counters.streams; counters.calls+=calls; counters.vectors+=vectors;
        counters.repeatedCandidates+=repeatedCandidates;
    }
    ReleaseSRWLockExclusive(&lock);
}
void VRProfile::DrawCalls(unsigned eye,unsigned stage,unsigned capture,const DrawCounters& counters)
{
    if(eye>1 || stage<1 || stage>3 || !capture || !std::isfinite(counters.timedMs) ||
        !std::isfinite(counters.maxTimedMs) || counters.timedMs<0 || counters.maxTimedMs<0 ||
        counters.timedCalls>counters.calls || counters.indexedCalls>counters.calls || counters.failedCalls>counters.calls) return;
    AcquireSRWLockExclusive(&lock);
    if(activeEpoch.load()==capture && observeDraws.load() && !Expired())
    {
        auto& total=drawTotals[eye][stage-1]; ++total.streams;
        auto& out=total.counters;
        out.calls+=counters.calls; out.indexedCalls+=counters.indexedCalls;
        out.primitives+=counters.primitives; out.vertices+=counters.vertices;
        out.failedCalls+=counters.failedCalls; out.timedCalls+=counters.timedCalls;
        out.timedMs+=counters.timedMs; out.maxTimedMs=(std::max)(out.maxTimedMs,counters.maxTimedMs);
    }
    ReleaseSRWLockExclusive(&lock);
}
void VRProfile::StateCalls(unsigned eye,StateKind kind,unsigned capture,const StateCounters& counters)
{
    const auto index=static_cast<unsigned>(kind);
    if(eye>1 || index>=std::size(stateNames) || !capture || !std::isfinite(counters.timedMs) ||
        !std::isfinite(counters.maxTimedMs) || counters.timedMs<0 || counters.maxTimedMs<0 ||
        counters.timedCalls>counters.calls || counters.failedCalls>counters.calls) return;
    AcquireSRWLockExclusive(&lock);
    if(activeEpoch.load()==capture && observeStates.load() && !Expired())
    {
        auto& total=stateTotals[eye][index]; ++total.streams;
        auto& out=total.counters;
        out.calls+=counters.calls; out.failedCalls+=counters.failedCalls; out.timedCalls+=counters.timedCalls;
        out.timedMs+=counters.timedMs; out.maxTimedMs=(std::max)(out.maxTimedMs,counters.maxTimedMs);
    }
    ReleaseSRWLockExclusive(&lock);
}
std::string VRProfile::Command(const std::string& command)
{
    AcquireSRWLockExclusive(&lock);
    if(command=="profile-start" || command=="profile-start-cpu" || command=="profile-start-cpu-noflush" || command=="profile-start-cpu-noao" || command=="profile-start-cpu-constants" || command=="profile-start-cpu-draws" || command=="profile-start-cpu-states" || command=="profile-start-cpu-drawforwarding" || command=="profile-start-cpu-texturelinear" || command=="profile-start-cpu-broadvisibility")
    {
        activeEpoch.store(0);
        gpuQueries.store(command=="profile-start");
        noFlushPoll.store(command=="profile-start-cpu-noflush");
        noAO.store(command=="profile-start-cpu-noao");
        observeConstants.store(command=="profile-start-cpu-constants");
        observeDraws.store(command=="profile-start-cpu-draws");
        forwardDraws.store(command=="profile-start-cpu-drawforwarding");
        textureLinear.store(command=="profile-start-cpu-texturelinear");
        broadVisibility.store(command=="profile-start-cpu-broadvisibility");
        observeStates.store(command=="profile-start-cpu-states");
        for(auto& set:samples) {set.count=0; set.dropped=0;}
        for(auto& eye:constantCounters) for(auto& counters:eye) counters={};
        for(auto& eye:drawTotals) for(auto& total:eye) total={};
        for(auto& eye:stateTotals) for(auto& total:eye) total={};
        started=GetTickCount64(); effectDeadline.store(started+30000); lastSubmission=0;
        if(++epoch==0) ++epoch;
        activeEpoch.store(epoch);
    }
    if(command=="profile-stop") activeEpoch.store(0);
    const bool active=started && !Expired();
    const auto elapsed=started ? std::min<ULONGLONG>(GetTickCount64()-started,30000) : 0;
    if(command.rfind("profile-states ",0)==0)
    {
        unsigned eye=0,kind=0; char extra=0;
        if(sscanf_s(command.c_str(),"profile-states %u %u %c",&eye,&kind,&extra,1)!=2 || eye>1 || kind>=std::size(stateNames))
        {ReleaseSRWLockExclusive(&lock); return "{\"ok\":false,\"error\":\"expected eye 0-1 and kind 0-9\"}";}
        const auto& total=stateTotals[eye][kind]; const auto& c=total.counters;
        char output[640];
        sprintf_s(output,"{\"ok\":true,\"epoch\":%u,\"active\":%s,\"eye\":%u,\"kind\":\"%s\",\"streams\":%llu,\"calls\":%llu,\"failedCalls\":%llu,\"timedCalls\":%llu,\"sampledDeviceMs\":%.6f,\"maxSampledDeviceMs\":%.6f,\"sampledMeanDeviceUs\":%.6f}",
            epoch,active ? "true":"false",eye,stateNames[kind],total.streams,c.calls,c.failedCalls,c.timedCalls,
            c.timedMs,c.maxTimedMs,c.timedCalls ? c.timedMs*1000/c.timedCalls:0);
        ReleaseSRWLockExclusive(&lock); return output;
    }
    if(command.rfind("profile-draws ",0)==0)
    {
        unsigned eye=0,stage=0; char extra=0;
        if(sscanf_s(command.c_str(),"profile-draws %u %u %c",&eye,&stage,&extra,1)!=2 || eye>1 || stage<1 || stage>3)
        {ReleaseSRWLockExclusive(&lock); return "{\"ok\":false,\"error\":\"expected eye 0-1 and stage 1-3\"}";}
        const auto& total=drawTotals[eye][stage-1]; const auto& c=total.counters;
        char output[768];
        sprintf_s(output,"{\"ok\":true,\"epoch\":%u,\"active\":%s,\"eye\":%u,\"stage\":%u,\"streams\":%llu,\"calls\":%llu,\"indexedCalls\":%llu,\"primitives\":%llu,\"vertices\":%llu,\"failedCalls\":%llu,\"timedCalls\":%llu,\"sampledDeviceMs\":%.6f,\"maxSampledDeviceMs\":%.6f,\"sampledMeanDeviceUs\":%.6f}",
            epoch,active ? "true":"false",eye,stage,total.streams,c.calls,c.indexedCalls,c.primitives,c.vertices,
            c.failedCalls,c.timedCalls,c.timedMs,c.maxTimedMs,c.timedCalls ? c.timedMs*1000/c.timedCalls:0);
        ReleaseSRWLockExclusive(&lock); return output;
    }
    if(command=="profile-constants")
    {
        std::string output="{\"ok\":true,\"epoch\":"+std::to_string(epoch)+",\"eyes\":[";
        for(unsigned eye=0;eye<2;++eye)
        {
            if(eye) output+=",";
            output+="{";
            for(unsigned kind=0;kind<2;++kind)
            {
                if(kind) output+=",";
                const auto& counters=constantCounters[eye][kind];
                char entry[240];
                sprintf_s(entry,"\"%s\":{\"streams\":%llu,\"calls\":%llu,\"vectors\":%llu,\"repeatedCandidates\":%llu}",
                    kind ? "pixel":"vertex",counters.streams,counters.calls,counters.vectors,counters.repeatedCandidates);
                output+=entry;
            }
            output+="}";
        }
        ReleaseSRWLockExclusive(&lock);
        return output+"]}";
    }
    if(command.rfind("profile-metric ",0)==0)
    {
        const auto name=command.substr(15);
        unsigned index=0;
        while(index<std::size(names) && name!=names[index]) ++index;
        if(index==std::size(names)) {ReleaseSRWLockExclusive(&lock); return "{\"ok\":false,\"error\":\"unknown metric\"}";}
        const auto& set=samples[index];
        std::vector<double> values(set.values.begin(),set.values.begin()+set.count);
        const auto dropped=set.dropped;
        const auto capture=epoch;
        ReleaseSRWLockExclusive(&lock);
        std::sort(values.begin(),values.end());
        double sum=0; unsigned over=0;
        for(double ms:values) {sum+=ms; if(ms>1000.0/90.0) ++over;}
        const auto percentile=[&](double p) {return values.empty() ? 0.0 : values[static_cast<size_t>(std::ceil(p*values.size()))-1];};
        char output[512];
        sprintf_s(output,"{\"ok\":true,\"epoch\":%u,\"active\":%s,\"metric\":\"%s\",\"samples\":%u,\"dropped\":%u,\"meanMs\":%.4f,\"medianMs\":%.4f,\"p95Ms\":%.4f,\"p99Ms\":%.4f,\"maxMs\":%.4f,\"over11_11ms\":%u}",capture,active ? "true":"false",names[index],static_cast<unsigned>(values.size()),dropped,values.empty()?0:sum/values.size(),percentile(.5),percentile(.95),percentile(.99),values.empty()?0:values.back(),over);
        return output;
    }
    char output[448];
    sprintf_s(output,"{\"ok\":true,\"epoch\":%u,\"active\":%s,\"elapsedMs\":%llu,\"durationMs\":30000,\"gpuQueries\":%s,\"nonflushingPoll\":%s,\"aoBypassSelected\":%s,\"constantObserverSelected\":%s,\"drawObserverSelected\":%s,\"stateObserverSelected\":%s}",epoch,active ? "true":"false",elapsed,gpuQueries.load() ? "true":"false",noFlushPoll.load() ? "true":"false",noAO.load() ? "true":"false",observeConstants.load() ? "true":"false",observeDraws.load() ? "true":"false",observeStates.load() ? "true":"false");
    ReleaseSRWLockExclusive(&lock);
    return output;
}
