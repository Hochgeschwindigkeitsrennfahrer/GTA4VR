module;

#include <common.hxx>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <intrin.h>

export module vrtrace;

import common;
import comvars;

export namespace VRTrace
{
    void Install();
    void Event(const char* name, const void* object = nullptr, const void* parent = nullptr);
}

namespace
{
    HANDLE traceFile = INVALID_HANDLE_VALUE;
    SRWLOCK traceLock = SRWLOCK_INIT;
    unsigned traceEvents = 0;
    unsigned traceFrame = 0;
    constexpr unsigned maxTraceEvents = 30000;
    SafetyHookInline shSceneSetup{};
    SafetyHookInline shDrawSetup{};
    SafetyHookInline shViewportExecute{};
    SafetyHookInline shViewportConstruct{};
    SafetyHookInline shVisibility{};
    SafetyHookInline shRenderFrame{};
    SafetyHookInline shBuildFrame{};
    volatile LONG visibilityCalls = 0;
    volatile LONG duplicateBuildAttempted = 0;
    bool duplicateBuild = false;
    bool duplicatePhase = false;
    volatile LONG eyePass = 0;
    void* worldParent = nullptr;
    void* radarParent = nullptr;
    void* scenePhase = nullptr;
    void* lightingPhase = nullptr;
    void* drawPhase = nullptr;
    bool sceneSetupThisFrame = false;
    void* sceneCommand1 = nullptr;
    void* sceneCommand2 = nullptr;
    void* drawCommand1 = nullptr;
    void* drawCommand2 = nullptr;
    volatile LONG sceneCommand1Seen = 0;
    volatile LONG sceneCommand2Seen = 0;
    volatile LONG drawCommand1Seen = 0;
    volatile LONG drawCommand2Seen = 0;

    void LogCamera(const char* label, const rage::grcViewport* viewport);

    void CloseTrace()
    {
        AcquireSRWLockExclusive(&traceLock);
        if (traceFile != INVALID_HANDLE_VALUE)
        {
            CloseHandle(traceFile);
            traceFile = INVALID_HANDLE_VALUE;
        }
        ReleaseSRWLockExclusive(&traceLock);
    }

    void __fastcall SceneSetup(void* self, void* edx, void* parent)
    {
        worldParent = parent;
        scenePhase = self;
        sceneSetupThisFrame = true;
        VRTrace::Event("scene_setup_begin", self, parent);
        VRTrace::Event("scene_setup_caller", self, _ReturnAddress());
        shSceneSetup.unsafe_fastcall(self, edx, parent);
        VRTrace::Event("scene_setup_end", self, parent);
    }

    void __fastcall DrawSetup(void* self, void* edx, void* parent)
    {
        if (sceneSetupThisFrame && parent == worldParent)
        {
            if (!lightingPhase)
                lightingPhase = self;
            else if (!drawPhase && self != lightingPhase)
                drawPhase = self;
        }
        if (worldParent && parent != worldParent)
            radarParent = parent;
        VRTrace::Event("draw_setup_begin", self, parent);
        VRTrace::Event("draw_setup_caller", self, _ReturnAddress());
        shDrawSetup.unsafe_fastcall(self, edx, parent);
        VRTrace::Event("draw_setup_end", self, parent);
    }

    void __fastcall ViewportExecute(void* self, void* edx)
    {
        if (self == sceneCommand1 &&
            InterlockedCompareExchange(&sceneCommand1Seen, 1, 0) == 0)
        {
            VRTrace::Event("eye1_command_execute", self);
            LogCamera("eye1_executed_camera",
                reinterpret_cast<rage::grcViewport*>(static_cast<char*>(self) + 0x10));
        }
        if (self == sceneCommand2 &&
            InterlockedCompareExchange(&sceneCommand2Seen, 1, 0) == 0)
        {
            VRTrace::Event("eye2_command_execute", self);
            LogCamera("eye2_executed_camera",
                reinterpret_cast<rage::grcViewport*>(static_cast<char*>(self) + 0x10));
        }
        if (self == drawCommand1 &&
            InterlockedCompareExchange(&drawCommand1Seen, 1, 0) == 0)
        {
            VRTrace::Event("eye1_draw_execute", self);
            LogCamera("eye1_draw_executed_camera",
                reinterpret_cast<rage::grcViewport*>(static_cast<char*>(self) + 0x10));
        }
        if (self == drawCommand2 &&
            InterlockedCompareExchange(&drawCommand2Seen, 1, 0) == 0)
        {
            VRTrace::Event("eye2_draw_execute", self);
            LogCamera("eye2_draw_executed_camera",
                reinterpret_cast<rage::grcViewport*>(static_cast<char*>(self) + 0x10));
        }
        VRTrace::Event("viewport_execute_begin", self);
        shViewportExecute.unsafe_fastcall(self, edx);
        VRTrace::Event("viewport_execute_end", self);
    }

