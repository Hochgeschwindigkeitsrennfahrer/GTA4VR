#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <tlhelp32.h>
#include <dbghelp.h>
#include <cstdio>
#include <cstring>
#include <map>
#pragma comment(lib,"dbghelp.lib")

// x86 development diagnostic: attach only to GTAIV.exe, observe exceptions,
// and detach after a bounded interval. Never alter code or saves.
// Optional --input-watch uses/restores hardware debug registers only.
static std::map<DWORD,CONTEXT> savedDebugRegisters;
static DWORD watchedDevices[4]{};

static bool DiscoverInputDevices(HANDLE process,DWORD game)
{
    HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPMODULE,game);
    MODULEENTRY32W module{sizeof(module)};
    bool found=false;
    if(Module32FirstW(snapshot,&module)) do
        if(!_wcsicmp(module.szModule,L"GTAIV.exe")) {found=true; break;}
    while(Module32NextW(snapshot,&module));
    CloseHandle(snapshot);
    if(!found) return false;
    unsigned char code[16]{}; SIZE_T bytes=0;
    if(!ReadProcessMemory(process,module.modBaseAddr+0x8f7240-0x400000,code,sizeof(code),&bytes) ||
        bytes!=sizeof(code) || code[0]!=0x51 || code[1]!=0xb9 || code[6]!=0xe8) return false;
    DWORD manager=0,devices[5]{};
    std::memcpy(&manager,code+2,4);
    if(!ReadProcessMemory(process,reinterpret_cast<void*>(manager+0x8c),devices,sizeof(devices),&bytes) ||
        bytes!=sizeof(devices) || !devices[4] || devices[4]>4) return false;
    for(unsigned i=0;i<devices[4];++i)
    {
        if(devices[i]<0x10000 || (devices[i]&3)) return false;
        DWORD table=0;
        if(!ReadProcessMemory(process,reinterpret_cast<void*>(devices[i]),&table,4,&bytes) || bytes!=4 || table<0x10000) return false;
    }
    for(unsigned i=0;i<devices[4];++i) {watchedDevices[i]=devices[i]; std::printf("Watching input device[%u]=%08lX\n",i,devices[i]);}
    return true;
}

static void ArmInputThread(DWORD threadId)
{
    if(savedDebugRegisters.count(threadId)) return;
    HANDLE thread=OpenThread(THREAD_GET_CONTEXT|THREAD_SET_CONTEXT,FALSE,threadId);
    CONTEXT context{}; context.ContextFlags=CONTEXT_DEBUG_REGISTERS;
    if(thread && GetThreadContext(thread,&context))
    {
        // Preserve an existing debugger's hardware slots; never replace them.
        if(context.Dr7&0xff) {std::printf("Watch skipped existing debug registers thread=%lu\n",threadId); CloseHandle(thread); return;}
        const auto original=context;
        context.Dr0=watchedDevices[0]; context.Dr1=watchedDevices[1];
        context.Dr2=watchedDevices[2]; context.Dr3=watchedDevices[3];
        context.Dr6=0; context.Dr7=0;
        for(unsigned i=0;i<4;++i) if(watchedDevices[i]) context.Dr7|=(1u<<(2*i)) | (0xdu<<(16+4*i)); // write, four bytes
        if(SetThreadContext(thread,&context)) savedDebugRegisters.emplace(threadId,original);
        else std::printf("Watch arm failed thread=%lu error=%lu\n",threadId,GetLastError());
    }
    if(thread) CloseHandle(thread);
}

static void ArmInputThreads(DWORD game)
{
    HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD,0);
    THREADENTRY32 entry{sizeof(entry)};
    if(Thread32First(snapshot,&entry)) do
        if(entry.th32OwnerProcessID==game) ArmInputThread(entry.th32ThreadID);
    while(Thread32Next(snapshot,&entry));
    CloseHandle(snapshot);
}

