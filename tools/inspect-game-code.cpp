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
// process ID, Ghidra VA (image base 0x400000), and byte count <= 64 KiB.
int main(int argc, char** argv)
{
    const bool scan=argc==4 && !std::strcmp(argv[2],"--pattern");
    if (argc!=4) { std::fprintf(stderr,"Usage: inspect-game-code PID GhidraVA byteCount | PID --pattern \"AA ? BB\"\n"); return 1; }
    const auto pid=static_cast<DWORD>(std::strtoul(argv[1],nullptr,0));
    const auto va=std::strtoull(argv[2],nullptr,0);
    const auto length=std::strtoul(argv[3],nullptr,0);
    if (!scan && (va<0x400000 || !length || length>65536)) return 1;
    const auto snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPMODULE|TH32CS_SNAPMODULE32,pid);
    if (snapshot==INVALID_HANDLE_VALUE) return 2;
    MODULEENTRY32W entry{sizeof(entry)};
    bool found=false;
    if (Module32FirstW(snapshot,&entry)) do {
        if (!_wcsicmp(entry.szModule,L"GTAIV.exe")) { found=true; break; }
    } while(Module32NextW(snapshot,&entry));
    CloseHandle(snapshot);
    if (!found || (!scan && va-0x400000+length>entry.modBaseSize)) return 3;
    const auto process=OpenProcess(PROCESS_VM_READ|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);
    if (!process) return 4;
    if(scan)
    {
        std::vector<int> pattern;
        std::istringstream input(argv[3]); std::string token;
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
                if(match)
                {
                    ++matches;
                    std::printf("match Ghidra=%08llX live=%p bytes=",0x400000ull+start+at,entry.modBaseAddr+start+at);
                    for(size_t i=0;i<std::min<size_t>(pattern.size()+12,read-at);++i) std::printf("%02X ",memory[start+at+i]);
                    std::puts("");
                }
            }
        }
        std::printf("moduleBase=%p matches=%u\n",entry.modBaseAddr,matches);
        CloseHandle(process); return 0;
    }
    std::vector<unsigned char> bytes(length);
    SIZE_T read=0;
    const auto live=entry.modBaseAddr+(va-0x400000);
    const bool ok=ReadProcessMemory(process,live,bytes.data(),length,&read)!=FALSE;
    CloseHandle(process);
    if (!ok || read!=length) return 5;
    std::printf("PID=%lu moduleBase=%p live=%p GhidraVA=%08llX bytes=%lu\n",pid,entry.modBaseAddr,live,va,length);
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
