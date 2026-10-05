#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <TlHelp32.h>
#include "../external/injector/zydis/Zydis.h"
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <vector>
#include <sstream>
#include <string>
#include <cstring>
#include <algorithm>

// Read-only inspection of a bounded live GTAIV code region. Arguments are
// process ID, analysis VA (image base 0x400000), and byte count <= 64 KiB.
// An optional module filename allows read-only inspection of a loaded bridge.
int main(int argc, char** argv)
{
    const bool scan=argc==4 && !std::strcmp(argv[2],"--pattern");
    const bool calls=argc==4 && !std::strcmp(argv[2],"--calls");
    const bool liveAddress=argc==5 && !std::strcmp(argv[2],"--live");
    if (argc!=4 && argc!=5) { std::fprintf(stderr,"Usage: inspect-game-code PID AnalysisVA byteCount [module.dll] | PID --pattern \"AA ? BB\" | PID --calls AnalysisVA | PID --live address byteCount\n"); return 1; }
    const std::string module=argc==5 && !liveAddress ? argv[4] : "GTAIV.exe";
    const auto pid=static_cast<DWORD>(std::strtoul(argv[1],nullptr,0));
    const auto va=std::strtoull(argv[liveAddress || calls ? 3 : 2],nullptr,0);
    const auto length=std::strtoul(argv[liveAddress ? 4 : 3],nullptr,0);
    if (!scan && !calls && ((!liveAddress && va<0x400000) || !length || length>65536 ||
        (liveAddress && (va<0x10000 || va+length>0x100000000ull)))) return 1;
    const auto snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPMODULE|TH32CS_SNAPMODULE32,pid);
    if (snapshot==INVALID_HANDLE_VALUE) return 2;
    MODULEENTRY32W entry{sizeof(entry)};
    bool found=false;
    if (Module32FirstW(snapshot,&entry)) do {
        const std::wstring moduleWide(module.begin(),module.end());
        if (!_wcsicmp(entry.szModule,moduleWide.c_str())) { found=true; break; }
    } while(Module32NextW(snapshot,&entry));
    CloseHandle(snapshot);
    if (!found || (!scan && !calls && !liveAddress && va-0x400000+length>entry.modBaseSize)) return 3;
    if(calls && (va<0x400000 || va-0x400000>=entry.modBaseSize)) return 1;
    const auto process=OpenProcess(PROCESS_VM_READ|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);
    if (!process) return 4;
    if(liveAddress)
    {
        // Inspect only one committed executable region in a process that has
        // the GTAIV module. No writes, injection or arbitrary execution.
        MEMORY_BASIC_INFORMATION region{};
        if(!VirtualQueryEx(process,reinterpret_cast<void*>(va),&region,sizeof(region)) ||
            region.State!=MEM_COMMIT || (region.Protect&PAGE_GUARD) ||
            !(region.Protect&(PAGE_EXECUTE|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY)) ||
            va+length>reinterpret_cast<uintptr_t>(region.BaseAddress)+region.RegionSize)
        {CloseHandle(process);return 7;}
        std::printf("Live executable region base=%p size=%zu type=%lu protect=%lu (not a Ghidra address)\n",
            region.BaseAddress,region.RegionSize,region.Type,region.Protect);
    }
    if(scan || calls)
    {
        std::vector<int> pattern;
        std::istringstream input(calls ? "E8 ? ? ? ?" : argv[3]); std::string token;
        while(input>>token) pattern.push_back(token=="?" || token=="??" ? -1 : std::strtoul(token.c_str(),nullptr,16));
        if(pattern.empty() || pattern.size()>256 || entry.modBaseSize>0x4000000) {CloseHandle(process); return 1;}
        std::vector<unsigned char> memory(entry.modBaseSize);
        unsigned matches=0;
        for(size_t start=0;start<memory.size();start+=65536)
        {
            SIZE_T read=0;
            const size_t size=std::min<size_t>(65536+pattern.size()-1,memory.size()-start);
            if(!ReadProcessMemory(process,entry.modBaseAddr+start,memory.data()+start,size,&read)) continue;
            for(size_t at=0;at+pattern.size()<=read && at<65536;++at)
            {
                bool match=true;
                for(size_t i=0;i<pattern.size();++i)
                    if(pattern[i]>=0 && memory[start+at+i]!=pattern[i]) {match=false; break;}
                if(match && calls)
                {
                    int32_t displacement{};
                    std::memcpy(&displacement,memory.data()+start+at+1,sizeof(displacement));
                    match=static_cast<uint32_t>(0x400000ull+start+at+5+displacement)==va;
                }
                if(match)
                {
                    ++matches;
                    std::printf("match Ghidra=%08llX live=%p bytes=",0x400000ull+start+at,entry.modBaseAddr+start+at);
                    for(size_t i=0;i<std::min<size_t>(pattern.size()+12,read-at);++i) std::printf("%02X ",memory[start+at+i]);
                    std::puts("");
                }
            }
        }
        std::printf("moduleBase=%p matches=%u%s\n",entry.modBaseAddr,matches,
            calls ? " (candidate direct calls; validate instruction boundaries and executable code)" : "");
        CloseHandle(process); return 0;
    }
    std::vector<unsigned char> bytes(length);
    SIZE_T read=0;
    const auto live=liveAddress ? reinterpret_cast<unsigned char*>(va) : entry.modBaseAddr+(va-0x400000);
    const bool ok=ReadProcessMemory(process,live,bytes.data(),length,&read)!=FALSE;
    CloseHandle(process);
    if (!ok || read!=length) return 5;
    std::printf("PID=%lu module=%s moduleBase=%p live=%p %s=%08llX bytes=%lu\n",pid,module.c_str(),entry.modBaseAddr,live,liveAddress ? "LiveAddress" : "AnalysisVA",va,length);
    size_t offset=0;
    while(offset<bytes.size())
    {
        ZydisDisassembledInstruction instruction{};
        if (!ZYAN_SUCCESS(ZydisDisassembleIntel(ZYDIS_MACHINE_MODE_LEGACY_32,
            reinterpret_cast<uintptr_t>(live)+offset,bytes.data()+offset,bytes.size()-offset,&instruction)))
        { std::printf("%08llX decode failed\n",va+offset); return 6; }
        std::printf("%08llX %s\n",va+offset,instruction.text);
        offset+=instruction.info.length;
    }
}