static bool RestoreInputThreads()
{
    bool success=true;
    for(const auto& entry:savedDebugRegisters)
    {
        HANDLE thread=OpenThread(THREAD_SET_CONTEXT|THREAD_SUSPEND_RESUME|THREAD_QUERY_INFORMATION,FALSE,entry.first);
        if(!thread) continue; // exited threads have no remaining registers
        DWORD exitCode=0;
        if(GetExitCodeThread(thread,&exitCode) && exitCode==STILL_ACTIVE)
        {
            if(SuspendThread(thread)!=DWORD(-1))
            {
                auto original=entry.second;
                if(!SetThreadContext(thread,&original)) {success=false; std::printf("Watch restore failed thread=%lu error=%lu\n",entry.first,GetLastError());}
                ResumeThread(thread);
            }
            else success=false;
        }
        CloseHandle(thread);
    }
    if(success) savedDebugRegisters.clear();
    return success;
}
static DWORD FindGame()
{
    HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
    PROCESSENTRY32W entry{sizeof(entry)};
    DWORD found=0;
    if(Process32FirstW(snapshot,&entry)) do
        if(!_wcsicmp(entry.szExeFile,L"GTAIV.exe"))
        {if(found) {found=0; break;} found=entry.th32ProcessID;}
    while(Process32NextW(snapshot,&entry));
    CloseHandle(snapshot);
    return found;
}

static bool HasFusionFix(DWORD game)
{
    HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPMODULE,game);
    if(snapshot==INVALID_HANDLE_VALUE) return false;
    MODULEENTRY32W entry{sizeof(entry)};
    bool loaded=false;
    if(Module32FirstW(snapshot,&entry)) do
        if(!_wcsicmp(entry.szModule,L"GTAIV.EFLC.FusionFix.asi")) {loaded=true; break;}
    while(Module32NextW(snapshot,&entry));
    CloseHandle(snapshot);
    return loaded;
}