    void* __fastcall ViewportConstruct(void* self, void* edx, void* sourceViewport)
    {
        auto result = shViewportConstruct.unsafe_fastcall<void*>(self, edx, sourceViewport);
        VRTrace::Event("viewport_construct", self, sourceViewport);
        if (duplicatePhase && traceFrame == 30 && scenePhase &&
            sourceViewport == static_cast<char*>(scenePhase) + 0xB0)
        {
            const LONG pass = InterlockedCompareExchange(&eyePass, 0, 0);
            if (pass == 1)
            {
                sceneCommand1 = self;
                VRTrace::Event("eye1_command_construct", self, sourceViewport);
                LogCamera("eye1_command_camera", reinterpret_cast<rage::grcViewport*>(sourceViewport));
            }
            else if (pass == 2)
            {
                sceneCommand2 = self;
                VRTrace::Event("eye2_command_construct", self, sourceViewport);
                LogCamera("eye2_command_camera", reinterpret_cast<rage::grcViewport*>(sourceViewport));
            }
        }
        if (duplicatePhase && traceFrame == 30 && drawPhase &&
            sourceViewport == static_cast<char*>(drawPhase) + 0xB0)
        {
            const LONG pass = InterlockedCompareExchange(&eyePass, 0, 0);
            if (pass == 1 && !drawCommand1)
                drawCommand1 = self;
            else if (pass == 2 && !drawCommand2)
                drawCommand2 = self;
            VRTrace::Event(pass == 2 ? "eye2_draw_command" : "eye1_draw_command", self, sourceViewport);
            LogCamera(pass == 2 ? "eye2_draw_camera" : "eye1_draw_camera",
                reinterpret_cast<rage::grcViewport*>(sourceViewport));
        }
        return result;
    }

    bool __cdecl Visibility(void* entity, void* phase, float* planes)
    {
        const LONG call = InterlockedIncrement(&visibilityCalls);
        if ((call & 0x3FF) == 0)
            VRTrace::Event("visibility_sample", entity, phase);
        const bool result = shVisibility.unsafe_ccall<bool>(entity, phase, planes);
        if (duplicatePhase && traceFrame == 30 && phase == scenePhase)
        {
            const LONG pass = InterlockedCompareExchange(&eyePass, 0, 0);
            if (pass == 1)
                VRTrace::Event(result ? "eye1_cull_true" : "eye1_cull_false", entity, phase);
            else if (pass == 2)
                VRTrace::Event(result ? "eye2_cull_true" : "eye2_cull_false", entity, phase);
        }
        return result;
    }

    void LogCamera(const char* label, const rage::grcViewport* viewport)
    {
        if (!viewport)
            return;
        char name[128];
        const int length = sprintf_s(name, "%s_%.2f_%.2f_%.2f_fov_%.2f",
            label, viewport->mCameraMatrix[3][0], viewport->mCameraMatrix[3][1],
            viewport->mCameraMatrix[3][2], viewport->mFov);
        if (length > 0)
            VRTrace::Event(name, viewport);
    }

    void __fastcall RenderFrame(void* self, void* edx)
    {
        sceneSetupThisFrame = false;
        lightingPhase = nullptr;
        drawPhase = nullptr;
        if (duplicatePhase && traceFrame == 30)
            InterlockedExchange(&eyePass, 1);
        VRTrace::Event("render_frame_begin", self);
        if (traceFrame == 30)
            VRTrace::Event("render_frame_caller", _ReturnAddress());
        shRenderFrame.unsafe_fastcall(self, edx);
        VRTrace::Event("render_frame_end", self);
    }

