#define NOMINMAX
#include <windows.h>
#include <evntrace.h>
#include <evntcons.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <vector>

namespace {
    DWORD targetThread=0;
    LONGLONG startTime=0,endTime=0;
    std::map<unsigned long long,unsigned> hits;
    std::map<std::pair<unsigned long long,unsigned long long>,unsigned> providers;
    // Classic kernel PerfInfo provider. Opcode 46 on unrelated providers
    // must not be interpreted as this payload.
    constexpr GUID perfInfo{0xce1dbfb4,0x137e,0x4da6,{0x87,0xb0,0x3f,0x59,0xaa,0x10,0x2c,0xbc}};
    void WINAPI Record(EVENT_RECORD* event)
    {
        if(std::memcmp(&event->EventHeader.ProviderId,&perfInfo,sizeof(perfInfo))!=0) return;
        if(event->EventHeader.EventDescriptor.Opcode!=46 || event->UserDataLength<16) return;
        if(event->EventHeader.TimeStamp.QuadPart<startTime || event->EventHeader.TimeStamp.QuadPart>=endTime) return;
        // Kernel sampled-profile payload: native trace pointer, thread ID,
        // count/reserved. Verify provider and totals against xperf before use.
        if(event->EventHeader.Flags&EVENT_HEADER_FLAG_32_BIT_HEADER) return;
        unsigned long long ip=0; DWORD thread=0;
        std::memcpy(&ip,event->UserData,sizeof(ip));
        std::memcpy(&thread,static_cast<char*>(event->UserData)+8,sizeof(thread));
        if(thread!=targetThread) return;
        ++hits[ip];
        unsigned long long guid[2]{};
        std::memcpy(guid,&event->EventHeader.ProviderId,sizeof(guid));
        ++providers[{guid[0],guid[1]}];
    }
}

int wmain(int argc,wchar_t** argv)
{
    if(argc!=4) {std::fprintf(stderr,"Usage: summarize-cpu-samples trace.etl threadID seconds\n");return 1;}
    targetThread=wcstoul(argv[2],nullptr,10);
    const unsigned seconds=wcstoul(argv[3],nullptr,10);
    if(!targetThread || !seconds || seconds>60) return 1;
    EVENT_TRACE_LOGFILEW trace{};
    trace.LogFileName=argv[1];
    trace.ProcessTraceMode=PROCESS_TRACE_MODE_EVENT_RECORD;
    trace.EventRecordCallback=Record;
    TRACEHANDLE handle=OpenTraceW(&trace);
    if(handle==INVALID_PROCESSTRACE_HANDLE) {std::fprintf(stderr,"OpenTrace failed: %lu\n",GetLastError());return 2;}
    startTime=trace.LogfileHeader.StartTime.QuadPart;
    endTime=startTime+static_cast<LONGLONG>(seconds)*10000000;
    const auto result=ProcessTrace(&handle,1,nullptr,nullptr);
    CloseTrace(handle);
    if(result!=ERROR_SUCCESS) {std::fprintf(stderr,"ProcessTrace failed: %lu\n",result);return 3;}
    unsigned total=0; for(const auto& entry:hits) total+=entry.second;
    std::printf("thread=%lu seconds=%u samples=%u uniqueIPs=%zu lostEvents=%lu\n",targetThread,seconds,total,hits.size(),trace.LogfileHeader.EventsLost);
    for(const auto& entry:providers) {
        GUID guid{}; unsigned long long words[]{entry.first.first,entry.first.second};
        std::memcpy(&guid,words,sizeof(guid));
        std::printf("provider={%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X} samples=%u\n",guid.Data1,guid.Data2,guid.Data3,
            guid.Data4[0],guid.Data4[1],guid.Data4[2],guid.Data4[3],guid.Data4[4],guid.Data4[5],guid.Data4[6],guid.Data4[7],entry.second);
    }
    for(const auto& entry:hits) std::printf("ip=%016llX hits=%u\n",entry.first,entry.second);
    return total ? 0 : 4;
}
