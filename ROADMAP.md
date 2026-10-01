# GTA IV VR roadmap

## Goal and current baseline

Add full 6DOF **single-player VR mode** to GTA IV: The Complete Edition using this FusionFix fork as the integration point. GTA IV, The Lost and Damned, and The Ballad of Gay Tony should be tracked separately; the first playable target is GTA IV free roam.

This is a plan, not a claim that VR already works. The repository builds a 32-bit FusionFix ASI through `premake5.bat` and `premake5.lua`. Issues [#001](issues/closed/001-reproduce-x86-build.md) and [#002](issues/closed/002-flat-screen-baseline.md) record successful x86 builds and a flat-screen GTA IV free-roam baseline. A standalone x86 OpenXR graphics bridge probe passed with SteamVR in [#004](issues/closed/004-select-openxr-bridge.md); it has not displayed an image in the headset or used GTA IV's rendering device. Preserve the existing change to `readme.md`.

## Working loop

1. Start each change from a recorded baseline: Git commit, game executable version, FusionFix build, selected graphics API, OpenXR runtime/headset, GPU, and graphics settings. Keep the game install and runtime files out of Git.
2. Make one small change in a branch. Put VR code behind an opt-in setting and keep the flat-screen path testable.
3. Generate the solution with `premake5.bat`; build the **x86** Debug configuration. Use Release for performance checks. Upstream's `premake5.lua` optionally reads a local `.env` line `GTAIV_DIR=<folder containing GTAIV.exe>` and copies a built ASI into an existing `plugins` folder. Use this only after a known-good baseline and a backup of the installed ASI.
4. Test in increasing scope: compile, flat-screen launch, runtime initialization, headset display, then repeatable free-roam scenarios. Log results and a reproducible failure case in the relevant issue or PR.
5. Update this roadmap when an experiment resolves an architectural question or changes a milestone's exit criteria. Keep upstream FusionFix changes separate from VR-specific changes where practical.

Do not commit proprietary game assets, game binaries, runtime DLLs, headset data, `.env`, generated builds, or local logs. Follow FusionFix's GPL-3.0 license and its single-player scope.

## Milestones

| Stage | Work | Exit evidence |
| --- | --- | --- |
| 0. Reproducible baseline | Confirm submodules/dependencies, Visual Studio toolchain, x86 Debug/Release builds, game version, and unmodified FusionFix behavior. Record a short test matrix for GTA IV and both episodes. | Clean build and flat-screen free-roam launch, with versions and any existing defects recorded. |
| 1. Rendering and runtime spike | Map the game's camera/view/projection data and the final scene, HUD, and post-processing passes. Choose and document one OpenXR graphics bridge after testing feasibility with this 32-bit D3D9 game; FusionFix also exposes a DXVK path. Probe the selected runtime for x86 support. | A design note with measured hook points, a minimal OpenXR session/frame loop, and a documented graphics interop decision. No VR gameplay requirement yet. |
| 2. First headset image | Render or transfer the scene into per-eye OpenXR swapchain images; use runtime-supplied poses/FOV and correct frame timing. Handle session start/stop, focus, swapchain recreation, and device reset. | Stable stereo image in the headset while standing still in GTA IV free roam; desktop mode still launches with VR disabled. |
| 3. Camera and world scale | Decouple head orientation from character movement; set a measured world-to-meter scale, eye offset, near clip, and recenter behavior. Check sky, shadows, reflections, particles, culling, and temporal effects in both eyes. | Head motion has correct parallax and no obvious doubled or missing world geometry across a repeatable walking route. |
| 4. Basic playability | Add seated/standing options, controller bindings or gamepad support, an aiming model, locomotion and turning options, and VR-safe HUD/menus. Handle cutscenes, phone, map, death, and loading transitions. | Complete a short on-foot and vehicle test route, use key menus, and recover from transitions without restarting. |
| 5. Comfort and performance | Offer snap/smooth turn, vignette and camera-shake controls; review forced camera motion, vehicles, FOV-changing effects, post-processing, and frame pacing. Profile CPU/GPU and test at a headset's native refresh target without changing game simulation timing blindly. | Documented comfort defaults, reproducible frame-time measurements, and an explicit list of remaining slow or uncomfortable scenes. |
| 6. Full support for gameplay | Fully support all gameplay features in a VR-native manner. All flat-screen features should be ported to VR equivalents. Guns should have manual reloading with physical clips/magazines. The phone should be a physical object in-world instead of as an overlay. | Game is fully playable from start to end. |
| 7. Compatibility and release | Test GTA IV, TLAD, and TBoGT; test D3D9/DXVK only where supported by the chosen bridge; validate clean install, upgrade, fallback, and removal. Package source, license notices, configuration, and install instructions. | Release candidate matrix with known limitations, clean flat-screen fallback, and repeatable install/uninstall steps. |

## Architectural questions to answer in Stage 1

- **Graphics bridge:** [#004's design note](docs/openxr-graphics-bridge.md) selects x86 D3D9Ex shared texture → D3D11 → OpenXR for the first implementation after a standalone copy/readback passed on SteamVR. GTA IV device compatibility, game-target sharing, synchronization across frames, reset behavior, and latency remain integration gates in #006. Do not assume ordinary D3D9 surfaces can be submitted directly to OpenXR.
- **Stereo scene generation:** Identify the point at which the world can be rendered twice with independent view/projection matrices while avoiding double game simulation, duplicated audio, and side effects. A final-frame stereo copy alone cannot create correct depth.
- **Camera state:** Confirm which values in `source/comvars.ixx` are authoritative at draw time, how camera modes change in vehicles/cutscenes, and whether projection/culling can be overridden safely.
- **Composition:** Decide which passes are world-space, per-eye, or head-locked. In particular test HUD, subtitles, phone, map, reflections, screen-space effects, and loading screens.
- **Runtime lifecycle:** Define the one place that owns OpenXR instance/session/swapchains and their cleanup across `Reset`, game exit, runtime loss, and focus changes.

## Likely code entry points

- `source/fusiondxhook.ixx`: D3D9 CreateDevice, EndScene, and Reset interception. These are investigation points, not proof that EndScene is the right stereo-render boundary.
- `source/dllmain.cpp` and `source/common.ixx`: existing game-process, end-scene, and reset events.
- `source/comvars.ixx`: camera matrices, D3D device, render-target abstractions.
- `source/postfx.ixx`: post-processing resources and reset handling.
- `source/centeredcam.ixx`, `source/cutscenecam.ixx`, `source/settings.ixx`: camera behavior and settings integration.
- `premake5.lua` and `.github/workflows/msvc_x86.yml`: build and CI integration.

## First concrete tasks

Track the initial bringup in [`issues/README.md`](issues/README.md):

1. [#001 — Reproduce the x86 build (closed)](issues/closed/001-reproduce-x86-build.md). Win32 Debug and Release ASI builds now succeed with VS 2026 MSBuild.
2. [#002 — Record the flat-screen baseline (closed)](issues/closed/002-flat-screen-baseline.md). GTA IV free roam passes with the local ASI in borderless D3D9 mode.
3. [#003 — Map camera and rendering hooks (closed)](issues/closed/003-map-rendering-hooks.md). A one-frame two-view test built separate G-buffer, lighting, and DrawScene command lists with distinct viewport snapshots after one observed game-process update; G-buffer and DrawScene commands for both views executed on the render thread. This is the measured scene-command boundary, not a headset image.
4. [#004 — Select an OpenXR graphics bridge (closed)](issues/closed/004-select-openxr-bridge.md). The x86 D3D9Ex → D3D11 → SteamVR OpenXR swapchain copy/readback passed in a standalone probe; game integration remains open.
5. [#005 — Add opt-in OpenXR initialization (closed)](issues/closed/005-openxr-initialization.md). An x86 SteamVR D3D11 session and zero-layer frame loop run in the game. VR-off/on gameplay routes, reset recreation, missing-runtime fallback, controlled runtime loss, and command-driven shutdown passed. Headset visuals and frame pacing remain open; apartment-exit stutter needs measurement before performance claims.
6. [#006 — Route two eye views with safe visibility and targets](issues/open/006-eye-visibility-and-targets.md). Main-world culling ran only once in #003's test; conservative or independent per-eye visibility, target routing, and pixel verification remain required.
7. [#007 — Attribute UI and post-processing draw order](issues/open/007-ui-composition-order.md). Exact HUD, subtitle, phone, radar, and map pixel order remains unmeasured.
8. [#009 — Local development command bridge (closed)](issues/closed/009-local-development-command-bridge.md). The opt-in named pipe supports live status and clean quit with explicit OpenXR shutdown; subsequent launches succeeded.

## References

- [Upstream FusionFix README and build instructions](https://github.com/ThirteenAG/GTAIV.EFLC.FusionFix#building)
- [OpenXR specification](https://registry.khronos.org/OpenXR/specs/1.1/html/xrspec.html)
- [Khronos OpenXR SDK](https://github.com/KhronosGroup/OpenXR-SDK)
