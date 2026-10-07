# GTA IV VR roadmap

## Goal and current baseline

Add full 6DOF **single-player VR mode** to GTA IV: The Complete Edition using this FusionFix fork as the integration point. GTA IV, The Lost and Damned, and The Ballad of Gay Tony should be tracked separately; the first playable target is GTA IV free roam.

The repository builds a 32-bit FusionFix ASI through `premake5.bat` and `premake5.lua`. Issues [#001](issues/closed/001-reproduce-x86-build.md) and [#002](issues/closed/002-flat-screen-baseline.md) record successful x86 builds and a flat-screen GTA IV free-roam baseline. The bridge selected in [#004](issues/closed/004-select-openxr-bridge.md) now displays continuous stereo world images from the game's rendering device in SteamVR, verified in [#006](issues/closed/006-eye-visibility-and-targets.md). Full VR gameplay remains under development. Preserve the existing change to `readme.md`.

## Working loop

1. Start each change from a recorded baseline: Git commit, game executable version, FusionFix build, selected graphics API, OpenXR runtime/headset, GPU, and graphics settings. Keep the game install and runtime files out of Git.
2. Make one small change in a branch. Put VR code behind an opt-in setting and keep the flat-screen path testable.
3. Generate the solution with `premake5.bat` when needed; build **x86 Release** for runtime and performance iterations. Build Debug when debugging/assertions or a Debug-specific issue requires it, rather than as an extra step before every Release run. Upstream's `premake5.lua` optionally reads a local `.env` line `GTAIV_DIR=<folder containing GTAIV.exe>` and copies a built ASI into an existing `plugins` folder. Use this only after a known-good baseline and a backup of the installed ASI.
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

- **Graphics bridge:** [#004's design note](docs/openxr-graphics-bridge.md) selects x86 D3D9Ex shared texture → D3D11 → OpenXR for the first implementation after a standalone copy/readback passed on SteamVR. Game device compatibility, independent eye targets, synchronization and reset passed in #006. This describes the initial bridge; current DXVK/Vulkan pipeline and timing evidence are recorded in #013. Do not assume ordinary D3D9 surfaces can be submitted directly to OpenXR.
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
4. [#004 — Select an OpenXR graphics bridge (closed)](issues/closed/004-select-openxr-bridge.md). The x86 D3D9Ex → D3D11 → SteamVR OpenXR swapchain copy/readback passed in a standalone probe; the selected system-backend path is now integrated in #006.
5. [#005 — Add opt-in OpenXR initialization (closed)](issues/closed/005-openxr-initialization.md). An x86 SteamVR D3D11 session and zero-layer frame loop run in the game. VR-off/on gameplay routes, reset recreation, missing-runtime fallback, controlled runtime loss, and command-driven shutdown passed. Headset visuals and frame pacing remain open; apartment-exit stutter needs measurement before performance claims.
6. [#006 — Route two eye views with safe visibility and targets (closed)](issues/closed/006-eye-visibility-and-targets.md). Independent pose/FOV-correct eye color/depth and conservative visibility passed in apartment/street captures after one observed game-process update. Continuous SteamVR headset rendering, directional shadows, real device reset, explicit shutdown and a final VR-disabled Release launch/image check passed. Runtime pacing removes the gameplay limiter and desktop VSync in opted-in stereo; Release samples reached 89.8–90 Hz at 2560×1440 per eye, with a later outdoor sample at 49.8 Hz. This establishes the first-image gate, not all-scene native FPS or complete VR gameplay. Historical auxiliary-view and startup work is retained in closed #011/#012; profiling remains #013 and specific stereo defects have individual issues. See [integration evidence](docs/eye-target-integration.md).
7. [#007 — Attribute UI and post-processing draw order](issues/closed/007-ui-composition-order.md). Superseded: core ownership established by #020; remaining UI coverage belongs to #031; native phone #022 is closed and cinematic blur #026 is resolved through a game option.
8. [#009 — Local development command bridge (closed)](issues/closed/009-local-development-command-bridge.md). The opt-in named pipe supports live status and clean quit with explicit OpenXR shutdown; subsequent launches succeeded.

9. [#010 — Traffic-light lamps missing in the right eye](issues/open/010-traffic-light-right-eye.md). Isolated light defect; user explicitly kept it separate from #006.
10. [#011 — Validate camera scale, pose timing and auxiliary views (superseded)](issues/closed/011-camera-scale-and-auxiliary-views.md). User considers the foundation working; remaining specific defects have individual issues.
11. [#012 — Diagnose intermittent early startup exits (closed, external)](issues/closed/012-intermittent-early-startup-exit.md). User attributes startup failures to Rockstar Launcher and Windows; historical evidence retained.
12. [#013 — Profile and improve the native-refresh frame budget](issues/open/013-profile-native-refresh-frame-budget.md). Attribute scene-dependent CPU/GPU/interop costs and measure a fixed route; no all-scene native FPS claim.

## Gameplay and rendering backlog

Closed [#029](issues/closed/029-diagnose-free-roam-crashes.md) on2026-10-04 after native model-reference corruption and render-command exhaustion fixes, repeated gameplay testing, final flat-screen acceptance and the user's confirmation that free-roam crashes are fixed. Startup #012 is closed as external Rockstar Launcher/Windows behavior; performance profiling remains #013.

Closed [#014](issues/closed/014-first-person-headset-camera.md) on2026-10-04: the user accepts the implemented first-person/room-scale/crouch/action/car/headset-aiming route together and separately confirms the final flat-screen check. Extra vehicle classes/passenger routes remain #019, scopes/special weapons #024, and remaining specific stereo defects have individual issues; broad #011 is superseded. Other special camera transitions are deferred; cutscenes retain their existing camera path.

Sequence camera/scale and controller foundations before dependent interactions. Rendering defect reproduction can proceed independently. Each issue owns its acceptance evidence; milestone completion still requires the combined route.

Performance work in #013 is shelved at the user's request on 2026-10-05 so another task can proceed. Preserve the working installed DXVK/Vulkan path, shaders, experiments and measured evidence; #013 remains open without an all-scene 90 Hz claim. The [checkpoint](docs/performance-013-checkpoint.md) records stable settings and how to resume. Distributed defaults remain opt-in with the D3D9Ex/D3D11 fallback; see [Vulkan integration and limits](docs/vulkan-openxr-feasibility.md).

The #014 camera foundation and #016 tracked hands/standard-pistol milestone are user-accepted and closed. Vive Wand rig IK, calibrated wrists, finger inputs, independent pistol muzzle/trigger firing, held support grip and tracked-body/directional-leg work are implemented. #015 is closed by user acceptance of touchpad traversal, semantic bindings and basic gameplay/car buttons; no haptics or exhaustive recovery-test completion is claimed. Native prop contact and configurable weaker arm drive are accepted in #018; grabbing/throwing remain open. Remaining weapon classes, left-handed ownership and obstruction/gameplay/fallback verification are tracked together in #024. #011 is closed as outdated; physical vehicle interaction/broader classes remain #019. See [tracked-hand evidence](docs/tracked-hands.md). #007 is closed as superseded; preserve its radar/menu ownership findings and investigate remaining UI/lifecycle boundaries in #031; #022 and #026 are closed. UI #020 has a user-accepted left-arm radar watch including health/armor. Wanted stars and the early world-space loading/pause screen are now user-accepted. Tracked menu pointing and semantic SteamVR rebinding are now user-accepted in #015/#020; Core native text/popup HUD on a shared stereo plane and generic car dashboard radar are now user-accepted; #020 is closed. Broader UI/mission/lifecycle/episode coverage and active SMS validation are tracked in [#031](issues/open/031-ui-coverage-and-lifecycle.md); Native held phone presentation and Wand controls are user-accepted in closed #022; broader calls/messages/mission and episode/fallback coverage remains #031.

[#028](issues/open/028-independent-desktop-view.md) tracks an ordinary third-person desktop image with its existing HUD alongside the headset views, including VR-only character hiding. It requires separate render/visibility ownership and an explicit cost measurement; it does not require choosing a new headset UI layout.

| Area | Issues and order |
| --- | --- |
| Stage 3 camera and tracking | [#014 first-person camera, headset frusta and room-scale body movement (closed)](issues/closed/014-first-person-headset-camera.md); [#011 scale/pose validation (superseded)](issues/closed/011-camera-scale-and-auxiliary-views.md). Remaining specific defects are tracked individually. |
| Stage 3 stereo defects | [#010 traffic lamps](issues/open/010-traffic-light-right-eye.md), [#023 objective markers](issues/open/023-objective-markers-both-eyes.md), and [#027 water shading/reflections](issues/open/027-stereo-water-shading.md). |
| Stage 4 controller and weapon foundations | [#015 Vive Wand support (closed)](issues/closed/015-vive-wand-controller-support.md) and [#016 6DOF hands/firearms (closed)](issues/closed/016-six-dof-hands-and-firearms.md); remaining [#019 vehicles](issues/open/019-vr-vehicles.md) and #024 firearms. [#021 weapon wheel (canceled)](issues/closed/021-vr-weapon-wheel.md): retain native weapon swapping. Inventory actual vehicle classes before defining plane support. |
| Stage 4 composition | [#007 attribution (superseded)](issues/closed/007-ui-composition-order.md), then [#020 core minimap/subtitles/overlays (closed)](issues/closed/020-vr-overlays-and-subtitles.md); [#031 UI coverage/lifecycle](issues/open/031-ui-coverage-and-lifecycle.md) remains open; native held phone controls are accepted in closed #022. |
| Stage 5 comfort and performance | [#013 frame-budget profile](issues/open/013-profile-native-refresh-frame-budget.md) remains open; [#026 cinematic depth/focus blur (closed)](issues/closed/026-disable-cinematic-depth-blur.md) is resolved through the existing game option. |
| Stage 6 physical interactions | After tracked hands: [#017 hand/melee combat (closed)](issues/closed/017-six-dof-melee-combat.md), [#018 physics-object grabbing](issues/closed/018-six-dof-physics-object-grabbing.md), [#022 native physical phone (closed)](issues/closed/022-physical-interactive-phone.md), [#024 physical reloads](issues/open/024-physical-weapon-reloading.md) and [#025 physical money/weapon drops](issues/open/025-physical-money-and-weapon-drops.md). Reloads/drops depend on the grab foundation. |

VR settings and explicit height calibration are tracked in [#030](issues/open/030-pause-menu-vr-settings-and-height-calibration.md). Until that pause-menu workflow exists, #014 uses INI `[VR] PlayerHeight=1.75` meters with physical crouch entry/exit at65%/75%. Settings UI and calibration do not block the current configured-height camera route.

## References

- [Upstream FusionFix README and build instructions](https://github.com/ThirteenAG/GTAIV.EFLC.FusionFix#building)
- [OpenXR specification](https://registry.khronos.org/OpenXR/specs/1.1/html/xrspec.html)
- [Khronos OpenXR SDK](https://github.com/KhronosGroup/OpenXR-SDK)

VR physical-interaction attribution is tracked in [#032](issues/open/032-vr-interaction-player-attribution.md): native owner/police reactions to tracked-hand vehicle damage and deliberate opening attempts. #018 includes native articulated NPC/ragdoll grabbing, prioritizing dead and already-ragdolled bodies; standing NPC interaction remains an extension.

Native carryable-prop mission recognition is an unverified compatibility follow-up in #018. No affected mission or progression dependency has been identified; defer its adapter until a concrete use case requires it, rather than making it a grab-foundation acceptance gate. Inventory money/weapon pickups remain #025.

Closed #018 on2026-10-07 by explicit user acceptance of the current physics-grab milestone, retaining unverified throw/lifecycle/regression coverage as documented limitations. Native mission pickup integration is deferred unless a concrete problem is reported. The accepted tracked fist/knife/bat/cue combat milestone is now closed #017; remaining melee work is #033.

Closed #017 on2026-10-07 by user acceptance of tracked punches, knife, bat and cue NPC combat plus native car/prop strikes. Remaining melee haptics, physical weapon blocking/support grips and lifecycle/episode/flat-screen validation are tracked in [#033](issues/open/033-melee-feedback-and-compatibility.md).
