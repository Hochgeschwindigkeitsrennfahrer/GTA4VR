# GTA IV VR roadmap

## Goal and current baseline

Add full 6DOF **single-player VR mode** to GTA IV: The Complete Edition using this FusionFix fork as the integration point. GTA IV, The Lost and Damned, and The Ballad of Gay Tony should be tracked separately; the first playable target is GTA IV free roam.

The repository builds a 32-bit FusionFix ASI through `premake5.bat` and `premake5.lua`. Successful x86 builds, a flat-screen free-roam baseline and continuous independent stereo rendering in SteamVR are established. Full VR gameplay and episode compatibility remain under development. Preserve the existing change to `readme.md`.

## Working loop

1. Start each change from a recorded baseline: Git commit, game executable version, FusionFix build, selected graphics API, OpenXR runtime/headset, GPU, and graphics settings. Keep the game install and runtime files out of Git.
2. Make one small change in a branch. Keep a DisableOpenXR flat-screen opt-out and keep the flat-screen path testable.
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

- **Graphics bridge:** [Initial bridge design](docs/openxr-graphics-bridge.md) selects x86 D3D9Ex shared texture → D3D11 → OpenXR for the first implementation after a standalone copy/readback passed on SteamVR. Game device compatibility, independent eye targets, synchronization and reset passed during stereo integration. This describes the initial bridge; current DXVK/Vulkan pipeline and timing evidence are recorded in [performance issue #2](https://github.com/bordplate/GTA4VR/issues/2). Do not assume ordinary D3D9 surfaces can be submitted directly to OpenXR.
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

## Accepted foundations

The tested GTA IV free-roam foundation includes first-person headset camera and room-scale body collision, physical crouch, jumping/vaulting/ejection and the current car camera. Cutscenes retain their existing camera path. Native model-reference and render-command capacity fixes resolved the reproduced free-roam rendering failures.

Vive Wand tracking, calibrated rig IK, finger controls, locomotion, semantic SteamVR bindings and native muzzle-directed firearm firing are accepted. Physical prop/ragdoll grabbing, tracked fist/knife/bat/cue combat, GTA IV firearm reloads/scopes, two-hand aiming and grenade/Molotov throws are accepted for the tested routes. Left-handed on-foot weapon ownership, haptics, grenade cooking, episode variants and exhaustive lifecycle/fallback coverage are not claimed complete.

Accepted UI includes wrist radar/health/armor/wanted stars, generic car dashboard radar, stereo text/popup HUD, world-space loading/pause screens with tracked menu pointer, and a native held phone with Wand controls. Spatial audio follows the headset. Broader mission, episode and transition coverage remains pending.

Car hands, physical steering and visible wheel rotation are accepted across multiple tested cars. Left-hand drive-by weapon selection/attachment and manual in-car phone presentation are accepted; broader combined driving/fire/ammunition and vehicle-class routes remain open.

Implementation evidence remains in [rendering pipeline](docs/rendering-pipeline.md), [eye-target integration](docs/eye-target-integration.md), [tracked hands](docs/tracked-hands.md), [room-scale collision](docs/room-scale-collision.md), [vehicle interactions](docs/vehicle-interactions.md) and [spatial audio](docs/vr-spatial-audio.md). Historical three-digit IDs in these technical notes predate GitHub migration and are not GitHub issue numbers.

## Active backlog

Use the [GitHub tracker](https://github.com/bordplate/GTA4VR/issues) for tasks, current status and acceptance evidence. The local issue tracker was retired on 2026-10-10.

| Area | GitHub issue |
| --- | --- |
| Traffic-light stereo rendering | [#1 — Restore traffic-light lamps in the right eye](https://github.com/bordplate/GTA4VR/issues/1) |
| Frame budget and rendering performance | [#2 — Profile and improve the native-refresh frame budget](https://github.com/bordplate/GTA4VR/issues/2) |
| Vehicle interactions and coverage | [#3 — VR vehicle interaction and driving](https://github.com/bordplate/GTA4VR/issues/3) |
| Objective marker stereo rendering | [#4 — Fix world objective markers visible only in the left eye](https://github.com/bordplate/GTA4VR/issues/4) |
| Physical inventory pickups | [#5 — Physics-based money and weapon drops](https://github.com/bordplate/GTA4VR/issues/5) |
| Water rendering | [#6 — Fix stereo water shimmer and reflections](https://github.com/bordplate/GTA4VR/issues/6) |
| Independent desktop camera/HUD | [#7 — Independent third-person desktop view](https://github.com/bordplate/GTA4VR/issues/7) |
| VR settings and height calibration | [#8 — Pause-menu VR settings and player-height calibration](https://github.com/bordplate/GTA4VR/issues/8) |
| UI mission/episode/lifecycle coverage | [#9 — Expand VR UI coverage and validate lifecycle](https://github.com/bordplate/GTA4VR/issues/9) |
| Native owner/police reactions | [#10 — Attribute VR physical interactions to the player](https://github.com/bordplate/GTA4VR/issues/10) |
| Melee feedback and compatibility | [#11 — Melee feedback and compatibility follow-up](https://github.com/bordplate/GTA4VR/issues/11) |
| Required cover action | [#12 — Concrete Jungle cover entry](https://github.com/bordplate/GTA4VR/issues/12) |
| Shop purchases and mission progression | [#13 — Clothing-store purchases](https://github.com/bordplate/GTA4VR/issues/13) |
| Activities and minigames | [#14 — Minigame gameplay and controls](https://github.com/bordplate/GTA4VR/issues/14) |
| NPC services and mission interactions | [#15 — Vendors and bowling staff](https://github.com/bordplate/GTA4VR/issues/15) |

A full VR-only playthrough is not currently possible. Required cover, clothing purchases, NPC service interactions and minigames can require keyboard/gamepad input or temporarily disabling VR and completing the section on a flat screen. These are progression blockers, not completed gameplay support.

Performance work resumed on 2026-10-12 under [#2](https://github.com/bordplate/GTA4VR/issues/2). Prepared screen-dimension mapping preserves the existing shader-constant signatures and improves matched flyover throughput by approximately 4%; see [configuration, measurements and limits](docs/vr-resolution-performance.md). Native G-buffer/DrawScene execution remains the larger bottleneck, and no all-scene 90 Hz result is claimed. Preserve shaders and experiments and the [earlier checkpoint](docs/performance-013-checkpoint.md); see [Vulkan integration and limits](docs/vulkan-openxr-feasibility.md). Distributed VR defaults remain enabled with DisableOpenXR=1 opting out with the D3D9Ex/D3D11 fallback.

The native VR pause-menu tab under [#8](https://github.com/bordplate/GTA4VR/issues/8) exposes standing height/calibration, physical crouch, body placement, HUD/radar placement and eye resolution. Its [design and validation](docs/vr-pause-settings.md) use owner-applied live changes, including an explicit eye-resolution Apply; enabling/disabling VR takes effect on the next launch. Physical crouch enters/exits at 65%/75% of configured standing eye height; broader menu/episode coverage remains pending.

Native carryable-prop mission recognition is deferred unless a concrete progression problem is reported; no affected mission has been established. Physical money/weapon collection belongs to #5. Retain native weapon swapping; a separate weapon wheel is not required. Cinematic depth/focus blur can be disabled through the existing game option. Startup failures attributed to Rockstar Launcher/Windows are not an open VR implementation issue.

## References

- [Upstream FusionFix README and build instructions](https://github.com/ThirteenAG/GTAIV.EFLC.FusionFix#building)
- [OpenXR specification](https://registry.khronos.org/OpenXR/specs/1.1/html/xrspec.html)
- [Khronos OpenXR SDK](https://github.com/KhronosGroup/OpenXR-SDK)
