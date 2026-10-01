# 005 — Add opt-in OpenXR initialization

## Goal

Introduce an isolated, disabled-by-default OpenXR lifecycle before stereo rendering. Roadmap stage: 1. Depends on #001 and the runtime/bridge findings in #004.

## Tasks

- [x] Add a VR enable setting whose default is off, with clear diagnostics for missing loader/runtime and unsupported graphics binding.
- [x] Create and destroy the OpenXR instance and session at defined lifecycle points; handle runtime loss and game/device reset without leaks or crashes. Reset recreation, controlled runtime loss, and command-driven exit passed.
- [x] Prototype a frame loop and verify session states, frame timing, and clean shutdown. Zero-layer frames and state changes passed; the development bridge releases OpenXR before requesting the game's window-close path.
- [x] Re-run the #002 flat-screen route with VR disabled and record runtime/headset versions for enabled testing. The user confirmed walking, driving, pause/resume, and alt-tab with VR off and with VR enabled.

## Done when

Flat-screen mode still works with VR disabled; enabling VR reaches a recorded OpenXR session/frame-loop state on a supported setup and exits cleanly. A missing runtime falls back with a useful diagnostic.

## Notes / evidence

- The stereo headset image belongs to roadmap stage 2 and should be tracked in a later issue.
- 2026-10-01 implementation: [`source/openxrvr.ixx`](../../source/openxrvr.ixx) owns the optional x86 loader, instance, D3D11 device, and OpenXR session. The pinned SDK submodule builds an x86 loader DLL; [build/test notes](../../docs/openxr-lifecycle.md) record commands and architecture. `[VR] EnableOpenXR=0` is the default in the distributed INI. With no loader present, the enabled Debug ASI logged a clear fallback and GTA IV became responsive without a startup failure.
- The x86 Debug ASI built and, with SteamVR/OpenXR 2.17.10 and the user's lighthouse headset, logged `xrCreateSession(D3D11): 0`, `READY`, `xrBeginSession: 0`, `FOCUSED`, and more than 19,000 zero-layer frames. An early D3D9 reset logged `xrDestroySession: 0` and `xrDestroyInstance: 0`, then recreated the session and resumed frames. The user confirmed visible GTA IV free roam, walking, driving, pause/resume, and alt-tab worked in borderless D3D9 mode. The x86 Release ASI built; the Release loader was checked as x86 and a Release game launch also reached `xrBeginSession` and frame 600.
- A temporary Debug-only controlled `ExitProcess` hook showed that the process stopped advancing in Windows' exit state before an OpenXR shutdown log. The hang's cause is not proven. The hook was removed and the process-detach callback was changed to avoid OpenXR/`FreeLibrary` calls under the loader lock. The stuck process prevented a second launch, so normal exit still needs a fresh-process test after reboot. The installed INI was restored exactly, the final Release ASI and Release x86 loader were installed, and the locked test binaries were moved out of the game folder into ignored `build/openxr-005-backup/`.
- GTA IV executable: 1.2.0.59 Complete Edition; graphics: system D3D9, borderless; runtime: SteamVR/OpenXR 2.17.10. No headset image or performance result is claimed.
- Post-reboot shutdown: [issue 009's bridge](009-local-development-command-bridge.md) destroyed the active session and instance successfully on the EndScene thread, then posted `WM_CLOSE`. GTAIV.exe exited within eight seconds, and subsequent launches succeeded. An invalid `XR_RUNTIME_JSON` returned `XR_ERROR_RUNTIME_UNAVAILABLE` (-51), fell back to a responsive game, and exited through the same command. Missing-loader fallback had already passed. Abrupt process termination bypasses explicit cleanup; ordinary menu quit is not separately verified.
- Final x86 Debug and Release ASI builds both returned exit code 0 after scoping OpenXR dependencies to the FusionFix project. The Release game reached SYNCHRONIZED and 1,218 frames before controlled termination of SteamVR's server/compositor. It observed STOPPING → IDLE → EXITING, successfully ended/destroyed the session and destroyed the instance, and remained responsive. RPC status reported `running=false`, `failed=true`; RPC quit then exited the game within eight seconds. Logs remain in ignored `build/openxr-005-backup/`.
- VR-off Debug route: no OpenXR loader/runtime module was loaded and status reported zero frames. The user confirmed walking, driving, pause/resume, and alt-tab worked, but reported intermittent severe stutter when leaving the apartment that cleared afterward. Its cause is unmeasured; this is not a performance acceptance result. Record and profile this observation before performance claims.
- Closed 2026-10-01: acceptance conditions met for this initialization spike. The final Release ASI/loader were installed and their hashes matched the build outputs. Original installed settings were restored with only the development bridge enabled locally; VR defaults off. Stereo rendering, headset visuals, frame pacing, and episode compatibility remain future work.