static void Stack(HANDLE process,DWORD threadId)
{
    HANDLE thread=OpenThread(THREAD_GET_CONTEXT|THREAD_QUERY_INFORMATION,FALSE,threadId);
    CONTEXT context{}; context.ContextFlags=CONTEXT_FULL;
    if(!thread || !GetThreadContext(thread,&context))
    {std::printf("Context unavailable error=%lu\n",GetLastError()); if(thread) CloseHandle(thread); return;}
    std::printf("thread=%lu eip=%08lX esp=%08lX ebp=%08lX eax=%08lX ecx=%08lX edx=%08lX ebx=%08lX esi=%08lX edi=%08lX\n",
        threadId,context.Eip,context.Esp,context.Ebp,context.Eax,context.Ecx,context.Edx,context.Ebx,context.Esi,context.Edi);
    MEMORY_BASIC_INFORMATION memory{};
    VirtualQueryEx(process,reinterpret_cast<void*>(context.Eip),&memory,sizeof(memory));
    std::printf("instruction region allocation=%p state=%08lX protection=%08lX\n",
        memory.AllocationBase,memory.State,memory.Protect);
    MODULEENTRY32W loaded{sizeof(loaded)};
    uintptr_t gameBase=0;
    HANDLE modules=CreateToolhelp32Snapshot(TH32CS_SNAPMODULE,GetProcessId(process));
    if(Module32FirstW(modules,&loaded)) do
    {
        const auto base=reinterpret_cast<uintptr_t>(loaded.modBaseAddr);
        if(!_wcsicmp(loaded.szModule,L"GTAIV.exe"))
        {
            gameBase=base;
            std::printf("game module base=%p size=%lu\n",loaded.modBaseAddr,loaded.modBaseSize);
        }
        if(context.Edx>=base && context.Edx-base<loaded.modBaseSize)
            std::printf("edx module=%ls base=%p size=%lu\n",loaded.szModule,loaded.modBaseAddr,loaded.modBaseSize);
    } while(Module32NextW(modules,&loaded));
    CloseHandle(modules);
    IMAGE_DOS_HEADER dos{}; SIZE_T headerBytes=0;
    if(ReadProcessMemory(process,reinterpret_cast<void*>(context.Edx),&dos,sizeof(dos),&headerBytes) &&
        dos.e_magic==IMAGE_DOS_SIGNATURE && dos.e_lfanew>0 && dos.e_lfanew<0x10000)
    {
        IMAGE_NT_HEADERS32 pe{};
        if(ReadProcessMemory(process,reinterpret_cast<void*>(context.Edx+dos.e_lfanew),&pe,sizeof(pe),&headerBytes) &&
            pe.Signature==IMAGE_NT_SIGNATURE)
            std::printf("edx image preferred=%08lX entryRVA=%08lX size=%08lX\n",
                pe.OptionalHeader.ImageBase,pe.OptionalHeader.AddressOfEntryPoint,pe.OptionalHeader.SizeOfImage);
    }
    DWORD raw[64]{}; SIZE_T bytes=0;
    if(ReadProcessMemory(process,reinterpret_cast<void*>(context.Esp),raw,sizeof(raw),&bytes))
        for(unsigned i=0;i<bytes/sizeof(DWORD);i+=4)
            std::printf("stack+%03X %08lX %08lX %08lX %08lX\n",i*4,raw[i],raw[i+1],raw[i+2],raw[i+3]);
    // CE input virtual call: capture the device even when its corrupted table
    // jumps to a nonzero address, or faults before pushing the return address.
    const bool atInputCall=gameBase && context.Eip==gameBase+0x8f7293-0x400000;
    const bool fromInputCall=gameBase && raw[0]==gameBase+0x8f7296-0x400000;
    const DWORD device=atInputCall ? context.Esi : raw[1];
    if((atInputCall || fromInputCall) && device>=0x10000)
    {
        DWORD object[16]{},slots[32]{};
        MEMORY_BASIC_INFORMATION region{};
        VirtualQueryEx(process,reinterpret_cast<void*>(device),&region,sizeof(region));
        std::printf("input device=%08lX allocation=%p state=%08lX protection=%08lX\n",
            device,region.AllocationBase,region.State,region.Protect);
        if(ReadProcessMemory(process,reinterpret_cast<void*>(device),object,sizeof(object),&bytes))
        {
            for(unsigned i=0;i<16;i+=4)
                std::printf("device+%02X %08lX %08lX %08lX %08lX\n",i*4,object[i],object[i+1],object[i+2],object[i+3]);
            if(object[0]>=0x10000 && ReadProcessMemory(process,reinterpret_cast<void*>(object[0]),slots,sizeof(slots),&bytes))
            {
                std::printf("input vtable=%08lX slot78=%08lX\n",object[0],slots[30]);
                for(unsigned i=0;i<32;i+=4)
                    std::printf("vtable+%02X %08lX %08lX %08lX %08lX\n",i*4,slots[i],slots[i+1],slots[i+2],slots[i+3]);
            }
            else std::printf("Input vtable unavailable error=%lu\n",GetLastError());
        }
    }
    const DWORD originalFrame=context.Ebp,originalStack=context.Esp;
    STACKFRAME64 frame{};
    frame.AddrPC={context.Eip,0,AddrModeFlat};
    frame.AddrStack={context.Esp,0,AddrModeFlat};
    frame.AddrFrame={context.Ebp,0,AddrModeFlat};
    SymSetOptions(SYMOPT_UNDNAME|SYMOPT_LOAD_LINES|SYMOPT_DEFERRED_LOADS);
    const bool symbols=SymInitialize(process,nullptr,TRUE)!=FALSE;
    for(unsigned i=0;i<40 && frame.AddrPC.Offset;++i)
    {
        alignas(SYMBOL_INFO) unsigned char buffer[sizeof(SYMBOL_INFO)+1024]{};
        auto* symbol=reinterpret_cast<SYMBOL_INFO*>(buffer);
        symbol->SizeOfStruct=sizeof(SYMBOL_INFO); symbol->MaxNameLen=1023;
        DWORD64 displacement=0;
        IMAGEHLP_LINE64 line{sizeof(line)}; DWORD lineOffset=0;
        IMAGEHLP_MODULE64 module{sizeof(module)};
        if(symbols) SymGetModuleInfo64(process,frame.AddrPC.Offset,&module);
        std::printf("%02u %08llX %s",i,frame.AddrPC.Offset,module.ModuleName);
        if(symbols && SymFromAddr(process,frame.AddrPC.Offset,&displacement,symbol))
            std::printf("!%s+0x%llX",symbol->Name,displacement);
        if(symbols && SymGetLineFromAddr64(process,frame.AddrPC.Offset,&lineOffset,&line))
            std::printf(" %s:%lu",line.FileName,line.LineNumber);
        std::puts("");
        if(!StackWalk64(IMAGE_FILE_MACHINE_I386,process,thread,&frame,&context,nullptr,
            SymFunctionTableAccess64,SymGetModuleBase64,nullptr)) break;
    }
    if(symbols) SymCleanup(process);
    DWORD link=originalFrame;
    for(unsigned i=0;i<16;++i)
    {
        DWORD pair[2]{};
        if(!ReadProcessMemory(process,reinterpret_cast<void*>(link),pair,sizeof(pair),&bytes)) break;
        std::printf("frame-chain %08lX previous=%08lX return=%08lX\n",link,pair[0],pair[1]);
        if(pair[0]<=link || pair[0]>originalStack+0x100000) break;
        link=pair[0];
    }
    CloseHandle(thread);
}

