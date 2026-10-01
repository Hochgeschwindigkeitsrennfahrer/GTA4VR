# Eye-target integration (issue 006)

## Current result and limits

Independent color/depth images, continuous live headset rendering, conservative world visibility, one game-process callback per stereo build, and a real resolution reset have passed on GTA IV CE 1.2.0.59 / SteamVR OpenXR 2.17.10 / RTX 4070 Ti. The user accepted the guarded directional shadow correction outdoors. World scale, pose latency, water reflections, unusual weather/temporal cases and both episodes remain unverified; auxiliary reflection/weather views remain based on the game camera. The isolated right-eye traffic lamp defect is tracked in issue 010.

Use the system backend (`API=0` in FusionFix's resolved `d3d9.cfg`) for the D3D9Ex → D3D11 bridge. API=1 selects DXVK; a Vulkan startup access violation and rejected D3D11 shared-texture opens were observed on that backend. Ex/stereo installation now checks the actual graphics config rather than the ASI INI and declines unsupported DXVK routing. Back up the graphics config before a local test change.

OpenXR-driven pacing now samples 90.00 projection layers/sec against the queried 90 Hz headset at 2560×1440 per eye in Release (PID 31412, two five-second averages, 3021 stereo builds / 3020 layers). The preceding default-presentation run measured 55.80 Hz. With OpenXR, stereo, submission and refresh matching opted in, windowed Ex creation/reset request IMMEDIATE desktop presentation; the live swapchain confirmed `0x80000000`. The real device Reset interception is required: the deprecated FusionFix D3D9 parameter event is not dispatched in this fork. VR-off creation retains the game's original presentation interval; a VR-on launch that later falls back retains IMMEDIATE presentation while its ordinary saved FPS limiter resumes. Native cadence in these samples is established; sustained all-scene performance, frame-time percentiles and compositor display counts remain unmeasured.

Final Release VR-off smoke passed in PID 22860 with Ex overrides disabled: correct apartment free roam and HUD in a fresh native capture, responsive RPC, then clean command-driven process exit. Earlier user gameplay-route acceptance remains the baseline. Intermittent early exits before ASI load remain under investigation in issue 012; this is not a claim of universally reliable startup. The sections below retain the experiment chronology.

After reboot, PID 16664 rendered continuously at 2560×1440, FOCUSED, with a fresh outdoor image and distinct finite eye depth captures. A sample reached 89.80 Hz and a later outdoor sample fell to 49.80 Hz. Scene-dependent game/rendering performance is the working hypothesis; cause attribution requires the CPU/GPU/interop profile in issue 013. No further fixed cap is inferred from that rate alone.

## Live-device gate

The standalone D3D9Ex probe in issue 004 used its own device. Before routing eye command streams, test the actual GTA IV device with `[DEVELOPMENT] ProbeGraphicsInterop=1` in the local FusionFix INI. This setting defaults off when absent and is independent of `EnableOpenXR`.

`source/vrinteropprobe.ixx` runs once after 180 unpaused EndScene callbacks. It records results in `GTAIV/plugins/GTAIV.VR.interop.log`, queries `IDirect3DDevice9Ex`, describes the current color/depth and back buffer, attempts an A8R8G8B8 shared render texture and `StretchRect` from the back buffer, and tests one system-memory readback. All temporary COM references are released before returning; no camera, target binding, or retained reset resource is changed. The readback may cause a one-time stall. Logs stay outside Git.

EndScene targets describe the end of the desktop frame; they do not identify the main-world G-buffer or establish per-eye target ownership. A successful shared handle also needs D3D11 opening and synchronized pixel readback before establishing interoperability. CPU readback success alone does not justify a production VR transfer path or a performance claim.

## Next gates

1. Resolve the live-device sharing result: test an opt-in Ex-compatible device path or measure a transfer fallback if the existing device cannot share.
2. Map queued target-setting commands to G-buffer, lighting, and DrawScene and preserve their ownership through render-thread execution.
3. Build coherent runtime pose/FOV viewports and a conservative visibility set before independent per-eye color/depth rendering.
4. Verify pixels from both eye targets, then integrate synchronized OpenXR swapchain copies and reset handling.

Issue 006 closed on 2026-10-01 after separate eye color/depth, continuous headset submission and one observed game-process update per stereo build were verified. Broader camera/auxiliary validation remains in issue 011; startup reliability and native-refresh profiling remain in issues 012/013.

## First live result, 2026-10-01

GTA IV Complete Edition 1.2.0.59, system D3D9, borderless 1920×1080, VR disabled, x86 Debug ASI based on `ef8c42b`, NVIDIA RTX 4070 Ti. The game automatically loaded its free-roam save and remained responsive to bridge status/quit.

| Operation | Observed result |
| --- | --- |
| Query `IDirect3DDevice9Ex` | `0x80004002` (`E_NOINTERFACE`) |
| Live device parameters | Adapter 0, HAL, behavior flags `0x44` |
| EndScene color and back buffer | 1920×1080, format 21 (`A8R8G8B8`), default pool, render target, no MSAA |
| EndScene depth | 1920×1080, format 1515474505 (`INTZ`), default pool, depth stencil, no MSAA |
| Create shared A8R8G8B8 texture | `0x8876086C` (`D3DERR_INVALIDCALL`), no shared handle |
| Back-buffer system-memory readback and lock | Success, pitch 7680; first bytes `16 1B 1B 1B` |

The selected Ex→D3D11 bridge cannot be applied directly to this existing device. Next investigate an opt-in Ex-compatible creation path, including hook compatibility, managed-resource usage, reset behavior, and flat-screen regression tests. The CPU path is only an observed diagnostic fallback; transfer time, repeated-frame reliability, and D3D11 upload are unmeasured. No camera, culling, eye isolation, or headset image result follows from this test.

The probe game exited through RPC and the previous Release ASI/INI were restored; ASI hashes matched. Raw output is kept in ignored `build/issue006-backup/interop-debug.log`.

## Opt-in Ex experiment

`[DEVELOPMENT] ExperimentalD3D9Ex=1` replaces the executable's imported factory and requests `CreateDeviceEx` for windowed mode on the system backend. `ExRemapManagedBuffers=1` adapts managed vertex/index-buffer requests to the default pool. Both switches default off when absent. A rejected managed vertex buffer preceded an access violation without the adaptation. The initial adapted run showed only the radar over a black world; Ex-compatible allocation of FusionFix's two SMAA lookup textures restored world rendering in a directly inspected window screenshot. Default-pool lookup textures are released/recreated with post-processing resources. Full-route and reset validation remain open; these switches are experimental.

The adapted live device exposed Ex, created a shared 1920×1080 A8R8G8B8 texture, and copied the back buffer into it. A bounded D3D9 event wait completed. D3D11 opened the texture on the game adapter LUID and reported BGRA8 UNORM. Whole-image hashes from separate D3D9/D3D11 readbacks matched (`98131949D3A7BF9D`). Debug verification took 279.700 ms including D3D11 device creation, synchronization, both readbacks, and CPU hashing; this does not measure a repeated-frame GPU bridge or establish VR performance.

Ghidra annotations were saved at import-name `0x0102e67e`, phase setup `0x00d68bd0`, and viewport assignment `0x005dc220`. Disassembly of setup confirms two copies from `parent+0x10` into `phase+0xB0` and `phase+0x4A0`; the assignment carries dependent matrix/plane state rather than recomputing it. This agrees with issue 003's runtime observations. Viewport generation must therefore update those dependencies explicitly or use a verified recomputation routine. Static `0x00430bf0` has no reliable stored function body, so its decompiler output is not evidence for a matrix algorithm.

The OpenXR module now samples both views in a LOCAL reference space at predicted display time and publishes a synchronized snapshot only when position and orientation are valid. `[DEVELOPMENT] ProbeEyeViewport=1` captures that snapshot alongside the world phase's matrices for convention validation; the new probe remains under runtime test. No pose override, separate eye target, or headset image has been established yet.

## Viewport conventions measured in the game

A Debug run with the Ex device and OpenXR enabled reached free roam on 2026-10-01. Bridge status reported a running SYNCHRONIZED session; a fresh window screenshot showed Niko and the apartment. The one-shot scene probe captured two valid LOCAL eye poses with distinct positions and asymmetric FOVs alongside the game viewport. This establishes sampling, not an eye image.

- Matrices use row vectors. `mCameraMatrix` stores three-component basis/translation rows; its fourth column contained Debug padding sentinels and must not be used as a homogeneous matrix. `mViewInverseMatrix` is the corresponding homogeneous camera pose, and `mViewMatrix` is its rigid inverse.
- Projection is right handed, looking along camera −Z, with D3D depth `[0,1]`. The sample used near/far 0.05/1500, vertical FOV 45°, and aspect 1.777778. Its projection diagonal was approximately 1.357995/2.414213; entries `[2][2]`, `[2][3]`, `[3][2]` were −1.00003338, −1, −0.05000167.
- `mMatrix_240` is the viewport-to-screen transform: for the sampled 1920×1080 viewport its diagonal is 960, −540, 1 and translation is 960, 540, 0. `mMatrix_200` equals `mWorldViewProjMatrix * mMatrix_240`, rather than a separate frustum matrix.
- The six normalized clip planes match near, left, right, far, top, bottom, with points inside satisfying `dot(position, plane) >= 0`. Extracting the far plane from float projection coefficients loses precision at this near/far ratio; the prototype regression explicitly tolerates that loss. Generation should preserve the specified camera-space far distance.

`source/includes/vrmath.hpp` implements these conventions independently of the game. The x86 `tools/tests/vrmath.cpp` test verifies asymmetric frustum corners at near/far, a rotated pose inverse, and matrix/plane entries against the actual sample. Build/run it from an x86 Visual Studio developer shell:

```powershell
cl /nologo /std:c++20 /EHsc tools/tests/vrmath.cpp /Fo:build/vrmath.obj /Fe:build/vrmath.exe
./build/vrmath.exe
```

The test passed. Ghidra's `Issue006ViewportMath` bookmark at `0x005dc220` was previewed, applied, read back, and saved. Remaining scalar/tail dependencies, camera overrides, visibility, eye targets, and headset submission are unverified. A stopped session now invalidates the published eye snapshot.

## Engine setters and local eye generation

The read-only `tools/inspect-game-code.cpp` helper inspects a bounded live code region by process ID and Ghidra VA. It locates the running GTAIV module and converts from image base `0x00400000`; it requests read/query process rights and never writes memory. Build it with the repository's bundled Zydis implementation from a Visual Studio developer shell; keep output under ignored `build/`:

```powershell
cl /nologo /c /O2 /Iexternal/injector/zydis external/injector/zydis/Zydis.c /Fo:build/inspect-zydis.obj
cl /nologo /std:c++20 /EHsc tools/inspect-game-code.cpp build/inspect-zydis.obj /Fo:build/inspect-game-code.obj /Fe:build/inspect-game-code.exe
./build/inspect-game-code.exe <PID> 0x004319b0 0x1F0
```

On the sampled CE 1.2.0.59 process, live base was `0x00390000`. Live code confirmed these Ghidra-address routines:

| Address | Supported role |
| --- | --- |
| `0x004319b0` | `__thiscall` camera setter, one matrix argument; copies camera xyz rows, creates homogeneous camera/inverse view, calls dependent-matrix and frustum updates. |
| `0x004317e0` | `__thiscall` custom projection setter, one matrix argument; sets perspective, copies projection, calls both updates. FOV/aspect/tan-half-angle metadata remains caller owned. |
| `0x00430f00` | Recomputes world-view, world-view-projection, and screen matrix product. If its destination is the global current viewport, it also updates GPU constants. |
| `0x00431ba0` | Builds and normalizes six planes from view/projection; supports the invert-Z flag. |

Bookmarks at all four addresses were previewed, applied, read back, and saved. No on-disk function repair or reanalysis was performed.

`source/vrcamera.ixx` resolves the two setter signatures only when the viewport probe or experimental stereo option is enabled. Missing/nonunique signatures disable generation. It recenters the initial LOCAL eye midpoint on the game's camera, composes the relative eye pose with the homogeneous game camera, supplies an asymmetric projection, and invokes the setters on private viewport copies. A reset requests recentering. The first prototype assumes one game unit per meter and uses the first eye orientation for the reference orientation; measured world scale, explicit user recentering, session-space changes, and camera decoupling are later integration work.

The Debug local-copy test passed: `generatedValid=1`, distinct eye camera positions approximately 0.068 game units apart, different asymmetric projections, and populated dependent matrices and planes. Bridge status remained responsive with a running OpenXR session. No eye camera was applied to the game's render phases, so this is a viewport-generation result, not a stereo image. The one-frame `[DEVELOPMENT] ProbeRenderBindings=1` trace is being tested to map target writes/reads before routing eye resources; process-local phase pointer values alone do not prove command ownership.

## Render-thread target attribution

The first bounded device trace showed that `CRenderPhase::sm_pCurrent` was often zero or changed between binds on the same render thread. Do not use that global alone to decide an eye's resource ownership. Additive after-list events for the three world phases now permit custom `CBaseDC` markers before and after their list commands. Each marker carries the game-process trace counter into the render stream.

A Debug run executed G-buffer, lighting, and DrawScene markers for frame 141 on one render thread, with no extra game-process update introduced by the trace. A fresh screenshot showed the apartment world and bridge status remained responsive. Measured target operations between those markers:

| Marker scope | Operations observed |
| --- | --- |
| G-buffer list | No target binding between the begin/end markers. G-buffer color/depth binds occurred earlier; resource setup is outside this list marker and must be routed or rebound explicitly. Clear placement is under measurement. |
| Lighting list | Writes `FullScreenCopy`, also writes G-buffer 2 with G-buffer 3 depth; samples all G-buffer textures and stencil, plus shared reflection data. |
| DrawScene list | Writes `FullScreenCopy`, uses back depth and G-buffer textures, writes screen/depth-copy intermediates, and runs the post-processing target chain through blur/luminance/SMAA before the desktop back buffer. |

Thus DrawScene includes substantial post-processing work; a final shared HDR texture alone is insufficient for eye isolation. The next trace adds clear/copy operations and bound-target snapshots at marker entry. The scene/lighting/draw list markers do not cover every phase prologue or epilogue.

The conservative visibility math helper builds a world AABB enclosing every corner of both finite eye frusta. An x86 test with rotated and translated cameras verified that all corners lie inside its planes. This trades efficiency for conservative inclusion and has not been applied to the live culling path; no geometry-visibility or performance result is claimed.

## Stereo routing experiment

### Continuous mode, shadows and headset pacing

Selective auxiliary scheduling alone did not fix head-dependent shadows. A draw-time probe confirmed that VS c12–15 and PS c15 contained the queued eye camera, with the same sun and shadow matrix in both eyes. Directional deferred PS1/PS2 use PS c14 only for cascade selection against cached c54 splits: using the head-oriented eye forward vector with game-camera cascade data is inconsistent. The next guarded correction recognizes that exact instruction pattern and sole c14 use in the live pixel shader, then supplies the original game-camera forward vector to PS c14 only while that shader is bound. The eye basis in vertex constants and the eye position in PS c15 remain intact. Original constants are restored before other shaders and ordinary passes. Runtime activation passed in PID 16264; the user then confirmed that outdoor shadows looked correct while turning their head. The isolated right-eye traffic-light lamp issue is tracked in #010, per the user's request, and does not block #006.

Use `ExperimentalStereoFrames=0` for development headset checks. The asymmetric deferred-lighting correction removed the excessive shadow eye offset according to the user; stretching, blinking and missing shadows remain under investigation. The second eye now builds only the three verified world phases while the first eye builds the complete phase array. Live CE disassembly at Ghidra `0x008F7FA4` confirmed the phase-array dispatch layout used for this scoped change. Shared shadow/reflection/weather/UI resources are updated once per game update; runtime compatibility of this scheduling change remains under test.

`[VR] MatchHeadsetRefresh=1` bypasses the ordinary FusionFix gameplay limiter while the running stereo session has eye images and requests rendering. The render thread's existing `xrWaitFrame` supplies runtime timing and backpressure; there is no additional fixed-Hz sleep. When stereo stops, rendering is not requested, or OpenXR fails, the saved FusionFix limiter applies again. Loading, cutscene and minigame safety limits still apply. This does not change headset refresh or establish a simulation/display-time synchronization scheme: the prototype still renders poses published by the previous OpenXR callback. See the [runtime frame timing specification](https://registry.khronos.org/OpenXR/specs/1.1/man/html/xrWaitFrame.html).

`status` reports `displayRefreshHz` from the optional `xrGetDisplayRefreshRateFB` extension and the most recent five-second average `submittedHz`; submission rate measures projection layers, not guaranteed unique or displayed frames. Pacing does not depend on that extension. OpenXR's predicted display period may differ from hardware refresh, so it is not used to infer a native headset rate. See the [refresh-rate query](https://registry.khronos.org/OpenXR/specs/1.1/man/html/xrGetDisplayRefreshRateFB.html) and [frame-state specification](https://registry.khronos.org/OpenXR/specs/1.1/man/html/XrFrameState.html).

### Live submission result (2026-10-01)

Changing the game resolution from 1920×1080 to 2560×1440 produced a real reset in PID 10000. OpenXR destroyed and recreated its instance/session; after leaving Graphics, the eye resources returned ready and another 600-frame batch completed with submitted layers. Paused menu samples with no eye images were not failures. The user then reported excessive shadow separation between the eyes despite correct-looking geometry/depth; shadow correctness remains a blocking issue. The deferred-lighting vertex shader source reconstructs symmetric rays from `c209.xy`; the eye projections are asymmetric. A bounded runtime shader diagnostic is checking that lead before a correction is accepted.

PID 2352's runtime vertex-shader disassembly confirmed the exact fullscreen deferred VS0: 11 instructions, symmetric `mul r0.xy, r0, c209`, then camera basis `c12`–`c14`. The stereo correction reassembles only this verified shader with `mad r0.xy, r0, c209.xyxy, c209.zwzw`. During its lighting scope, `c209` contains half tangent spans and tangent centers from the queued eye FOV; its original value/shader are restored before other shaders or the ordinary pass. Local-light volume shaders use this register differently and are excluded. An x86 math regression projects reconstructed interior points back to their original NDC at several depths, including off-center terms. Runtime activation passed in PID 26744: the verified shader assembled successfully and the continuous session exceeded 1,000 eye builds with active projection submissions. A fresh native capture showed the apartment world. Headset shadow alignment remains awaiting an outdoor observation.

The user confirmed a live apartment image in the headset and could look around during the 600-frame batch. PID 10000 reported FOCUSED, 600 stereo builds and successful projection layers; a fresh native capture showed the desktop apartment view. This establishes bounded indoor live stereo, with prototype scale and a game-frame pose delay. It does not establish outdoor rendering, comfort, performance or reset compatibility.

The image provider fences the current D3D9 writes before the OpenXR owner copies either shared texture with D3D11. Every mip surface of each cloned target is routed, including the AO depth chain. The conservative union planes are applied to scene, lighting and DrawScene viewport copies. Both three-phase eye streams retain one game-process callback counter. Auxiliary shadows/reflections/cloud/rain still use the engine's existing resources and need explicit testing.

Development headset checks use `[VR] ExperimentalStereoFrames=0` for continuous rendering. Keep this enabled throughout walking and outdoor checks. Values 1–600 remain available for bounded diagnostics, which hold the last pair afterward. `tools/game-command.ps1 stereo-test` restarts the counter and captures a fresh color/depth pair on the next game-process callback; it does not inject input or change saves. `status` includes readiness, built frame count, frame budget and a `continuous` flag. Distributed VR defaults remain off.

For startup diagnosis, `tools/game-crash-monitor.cpp` builds as an x86 standalone observer with MSVC and `dbghelp.lib`. It waits up to 45 seconds for one GTAIV process, attaches with kill-on-exit disabled, observes another 45 seconds, and detaches while retaining the game. `--startup` extends the bounded discovery/observation windows to 120/180 seconds to cover launcher and loading delays. Unhandled exceptions record registers, module bases and bounded stack reads; exceptions retain normal handling. Keep output under ignored `build/`. It neither fixes nor suppresses the intermittent early assertion/access violation. In-process exception logging is separately opt-in with `[DEVELOPMENT] ProbeGraphicsExceptions=1`; distributed behavior remains off.

`game-crash-monitor.exe --snapshot` reads up to 64 thread contexts/stacks at the debugger's initial attach breakpoint, then detaches. Use it for a startup dialog that blocks before RPC exists; it does not change registers or dismiss the dialog. This localized the CE menu lookup assertion to `Common::Common`: the settings background hook overwrote `A1 ... 83 F8 08` at static `0x005A9412`. The intact CE scroll-menu load at `0x008BFE9D` references the same static global `0x01160C40`. A fallback signature uses that load. `Issue006Startup` at `0x005A9412` was previewed, applied, read back and saved in Ghidra. Separate early AVs remain unexplained.

The live code inspector also supports `inspect-game-code.exe <PID> --pattern "AA ? BB"`. It reads the GTAIV module only, capped at 64 MiB, and reports match addresses and bounded nearby bytes without writing memory or saving a binary dump. This confirmed zero original menu matches and one surviving scroll-menu suffix in the failed PID 21308.

The follow-up trace recorded G-buffer 0/1/2 and R16F stencil bound at marker entry, with INTZ G-buffer 3 depth. The full color/depth clear occurs after that entry marker. This permits rebinding the actual device state before geometry, including bindings that the game's pointer cache omits. DrawScene includes the existing post-processing chain and its final back-buffer write.

`source/vrstereo.ixx` is an opt-in experiment, initially limited to one stereo frame. It creates independent named world/post-processing textures and two shared A8R8G8B8 final-color textures. Queued markers rebind render targets, depth and samplers at each world phase boundary, then restore original resources between phases. The scheduler generates eye viewports and conservative visibility planes, invokes the command builder twice, and retains one game-process callback counter. This implementation remains under test; builder repetition may have auxiliary side effects.

For this test, set `[VR] ExperimentalStereo=1` and `ExperimentalStereoFrames=1`, with the Ex device and OpenXR enabled. Disable `ProbeEyeViewport`, `ProbeRenderBindings`, and `ProbeGraphicsInterop` to avoid conflicting hooks or probe work. The distributed defaults remain off. The render-thread trace goes to `plugins/GTAIV.VR.stereo.log`; after both DrawScene streams complete, the diagnostic writes `GTAIV.VR.eye0.bmp` and `GTAIV.VR.eye1.bmp` next to it. These local game images and logs must stay outside Git. Capturing separate color surfaces does not establish depth isolation, visibility correctness, or headset submission.

The first runtime attempts allocated 27 named targets per eye, then faulted during viewport generation before list building. PID 14244's exception was at runtime `0x000B07F9`, allocation/module base `0x00080000`, corresponding to Ghidra `0x004307F9`. Instruction bytes `0F 28 30` are an aligned SSE load; EAX was `0x645D4F58`, which is not 16-byte aligned. The mirrored viewport's C++ alignment is four bytes. Generation now runs engine setters on explicitly `alignas(16)` private viewports and matrix inputs, then copies the result into caller storage. This finding was saved/read back in Ghidra bookmark `Issue006Alignment` at `0x004307f0`. The fix remains under runtime retest; no image result is inferred from it.

The alignment retest passed in PID 8916. Frame 150 executed all three world phases for eye 0, followed by all three for eye 1, on render thread 4000. The builder log recorded callback counter 150 both before and after. Both final-color surfaces were read back: hashes `F21BD74ECE66F760` and `87D39B1DD5F0E2F0`. Inspecting the images showed distinct, asymmetric apartment world views, including the near left pillar, Niko, door, furniture and kitchen. A fresh native desktop capture showed normal world rendering after the single test frame. Bridge status remained responsive with OpenXR running SYNCHRONIZED; RPC quit exited normally. No outdoor visibility, independent depth pixels, continuous temporal correctness, reset compatibility, or headset image is established by this run. A bounded INTZ-to-R32F shader readback is the next depth isolation check.

The corrected depth diagnostic passed in PID 27304, frame 147. Each eye's INTZ G-buffer 3 texture was sampled into R32F by a point-filtered shader, then read back. Each contained 2,073,600 finite depth values. Hashes differed (`386E3022C68AAFCC`, `A851C4C376DF8B3E`), with ranges 0.310420–1 and 0.313154–1. The first diagnostic read the original texture twice through the restoration hook; its identical hashes are excluded. Direct clone sampling bypasses that hook for the diagnostic and restores saved graphics state afterward.

`[VR] ExperimentalSubmit=1` enables the next test: complete D3D9 work with an event query, open both shared color textures on the OpenXR D3D11 device, acquire/wait/copy/complete/release separate eye swapchain images, and submit one projection layer. Each queued marker retains its source pose/FOV and time so the submitted layer describes the rendered images. The one-frame experiment then holds that pair; it is deliberately a frozen image test. Newly located poses are for later game rendering, so this prototype has a game-frame delay and does not establish production frame pacing. The first submission attempt rejected ordinary BGRA UNORM; SteamVR advertises the BGRA SRGB format already validated in #004. The corrected test uses that format for display-encoded final color and a compatible D3D11 resource copy. Swapchain cleanup is explicit on the render thread; no global COM destructor runs under the loader lock.
