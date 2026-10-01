# Opt-in OpenXR lifecycle (issue 005)

This is a session and frame-loop diagnostic. It does **not** render a headset image or alter the game camera. GTA IV remains flat-screen. `[VR] EnableOpenXR=0` in `plugins/GTAIV.EFLC.FusionFix.ini` is the default; when disabled, the ASI does not load or call OpenXR.

## Build and install

The repository pins [Khronos OpenXR-SDK-Source](https://github.com/KhronosGroup/OpenXR-SDK-Source) as an `external/openxr-sdk` submodule at `f2448a8797c85814aa892efc1ab8707900fbcc78` (SDK 1.1.63). After `git submodule update --init --recursive`, build its **Win32 DLL loader** using VS 2026/CMake:

```powershell
cmake -S external/openxr-sdk -B build/openxr-loader-win32 -G "Visual Studio 18 2026" -A Win32 -DDYNAMIC_LOADER=ON -DBUILD_TESTS=OFF
cmake --build build/openxr-loader-win32 --config Debug --target openxr_loader
cmake --build build/openxr-loader-win32 --config Release --target openxr_loader
.\premake5.bat
& 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe' build\GTAIV.EFLC.FusionFix.vcxproj /p:Configuration=Debug /p:Platform=Win32
```

Use a VS 2026 Developer Prompt if `cmake` is not on `PATH`. Debug testing needs `build/openxr-loader-win32/src/loader/Debug/openxr_loaderd.dll` installed as `GTAIV/plugins/openxr_loader.dll` alongside the Debug ASI. Release packaging copies the Release x86 `openxr_loader.dll` and the SDK's Apache-2.0 license to `data/plugins/`. The loader is **optional at game startup**: `source/openxrvr.ixx` loads it by absolute plugin path only if VR is enabled. A missing loader is logged and the game continues flat-screen. SteamVR's runtime DLL is supplied by SteamVR and is never copied into this project.

## Ownership and behavior

- `OpenXRVR::Install` reads the setting from the ASI's INI and registers callbacks only when enabled. It writes a diagnostic `GTAIV/plugins/GTAIV.VR.openxr.log` and loads the x86 loader explicitly.
- The module owns the loader, instance, HMD system ID, D3D11 device/context on the runtime's adapter LUID, and D3D11 graphics session. It requests OpenXR API 1.0 because the installed SteamVR x86 runtime rejected 1.1. All creation failures release partial resources and disable OpenXR for that game process; flat-screen rendering continues.
- On each FusionFix EndScene event it polls OpenXR events, begins the session at `READY`, ends it at `STOPPING`, and handles loss/exit states. While running, it calls `xrWaitFrame`, `xrBeginFrame`, and `xrEndFrame` with **zero layers**. It logs the first frame and every 600th frame. A SteamVR session may set `shouldRender=0` when the headset is idle; frame calls still run while the session is active.
- Before a D3D9 reset, it destroys the session, D3D11 objects, and instance. The next EndScene recreates them. Runtime loss also destroys resources and falls back to flat-screen. The [development bridge](development-command-bridge.md) performs explicit OpenXR shutdown before requesting the game's normal window-close path. Process detach closes the log handle but avoids OpenXR calls under the Windows loader lock. Frame calls are currently driven by the desktop EndScene cadence; frame pacing, headset submission, and render-thread ownership need further work before a headset image (#006).

## Tested on 2026-10-01

GTA IV Complete Edition 1.2.0.59, borderless system D3D9, SteamVR/OpenXR 2.17.10, `SteamVR/OpenXR : lighthouse`, NVIDIA GeForce RTX 4070 Ti. A Debug x86 ASI and x86 loader DLL reached `xrCreateSession=0`, `READY`, `xrBeginSession=0`, `FOCUSED`, and thousands of zero-layer frames while the flat-screen game remained responsive. The game performed an early D3D9 reset; the module destroyed and recreated the session successfully. With the loader absent, the enabled game reached a responsive process and logged an explicit fallback. The user confirmed that free roam, walking, driving, pause/resume, and alt-tab worked with the enabled diagnostic build. This does not establish headset visuals or performance.

Do not ship the Debug loader DLL. The Release DLL is checked as x86 (`14C machine`) and is packaged with its license by `release.bat`. Preserve the game's installed INI, ASI, and loader before local tests; the repository does not track those binaries or logs.

### Shutdown evidence

A temporary Debug-only EndScene hook called `ExitProcess` after a marker appeared. The process stopped advancing and remained in Windows' process list in an exit state until reboot. The temporary hook was removed; the cause was not established from the missing shutdown log.

After reboot, the named-pipe `quit` command explicitly destroyed the live OpenXR session and instance (both returned success), logged `OpenXR explicit shutdown complete`, and posted `WM_CLOSE` to the game-owned window. GTAIV.exe exited within the eight-second observation window. Subsequent launches succeeded. An integrated invalid-runtime-manifest test returned `XR_ERROR_RUNTIME_UNAVAILABLE` (`-51`), fell back to a responsive flat-screen game, and also exited through RPC. A separate VR-off run reported zero OpenXR frames and loaded neither `openxr_loader.dll` nor SteamVR's `vrclient.dll`. These observations verify the command-driven exit path; an abrupt external process termination does not run that cleanup.

### Final fallback and gameplay checks

The final x86 Debug and Release builds returned exit code 0. In the Release game, terminating SteamVR's server/compositor after 1,218 frames produced STOPPING → IDLE → EXITING. `xrEndSession`, `xrDestroySession`, and `xrDestroyInstance` all returned `XR_SUCCESS`. GTA IV remained responsive; the bridge reported `running=false`, `failed=true` and cleanly quit the game within eight seconds. SteamVR was restarted after the test. Runtime recovery currently requires relaunching the game.

The user confirmed the full VR-disabled walking/driving/pause/alt-tab route. Severe intermittent stutter when leaving the apartment cleared afterward; no measurements or attribution were made. The final installed Release files match the build hashes; local settings retain the development bridge with VR off. Ordinary menu quit, stereo images, latency, and TLAD/TBoGT remain unverified.