    void __fastcall BuildFrame(void* self, void* edx)
    {
        VRTrace::Event("build_frame_begin", self);
        if (traceFrame == 30)
            VRTrace::Event("build_frame_caller", _ReturnAddress());
        shBuildFrame.unsafe_fastcall(self, edx);
        VRTrace::Event("build_frame_end", self);
        if (duplicateBuild && traceFrame == 30 &&
            InterlockedCompareExchange(&duplicateBuildAttempted, 1, 0) == 0)
        {
            VRTrace::Event("build_frame_second_begin", self);
            shBuildFrame.unsafe_fastcall(self, edx);
            VRTrace::Event("build_frame_second_end", self);
        }
        else if (duplicatePhase && traceFrame == 30 && scenePhase && lightingPhase &&
            drawPhase && radarParent &&
            InterlockedCompareExchange(&duplicateBuildAttempted, 1, 0) == 0)
        {
            void* phases[] = {scenePhase, lightingPhase, drawPhase};
            auto* secondViewport = reinterpret_cast<rage::grcViewport*>(
                static_cast<char*>(radarParent) + 0x10);
            rage::grcViewport saved[3][2]{};
            LogCamera("eye1_camera", reinterpret_cast<rage::grcViewport*>(
                static_cast<char*>(scenePhase) + 0xB0));
            LogCamera("eye2_camera", secondViewport);
            for (int i = 0; i < 3; ++i)
            {
                auto* primary = reinterpret_cast<rage::grcViewport*>(
                    static_cast<char*>(phases[i]) + 0xB0);
                auto* secondary = reinterpret_cast<rage::grcViewport*>(
                    static_cast<char*>(phases[i]) + 0x4A0);
                std::memcpy(&saved[i][0], primary, sizeof(saved[i][0]));
                std::memcpy(&saved[i][1], secondary, sizeof(saved[i][1]));
                std::memcpy(primary, secondViewport, sizeof(saved[i][0]));
                std::memcpy(secondary, secondViewport, sizeof(saved[i][1]));
            }
            InterlockedExchange(&eyePass, 2);
            VRTrace::Event("build_frame_second_begin", self);
            shBuildFrame.unsafe_fastcall(self, edx);
            VRTrace::Event("build_frame_second_end", self);
            for (int i = 0; i < 3; ++i)
            {
                std::memcpy(static_cast<char*>(phases[i]) + 0xB0,
                    &saved[i][0], sizeof(saved[i][0]));
                std::memcpy(static_cast<char*>(phases[i]) + 0x4A0,
                    &saved[i][1], sizeof(saved[i][1]));
            }
            InterlockedExchange(&eyePass, 0);
        }
    }
}

void VRTrace::Event(const char* name, const void* object, const void* parent)
{
    AcquireSRWLockExclusive(&traceLock);
    if (traceFile == INVALID_HANDLE_VALUE)
    {
        ReleaseSRWLockExclusive(&traceLock);
        return;
    }

    if (std::strcmp(name, "game_process") == 0)
        ++traceFrame;
    const auto sequence = ++traceEvents;
    if (sequence > maxTraceEvents)
    {
        ReleaseSRWLockExclusive(&traceLock);
        return;
    }
    char line[160];
    const int length = sprintf_s(line, "%u,%llu,%lu,%u,%s,%p,%p\r\n",
        sequence, GetTickCount64(), GetCurrentThreadId(), traceFrame, name, object, parent);
    if (length > 0)
    {
        DWORD written = 0;
        WriteFile(traceFile, line, static_cast<DWORD>(length), &written, nullptr);
    }
    ReleaseSRWLockExclusive(&traceLock);
}