int main(int argc,char** argv)
{
    const bool snapshotThreads=argc==2 && !std::strcmp(argv[1],"--snapshot");
    const bool startup=argc==2 && !std::strcmp(argv[1],"--startup");
    const bool runtime=argc==2 && !std::strcmp(argv[1],"--runtime");
    const bool inputWatch=argc==2 && !std::strcmp(argv[1],"--input-watch");
    const bool runtimeGate=runtime || inputWatch;
    bool snapshotDone=false;
    const auto deadline=GetTickCount64()+((startup || runtimeGate) ? 120000 : 45000);
    DWORD game=0;
    while(!game && GetTickCount64()<deadline)
    {
        game=FindGame();
        // Startup protection generates large handled exception loops under
        // a debugger. Runtime diagnosis waits until the ASI actually loads.
        if(runtimeGate && game && !HasFusionFix(game)) game=0;
        if(!game) Sleep(runtimeGate ? 100 : 20);
    }
    if(!game) {std::puts("No unique GTAIV process found within the observation limit"); return 1;}
    HANDLE process=OpenProcess(PROCESS_QUERY_INFORMATION|PROCESS_VM_READ,FALSE,game);
    if(!process || !DebugActiveProcess(game))
    {std::printf("Attach failed pid=%lu error=%lu\n",game,GetLastError()); if(process) CloseHandle(process); return 2;}
    DebugSetProcessKillOnExit(FALSE);
    std::printf("Attached pid=%lu; observing for at most %u seconds\n",game,(startup || runtimeGate) ? 180 : 45);
    bool alive=true;
    bool watchReady=false;
    bool watchFinished=false;
    unsigned watchHits=0;
    unsigned firstChanceAV=0;
    const auto attachedDeadline=GetTickCount64()+((startup || runtimeGate) ? 180000 : 45000);
    while(alive && GetTickCount64()<attachedDeadline)
    {
        DEBUG_EVENT event{};
        if(!WaitForDebugEvent(&event,250)) continue;
        DWORD action=DBG_CONTINUE;
        // All debuggee threads are stopped while a debug event is handled.
        if(inputWatch && !watchReady && !watchFinished && DiscoverInputDevices(process,game))
        {watchReady=true; ArmInputThreads(game);}
        if(watchReady && event.dwDebugEventCode==CREATE_THREAD_DEBUG_EVENT) ArmInputThread(event.dwThreadId);
        if(event.dwDebugEventCode==EXCEPTION_DEBUG_EVENT)
        {
            const auto& error=event.u.Exception;
            if(error.ExceptionRecord.ExceptionCode==EXCEPTION_SINGLE_STEP) action=DBG_EXCEPTION_NOT_HANDLED;
            if(watchReady && error.ExceptionRecord.ExceptionCode==EXCEPTION_SINGLE_STEP)
            {
                HANDLE thread=OpenThread(THREAD_GET_CONTEXT,FALSE,event.dwThreadId);
                CONTEXT debug{}; debug.ContextFlags=CONTEXT_DEBUG_REGISTERS;
                const bool hit=thread && GetThreadContext(thread,&debug) && (debug.Dr6&0xf);
                if(thread) CloseHandle(thread);
                if(hit)
                {
                    ++watchHits;
                    watchFinished=true;
                    std::printf("Input write watch hit=%u thread=%lu slots=%lX\n",watchHits,event.dwThreadId,debug.Dr6&0xf);
                    if(watchHits<=8) Stack(process,event.dwThreadId);
                    // Restore immediately after the first write; leave native
                    // execution and exception handling unchanged thereafter.
                    if(RestoreInputThreads()) watchReady=false;
                    action=DBG_CONTINUE;
                }
                else action=DBG_EXCEPTION_NOT_HANDLED;
            }
            if(snapshotThreads && !snapshotDone && error.ExceptionRecord.ExceptionCode==EXCEPTION_BREAKPOINT)
            {
                // Debug attach stops the target's threads. Read their contexts
                // before continuing, without changing registers or code.
                HANDLE threads=CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD,0);
                THREADENTRY32 entry{sizeof(entry)};
                unsigned count=0;
                if(Thread32First(threads,&entry)) do
                    if(entry.th32OwnerProcessID==game && count++<64) Stack(process,entry.th32ThreadID);
                while(Thread32Next(threads,&entry));
                CloseHandle(threads);
                snapshotDone=true;
            }
            if(error.ExceptionRecord.ExceptionCode==EXCEPTION_ACCESS_VIOLATION)
            {
                ++firstChanceAV;
                // Failure reporting can itself fault in dxdiagn/COM. Capture
                // the original bounded first-chance sites without suppressing
                // any exception or changing native handling.
                if(error.dwFirstChance && firstChanceAV<=8)
                {
                    std::printf("First-chance AV=%u address=%p\n",firstChanceAV,error.ExceptionRecord.ExceptionAddress);
                    Stack(process,event.dwThreadId);
                }
            }
            if(!error.dwFirstChance)
            {
                std::printf("Unhandled exception=%08lX address=%p firstChanceAV=%u\n",
                    error.ExceptionRecord.ExceptionCode,error.ExceptionRecord.ExceptionAddress,firstChanceAV);
                Stack(process,event.dwThreadId);
            }
            if(error.ExceptionRecord.ExceptionCode!=EXCEPTION_BREAKPOINT &&
                !(inputWatch && error.ExceptionRecord.ExceptionCode==EXCEPTION_SINGLE_STEP)) action=DBG_EXCEPTION_NOT_HANDLED;
        }
        else if(event.dwDebugEventCode==CREATE_PROCESS_DEBUG_EVENT)
        {
            if(event.u.CreateProcessInfo.hFile) CloseHandle(event.u.CreateProcessInfo.hFile);
            CloseHandle(event.u.CreateProcessInfo.hProcess); CloseHandle(event.u.CreateProcessInfo.hThread);
        }
        else if(event.dwDebugEventCode==CREATE_THREAD_DEBUG_EVENT) CloseHandle(event.u.CreateThread.hThread);
        else if(event.dwDebugEventCode==LOAD_DLL_DEBUG_EVENT && event.u.LoadDll.hFile)
        {
            wchar_t path[MAX_PATH]{};
            GetFinalPathNameByHandleW(event.u.LoadDll.hFile,path,MAX_PATH,0);
            if(wcsstr(path,L"FusionFix.asi") || wcsstr(path,L"dinput8.dll"))
                std::printf("loaded base=%p %ls\n",event.u.LoadDll.lpBaseOfDll,path);
            CloseHandle(event.u.LoadDll.hFile);
        }
        else if(event.dwDebugEventCode==EXIT_PROCESS_DEBUG_EVENT)
        {std::printf("Process exited code=%08lX firstChanceAV=%u\n",event.u.ExitProcess.dwExitCode,firstChanceAV); alive=false;}
        ContinueDebugEvent(event.dwProcessId,event.dwThreadId,action);
        std::fflush(stdout);
        if(snapshotDone) break;
    }
    if(alive)
    {
        if(!savedDebugRegisters.empty() && !RestoreInputThreads())
            std::puts("Hardware watch restoration incomplete; do not use this run for acceptance");
        if(!DebugActiveProcessStop(game)) {std::printf("Detach failed error=%lu\n",GetLastError()); CloseHandle(process); return 3;}
        std::puts(snapshotDone ? "Detached after thread snapshot; game retained" :
            "Detached at observation limit; game retained");
    }
    CloseHandle(process);
}
