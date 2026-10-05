#define NOMINMAX
#include <windows.h>
#include <evntrace.h>
#include <evntcons.h>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <vector>

// Export CPU Profile stacks only: correlate StackWalk's original event QPC
// with the sampled-profile event, rather than treating every stack as CPU work.
namespace {
    constexpr GUID profileGuid{0xce1dbfb4,0x137e,0x4da6,{0x87,0xb0,0x3f,0x59,0xaa,0x10,0x2c,0xbc}};
    constexpr GUID stackGuid{0xdef2fe46,0x7bd6,0x4b80,{0xbd,0x94,0xf5,0x7f,0xe2,0x0d,0x0c,0xe3}};
    DWORD thread=0;
    LONGLONG beginFileTime=0,endFileTime=0;
    bool convertedPass=false;
    size_t sampleIndex=0;
    unsigned mismatches=0;
    struct Sample {LONGLONG qpc; unsigned long long ip;};
    std::vector<Sample> rawSamples;
    std::set<LONGLONG> profiles;
    std::map<LONGLONG,std::vector<unsigned long long>> stacks;
    unsigned duplicates=0;
    std::map<LONGLONG,unsigned> duplicatesByTime;
    void WINAPI Record(EVENT_RECORD* e) {
        const auto* data=static_cast<const unsigned char*>(e->UserData);
        if(std::memcmp(&e->EventHeader.ProviderId,&profileGuid,sizeof(GUID))==0 &&
            e->EventHeader.EventDescriptor.Opcode==46 && e->UserDataLength>=16) {
            if(e->EventHeader.Flags&EVENT_HEADER_FLAG_32_BIT_HEADER) return;
            DWORD tid=0; std::memcpy(&tid,data+8,4);
            if(tid!=thread) return;
            unsigned long long ip=0;std::memcpy(&ip,data,8);
            if(!convertedPass) rawSamples.push_back({e->EventHeader.TimeStamp.QuadPart,ip});
            else {
                if(sampleIndex>=rawSamples.size() || rawSamples[sampleIndex].ip!=ip) {++mismatches;++sampleIndex;return;}
                if(e->EventHeader.TimeStamp.QuadPart>=beginFileTime && e->EventHeader.TimeStamp.QuadPart<endFileTime)
                    profiles.insert(rawSamples[sampleIndex].qpc);
                ++sampleIndex;
            }
        } else if(std::memcmp(&e->EventHeader.ProviderId,&stackGuid,sizeof(GUID))==0 &&
            e->EventHeader.EventDescriptor.Opcode==32 && e->UserDataLength>=24) {
            if(convertedPass) return;
            LONGLONG time=0; DWORD tid=0; std::memcpy(&time,data,8); std::memcpy(&tid,data+12,4);
            if(tid!=thread) return;
            const unsigned stride=(e->EventHeader.Flags&EVENT_HEADER_FLAG_32_BIT_HEADER)?4:8;
            std::vector<unsigned long long> frames;
            for(unsigned offset=16;offset+stride<=e->UserDataLength;offset+=stride) {
                unsigned long long ip=0;std::memcpy(&ip,data+offset,stride);frames.push_back(ip);
            }
            const auto found=stacks.find(time);
            if(found==stacks.end()) stacks.emplace(time,std::move(frames));
            else {
                ++duplicates; ++duplicatesByTime[time];
                // Kernel and user stack fragments can share the original event
                // timestamp. Preserve both in sampled-leaf-first order.
                const bool oldKernel=!found->second.empty() && found->second.front()>0xffffffffULL;
                const bool newKernel=!frames.empty() && frames.front()>0xffffffffULL;
                if(oldKernel!=newKernel) {
                    if(oldKernel) found->second.insert(found->second.end(),frames.begin(),frames.end());
                    else {frames.insert(frames.end(),found->second.begin(),found->second.end());found->second=std::move(frames);}
                } else if(frames.size()>found->second.size()) found->second=std::move(frames);
            }
        }
    }
}
int wmain(int argc,wchar_t** argv) {
    if(argc!=4) {std::fprintf(stderr,"Usage: export-cpu-stacks trace.etl threadID seconds > stacks.txt\n");return 1;}
    thread=wcstoul(argv[2],nullptr,10);
    const unsigned seconds=wcstoul(argv[3],nullptr,10);
    if(!thread || !seconds || seconds>60) return 1;
    EVENT_TRACE_LOGFILEW trace{};trace.LogFileName=argv[1];
    trace.ProcessTraceMode=PROCESS_TRACE_MODE_EVENT_RECORD|PROCESS_TRACE_MODE_RAW_TIMESTAMP;
    trace.EventRecordCallback=Record;
    auto handle=OpenTraceW(&trace);
    if(handle==INVALID_PROCESSTRACE_HANDLE) return 2;
    beginFileTime=trace.LogfileHeader.StartTime.QuadPart;
    endFileTime=beginFileTime+static_cast<LONGLONG>(seconds)*10000000;
    const auto result=ProcessTrace(&handle,1,nullptr,nullptr);CloseTrace(handle);
    if(result!=ERROR_SUCCESS) return 3;
    // Let ETW perform its calibrated clock conversion. BootTime subtraction
    // alone is not an exact QPC conversion. Match the ordered per-thread IP
    // sequence in a second pass and fail rather than guessing if it changes.
    convertedPass=true;
    trace.ProcessTraceMode=PROCESS_TRACE_MODE_EVENT_RECORD;
    handle=OpenTraceW(&trace);
    if(handle==INVALID_PROCESSTRACE_HANDLE) return 2;
    const auto second=ProcessTrace(&handle,1,nullptr,nullptr);CloseTrace(handle);
    if(second!=ERROR_SUCCESS || mismatches || sampleIndex!=rawSamples.size()) {
        std::fprintf(stderr,"Clock-pass sequence mismatch: %u, raw=%zu converted=%zu\n",mismatches,rawSamples.size(),sampleIndex);return 5;
    }
    unsigned matched=0;
    unsigned matchedDuplicates=0;
    std::map<std::vector<unsigned long long>,unsigned> folded;
    for(auto time:profiles) {
        const auto found=stacks.find(time);
        if(found==stacks.end()) continue;
        ++matched;
        matchedDuplicates+=duplicatesByTime[time];
        // ETW stores sampled leaf first. Folded stacks are root first.
        std::vector<unsigned long long> rootFirst(found->second.rbegin(),found->second.rend());
        ++folded[rootFirst];
    }
    std::fprintf(stderr,"thread=%lu profileEvents=%zu matched=%u missing=%zu duplicateStackTimes=%u matchedDuplicates=%u lost=%lu frequency=%lld\n",
        thread,profiles.size(),matched,profiles.size()-matched,duplicates,matchedDuplicates,trace.LogfileHeader.EventsLost,trace.LogfileHeader.PerfFreq.QuadPart);
    for(const auto& row:folded) {
        bool first=true;
        for(auto ip:row.first) {std::printf("%s%016llX",first?"":";",ip);first=false;}
        std::printf(" %u\n",row.second);
    }
    return matched?0:4;
}
