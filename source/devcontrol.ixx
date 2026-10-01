module;

#include <common.hxx>
#include <sddl.h>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <string>
#include <vector>

export module devcontrol;

import common;
import comvars;
import openxrvr;
import vrstereo;

export namespace DevControl { void Install(); }

namespace
{
    HANDLE pipe = INVALID_HANDLE_VALUE;
    HANDLE ioEvent = nullptr;
    OVERLAPPED io{};
    enum class Stage { Connecting, Command, Response, Acknowledge };
    Stage stage = Stage::Connecting;
    bool complete = false;
    DWORD transferred = 0;
    DWORD immediateError = 0;
    ULONGLONG operationStarted = 0;
    char input[128]{};
    std::string response;
    HWND quitWindow = nullptr;

    void Prepare(Stage next)
    {
        stage = next;
        complete = false;
        transferred = 0;
        immediateError = 0;
        operationStarted = GetTickCount64();
        ResetEvent(ioEvent);
        io = {};
        io.hEvent = ioEvent;
    }

    void Connect()
    {
        quitWindow = nullptr;
        DisconnectNamedPipe(pipe);
        Prepare(Stage::Connecting);
        complete = ConnectNamedPipe(pipe, &io) != FALSE;
        if (!complete)
        {
            const auto error = GetLastError();
            if (error == ERROR_PIPE_CONNECTED) complete = true;
            else if (error != ERROR_IO_PENDING) immediateError = error;
        }
    }

    void Read(Stage next)
    {
        Prepare(next);
        complete = ReadFile(pipe, input, sizeof(input), &transferred, &io) != FALSE;
        if (!complete && GetLastError() != ERROR_IO_PENDING) immediateError = GetLastError();
    }

    HWND GameWindow()
    {
        HWND window = gWnd;
        if (!IsWindow(window) && RageDirect3DDevice9::m_pRealDevice &&
            *RageDirect3DDevice9::m_pRealDevice)
        {
            D3DDEVICE_CREATION_PARAMETERS parameters{};
            if (SUCCEEDED((*RageDirect3DDevice9::m_pRealDevice)->GetCreationParameters(&parameters)))
                window = parameters.hFocusWindow;
        }
        DWORD process = 0;
        GetWindowThreadProcessId(window, &process);
        return IsWindow(window) && process == GetCurrentProcessId() ? window : nullptr;
    }

    void ExecuteCommand()
    {
        std::string command(input, transferred);
        while (!command.empty() && (command.back() == '\r' || command.back() == '\n'))
            command.pop_back();
        if (command == "status")
        {
            response = "{\"ok\":true,\"protocol\":1,\"pid\":" +
                std::to_string(GetCurrentProcessId()) + ",\"openxr\":" +
                OpenXRVR::Status() + ",\"stereo\":" + VRStereo::Status() + "}";
        }
        else if(command=="stereo-test")
        {
            response=VRStereo::RestartExperiment() ? "{\"ok\":true,\"command\":\"stereo-test\",\"accepted\":true}" :
                "{\"ok\":false,\"error\":\"experimental stereo not ready\"}";
        }
        else if (command == "quit")
        {
            quitWindow = GameWindow();
            response = quitWindow ? "{\"ok\":true,\"command\":\"quit\",\"accepted\":true}" :
                "{\"ok\":false,\"error\":\"game window unavailable\"}";
        }
        else response = "{\"ok\":false,\"error\":\"unsupported command\"}";
        Prepare(Stage::Response);
        complete = WriteFile(pipe, response.data(), static_cast<DWORD>(response.size()),
            &transferred, &io) != FALSE;
        if (!complete && GetLastError() != ERROR_IO_PENDING) immediateError = GetLastError();
    }

    void Pump()
    {
        if (pipe == INVALID_HANDLE_VALUE) return;
        if (immediateError) { Connect(); return; }
        if (!complete)
        {
            if (!GetOverlappedResult(pipe, &io, &transferred, FALSE))
            {
                if (GetLastError() == ERROR_IO_INCOMPLETE)
                {
                    if (stage != Stage::Connecting && GetTickCount64() - operationStarted > 5000)
                        CancelIoEx(pipe, &io);
                    return;
                }
                Connect();
                return;
            }
        }
        switch (stage)
        {
        case Stage::Connecting: Read(Stage::Command); break;
        case Stage::Command: ExecuteCommand(); break;
        case Stage::Response: Read(Stage::Acknowledge); break;
        case Stage::Acknowledge:
            if (transferred == 3 && std::memcmp(input, "ack", 3) == 0 && quitWindow)
            {
                OpenXRVR::Shutdown();
                VRStereo::Shutdown();
                PostMessageW(quitWindow, WM_CLOSE, 0, 0);
            }
            Connect();
            break;
        }
    }

    PSECURITY_DESCRIPTOR CurrentUserSecurity()
    {
        HANDLE token = nullptr;
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return nullptr;
        DWORD bytes = 0;
        GetTokenInformation(token, TokenUser, nullptr, 0, &bytes);
        std::vector<unsigned char> data(bytes);
        const BOOL gotUser = GetTokenInformation(token, TokenUser, data.data(), bytes, &bytes);
        CloseHandle(token);
        if (!gotUser) return nullptr;
        LPWSTR sid = nullptr;
        if (!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(data.data())->User.Sid, &sid))
            return nullptr;
        const std::wstring sddl = std::wstring(L"D:P(A;;GA;;;SY)(A;;GA;;;") + sid + L")";
        LocalFree(sid);
        PSECURITY_DESCRIPTOR descriptor = nullptr;
        if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(),
            SDDL_REVISION_1, &descriptor, nullptr)) return nullptr;
        return descriptor;
    }
}

void DevControl::Install()
{
    CIniReader ini("");
    if (ini.ReadInteger("DEVELOPMENT", "EnableCommandBridge", 0) == 0) return;
    auto descriptor = CurrentUserSecurity();
    if (!descriptor) return; // Do not fall back to a broader ACL.
    SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), descriptor, FALSE};
    wchar_t name[128];
    swprintf_s(name, L"\\\\.\\pipe\\GTA4VR.Dev.%lu", GetCurrentProcessId());
    pipe = CreateNamedPipeW(name, PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED |
        FILE_FLAG_FIRST_PIPE_INSTANCE, PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE |
        PIPE_REJECT_REMOTE_CLIENTS, 1, 512, 128, 0, &security);
    LocalFree(descriptor);
    if (pipe == INVALID_HANDLE_VALUE) return;
    ioEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!ioEvent) { CloseHandle(pipe); pipe = INVALID_HANDLE_VALUE; return; }
    Connect();
    FusionFix::onEndScene() += []() { Pump(); };
    FusionFix::onShutdownEvent() += []()
    {
        if (pipe != INVALID_HANDLE_VALUE) { CancelIoEx(pipe, nullptr); CloseHandle(pipe); }
        if (ioEvent) CloseHandle(ioEvent);
    };
}
