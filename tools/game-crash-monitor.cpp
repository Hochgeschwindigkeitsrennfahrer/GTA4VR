#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <tlhelp32.h>
#include <dbghelp.h>
#include <cstdio>
#include <cstring>
#pragma comment(lib,"dbghelp.lib")

// x86 development diagnostic: attach only to GTAIV.exe, observe exceptions,
// and detach after a bounded interval. Never alter code, saves, or registers.
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

static void Stack(HANDLE process,DWORD threadId)
{
    HANDLE thread=OpenThread(THREAD_GET_CONTEXT|THREAD_QUERY_INFORMATION,FALSE,threadId);
    CONTEXT context{}; context.ContextFlags=CONTEXT_FULL;
    if(!thread || !GetThreadContext(thread,&context))
    {std::printf("Context unavailable error=%lu\n",GetLastError()); if(thread) CloseHandle(thread); return;}
    std::printf("thread=%lu eip=%08lX esp=%08lX ebp=%08lX eax=%08lX ecx=%08lX edx=%08lX\n",
        threadId,context.Eip,context.Esp,context.Ebp,context.Eax,context.Ecx,context.Edx);
    MEMORY_BASIC_INFORMATION memory{};
    VirtualQueryEx(process,reinterpret_cast<void*>(context.Eip),&memory,sizeof(memory));
    std::printf("instruction region allocation=%p state=%08lX protection=%08lX\n",
        memory.AllocationBase,memory.State,memory.Protect);
    MODULEENTRY32W loaded{sizeof(loaded)};
    HANDLE modules=CreateToolhelp32Snapshot(TH32CS_SNAPMODULE,GetProcessId(process));
    if(Module32FirstW(modules,&loaded)) do
    {
        const auto base=reinterpret_cast<uintptr_t>(loaded.modBaseAddr);
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
    bool snapshotDone=false;
    const auto deadline=GetTickCount64()+(startup ? 120000 : 45000);
    DWORD game=0;
    while(!game && GetTickCount64()<deadline) {game=FindGame(); if(!game) Sleep(20);}
    if(!game) {std::puts("No unique GTAIV process found within the observation limit"); return 1;}
    HANDLE process=OpenProcess(PROCESS_QUERY_INFORMATION|PROCESS_VM_READ,FALSE,game);
    if(!process || !DebugActiveProcess(game))
    {std::printf("Attach failed pid=%lu error=%lu\n",game,GetLastError()); if(process) CloseHandle(process); return 2;}
    DebugSetProcessKillOnExit(FALSE);
    std::printf("Attached pid=%lu; observing for at most %u seconds\n",game,startup ? 180 : 45);
    bool alive=true;
    unsigned firstChanceAV=0;
    const auto attachedDeadline=GetTickCount64()+(startup ? 180000 : 45000);
    while(alive && GetTickCount64()<attachedDeadline)
    {
        DEBUG_EVENT event{};
        if(!WaitForDebugEvent(&event,250)) continue;
        DWORD action=DBG_CONTINUE;
        if(event.dwDebugEventCode==EXCEPTION_DEBUG_EVENT)
        {
            const auto& error=event.u.Exception;
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
            if(error.ExceptionRecord.ExceptionCode==EXCEPTION_ACCESS_VIOLATION) ++firstChanceAV;
            if(!error.dwFirstChance)
            {
                std::printf("Unhandled exception=%08lX address=%p firstChanceAV=%u\n",
                    error.ExceptionRecord.ExceptionCode,error.ExceptionRecord.ExceptionAddress,firstChanceAV);
                Stack(process,event.dwThreadId);
            }
            if(error.ExceptionRecord.ExceptionCode!=EXCEPTION_BREAKPOINT) action=DBG_EXCEPTION_NOT_HANDLED;
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
        if(!DebugActiveProcessStop(game)) {std::printf("Detach failed error=%lu\n",GetLastError()); CloseHandle(process); return 3;}
        std::puts(snapshotDone ? "Detached after thread snapshot; game retained" :
            "Detached at observation limit; game retained");
    }
    CloseHandle(process);
}