void VRTrace::Install()
{
    wchar_t path[MAX_PATH]{};
    if (!GetModuleFileNameW(nullptr, path, MAX_PATH))
        return;
    wchar_t* fileName = wcsrchr(path, L'\\');
    if (!fileName)
        return;
    fileName[1] = L'\0';
    if (wcscat_s(path, L"plugins\\GTAIV.VR.trace.enable") != 0 ||
        GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES)
        return;

    fileName = wcsrchr(path, L'\\');
    if (!fileName)
        return;
    wcscpy_s(fileName + 1, MAX_PATH - (fileName + 1 - path), L"GTAIV.VR.duplicate-build.enable");
    duplicateBuild = GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;
    wcscpy_s(fileName + 1, MAX_PATH - (fileName + 1 - path), L"GTAIV.VR.duplicate-phase.enable");
    duplicatePhase = GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;
    wcscpy_s(fileName + 1, MAX_PATH - (fileName + 1 - path), L"GTAIV.VR.trace.csv");
    traceFile = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (traceFile == INVALID_HANDLE_VALUE)
        return;
    constexpr char header[] = "sequence,tick_ms,thread,frame,event,object,parent\r\n";
    DWORD written = 0;
    WriteFile(traceFile, header, sizeof(header) - 1, &written, nullptr);

    FusionFix::onGameInitEvent() += []() { Event("game_init"); };
    FusionFix::onGameProcessEvent() += []() { Event("game_process"); };
    FusionFix::onMenuDrawingEvent() += []() { Event("menu_drawing"); };
    FusionFix::onBeforeReset() += []() { Event("before_reset"); };
    FusionFix::onEndScene() += []() { Event("end_scene"); };
    CRenderPhaseDeferredLighting_SceneToGBuffer::OnBuildRenderList() += []() { Event("scene_to_gbuffer_list"); };
    CRenderPhaseDeferredLighting_LightsToScreen::OnBuildRenderList() += []() { Event("lights_to_screen_list"); };
    CRenderPhaseDrawScene::OnBuildRenderList() += []() { Event("draw_scene_list"); };
    auto scenePattern = hook::pattern("53 8B 5C 24 08 56 57 8B F9 8D 73 10 56 8D 8F B0 00 00 00");
    if (!scenePattern.empty())
        shSceneSetup = safetyhook::create_inline(scenePattern.get_first(0), SceneSetup);
    else
        Event("scene_setup_pattern_missing");

    auto drawPattern = hook::pattern("53 8B 5C 24 08 55 8B 83 38 05 00 00 56 57 8B F9 8D 73 10 56");
    if (!drawPattern.empty())
        shDrawSetup = safetyhook::create_inline(drawPattern.get_first(0), DrawSetup);
    else
        Event("draw_setup_pattern_missing");

    auto viewportExecutePattern = hook::pattern("80 B9 00 04 00 00 00 6A 01 74 0B 6A 00");
    if (!viewportExecutePattern.empty())
        shViewportExecute = safetyhook::create_inline(viewportExecutePattern.get_first(0), ViewportExecute);
    else
        Event("viewport_execute_pattern_missing");

    auto viewportConstructPattern = hook::pattern("56 8B F1 57 8B 46 04 C7 06 ? ? ? ? 33 05 ? ? ? ? 8D 4E 10 25 FF 3F 00 00");
    if (!viewportConstructPattern.empty())
        shViewportConstruct = safetyhook::create_inline(viewportConstructPattern.get_first(0), ViewportConstruct);
    else
        Event("viewport_construct_pattern_missing");

    auto visibilityPattern = hook::pattern("55 8B EC 83 E4 F0 83 EC 48 56 8B 75 0C 57 83 7E 3C 01");
    if (!visibilityPattern.empty())
        shVisibility = safetyhook::create_inline(visibilityPattern.get_first(0), Visibility);
    else
        Event("visibility_pattern_missing");

    auto renderFramePattern = hook::pattern("55 8B EC 83 E4 F8 83 EC 20 53 55 56 57 8B E9 E8 ? ? ? ? 8D 4D 38");
    if (!renderFramePattern.empty())
        shRenderFrame = safetyhook::create_inline(renderFramePattern.get_first(0), RenderFrame);
    else
        Event("render_frame_pattern_missing");

    auto buildFramePattern = hook::pattern("56 57 8B F9 E8 ? ? ? ? 6A 01 6A 10 E8 ? ? ? ? 83 C4 08 85 C0");
    if (!buildFramePattern.empty())
        shBuildFrame = safetyhook::create_inline(buildFramePattern.get_first(0), BuildFrame);
    else
        Event("build_frame_pattern_missing");
    FusionFix::onShutdownEvent() += []() { CloseTrace(); };
}
