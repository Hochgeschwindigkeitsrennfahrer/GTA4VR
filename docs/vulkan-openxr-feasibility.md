# Vulkan/OpenXR feasibility — issue 013

## Recommendation (2026-10-04)

Continue evaluating the opt-in DXVK/Vulkan eye-image bridge. Continuous headset stereo, explicit shutdown/relaunch and an initial saved-view performance improvement are verified locally. Retain the system D3D9Ex/D3D11 default until broader correctness, lifecycle and street/vehicle Release comparisons pass.

The Vulkan menu option selects DXVK through FusionFix's `d3d9.cfg` (`[MAIN] API=1`). GTA IV still produces D3D9 commands. Vulkan could replace cross-device publication and change driver command-processing cost; simulation, stereo scene work and the D3D9 frontend remain. Sampled D3D9 CPU cost must not all be labeled VR transfer overhead. See [profiling results](profiling-013-results.md).

## Local discovery results

Standalone x86 probe: [`dxvk_openxr_discovery.cpp`](../tools/bridge-probe/dxvk_openxr_discovery.cpp). It creates its own hidden 64×64 device/resources. It does not launch GTA IV, edit installed settings, copy pixels or submit headset frames.

Environment: SteamVR/OpenXR 2.17.10, lighthouse HMD, NVIDIA GeForce RTX 4070 Ti. Installed x86 `GTAIV/vulkan.dll` identifies itself in its log as DXVK v3.1.1; SHA256 `265888C31CA78DFFA290C39CB7E50BFB02762590E41927906E46FB32F01497FA`. Its generic Windows file version is not the DXVK release identifier. MSVC x86 `/W4` compilation passed without warnings using the existing static x86 OpenXR loader. Logs and binaries remain in ignored `build/bridge-probe/`.

| Check | Observation | Limit |
| --- | --- | --- |
| DXVK interop | Factory, device and texture queries succeeded; Vulkan handles and texture image retrieved. | Only this installed build tested. |
| SteamVR | Both Vulkan OpenXR extensions advertised; API 1.0 accepted. | Other runtimes untested. |
| Plain initialization | Four requested instance extension names absent; runtime GPU selection returned `XR_ERROR_RUNTIME_FAILURE`. | Correlation does not establish the complete failure cause. |
| OpenVR bootstrap | Preloading SteamVR's existing x86 `openvr_api.dll` before DXVK factory creation enabled its extension provider. All requested instance extensions present; runtime selected DXVK's GPU. | Ordering inside GTA IV untested. |
| OpenXR session | Creation succeeded on DXVK's Vulkan device/graphics queue. | Session not begun; no frame loop. |
| Swapchain | 64×64 BGRA8 sRGB with color attachment/transfer destination usage succeeded; three nonzero Vulkan images. | No pixel, GPU ownership or reset validation. |

## Integration requirements

DXVK's [v3.1.1 interop interfaces](https://github.com/doitsujin/dxvk/blob/v3.1.1/src/d3d9/d3d9_interfaces.h) expose handles, image transitions, rendering flush and queue locking. Its [implementation](https://github.com/doitsujin/dxvk/blob/v3.1.1/src/d3d9/d3d9_interop.cpp) synchronizes the CPU command stream during flush; this is not a GPU completion fence. Lock the queue for our Vulkan submissions, make no D3D9 calls while locked, and restore source layouts before DXVK reuse. OpenXR acquisition, waiting, completion and release need explicit ownership.

The [OpenVR provider](https://github.com/doitsujin/dxvk/blob/v3.1.1/src/dxvk/dxvk_openvr.cpp) queries required extensions before Vulkan creation. The Windows OpenXR provider expects Wine's `wineopenxr.dll`, so native OpenXR discovery is not automatic through that provider. Optional OpenVR preloading is a SteamVR-specific bootstrap; actual gameplay/submission can still use OpenXR. Verify initialization cleanup and missing-runtime fallback before integration.

The probe uses the [legacy Vulkan binding](https://registry.khronos.org/OpenXR/specs/1.0/man/html/XR_KHR_vulkan_enable.html) because DXVK creates the device. Discover requirements early. Enabling `XR_KHR_vulkan_enable2` cannot retrofit features into an existing device. Required device extensions were printed, but every enabled feature was not independently validated.

Start with a same-device Vulkan copy into acquired OpenXR images. The inspected interop API does not import arbitrary OpenXR images as D3D9 targets; direct rendering would require additional DXVK integration. Do not promise zero-copy. Validate channel order, UNORM/sRGB interpretation and barriers with readback.

Reuse camera, visibility and stereo command generation. Add explicit Vulkan ownership for eye allocation/publication, runtime resources, synchronization, GPU timing and reset. Current API=1 guards protect an incompatible D3D9Ex/D3D11 path; removing them alone would repeat #006's failed experiment.

## Remaining decision gates

### Copy and game prototype update

The exact game copier in `source/includes/dxvk_vulkan_bridge.hpp` passed standalone x86 Debug and optimized Release: 32 changing-color 64×64 copies, all 4096 pixels correct per copy, both source slots exercised, all three runtime images returned, successful object teardown. It transitions sources through DXVK, waits for source writes using bounded `WaitForResource(READONLY|DONOTWAIT)`, submits under the queue lock, restores layouts and requires its Vulkan fence before release. Readback is only in the standalone probe.

Earlier flush/queue-lock-only variants produced zero or stale pixels and DXVK device-loss messages. A diagnostic D3D9 source readback improved but did not eliminate stale results. Explicit source-resource completion fixed the repeated check. Consequently the prototype retains a producer GPU wait; no removal of all synchronization overhead is claimed. An x86 Khronos validation layer was unavailable locally. Exact cause of the earlier failures remains unproven.

The opt-in game implementation uses `[VR] ExperimentalVulkanBridge=1` with `d3d9.cfg API=1`. For this SteamVR test, `VulkanOpenVRBootstrap` is the absolute path to its existing `bin/win32/openvr_api.dll`. These options default off/empty in distribution. Runtime creation waits for the real DXVK game device; Vulkan eye publication bypasses D3D9 shared handles and D3D11. The existing camera, world visibility, scene commands and reference spaces remain shared. Vulkan copy spans include producer flush/wait and copy fence; they are CPU wall-clock costs, not GPU durations. A pending copy timeout quarantines runtime objects rather than freeing images still in use.

Initial GTA IV run created the Vulkan session successfully but exited during eye resource setup. Inspection found an unguarded Ex-interface dereference in presentation-hook installation; this was corrected. Subsequent runs lost the Vulkan device, leaving a black desktop `Load Successful` frame and headset `Waiting`. A zero-layer OpenXR session with stereo disabled also lost the device while driving, so the failure is not isolated to eye copies. VR-disabled DXVK rendered the apartment and exited through the bridge in the observed comparison.

`DXVK_DEBUG=hang` confirmed a GPU hang but provided no failing-command details; `VK_KHR_device_fault` was unavailable. Disabling descriptor heap and descriptor buffer selected the legacy binding model, but it still lost the device after one stereo submission. These comparisons do not establish a driver bug or a descriptor-mode cause.

Source review found missing external queue synchronization around runtime calls. The [OpenXR Vulkan concurrency requirements](https://raw.githubusercontent.com/KhronosGroup/OpenXR-Docs/main/specification/sources/chapters/extensions/khr/khr_vulkan_enable.adoc) require synchronization for `xrBeginFrame`, `xrEndFrame`, `xrAcquireSwapchainImage`, and `xrReleaseSwapchainImage`. DXVK submits asynchronously; locking only our `vkQueueSubmit` was insufficient even for a zero-layer session. The prototype now wraps those four calls in DXVK's queue lock and uses a scoped unlock for our submission. Wait functions remain outside the lock, and callbacks must not call D3D9.

Retest Release `CCAC483C5DE0E7E65AFF7F3570538C244EF70F77C860A7BA9AA5DE8487158704` with default descriptor heap and no hang diagnostic rendered continuously for thousands of frames; the user confirmed live, correctly oriented images in both eyes and head movement. Bridge quit completed runtime teardown with successful destroy results and actual GTAIV process exit. Relaunch again reached focused stereo. The earlier hangs have not recurred in these runs; reset, runtime-loss and broader gameplay acceptance remain open.

Initial uncontrolled street CPU-only capture (`vulkan-street-initial-diagnostic`) measured2030 submission intervals: mean14.7629ms, median14.4022ms, p95 20.8709ms. Eye command medians5.4642/5.0351ms; copy spans1.2737/0.2775ms median. OpenXR stayed FOCUSED and reported6257 stereo layers after capture, with no device loss in the fresh DXVK log. This view is not matched to the earlier D3D9 capture and cannot establish a speedup; it still misses the11.11ms target on many frames. The copy span includes source completion and queue synchronization, not just image bandwidth.

A Vulkan/system/Vulkan saved-apartment comparison then measured mean submission intervals11.1232/14.4609/11.1144ms, p95 12.1646/18.6599/12.2109ms. Approximate90 versus69 submissions/sec and23% lower mean interval are verified in this held view, with lower per-eye command-dispatch costs. The headset was resting at an unusual angle close to the body; screenshots and native model counts support approximately matched content, not a representative gameplay route. No builds/screenshots ran during each30second CPU-only capture. Both runtime focus and default descriptor bindings were maintained. See the [comparison and limits](profiling-013-results.md#vulkan-prototype-comparison).

1. **Passed:** known-color D3D9 → Vulkan OpenXR copy/readback, repeated ownership and teardown outside the game. Device reset/loss stress remains open.
2. Compare warmed flat-screen DXVK startup/correctness and timings with system D3D9, preserving configs and equivalent scenes.
3. Opt-in Vulkan stereo and both-eye user checks now pass, as do explicit shutdown and relaunch. Check outdoor effects, reset, runtime loss and missing headset/runtime fallback before promotion.
4. Compare Release apartment/street median and p95 frame intervals, command cost, GPU work and synchronization at identical settings. Separate shader compilation from warmed measurements.

Switch the supported default only if real frame times improve without correctness/lifecycle regressions. Discovery establishes feasibility, not native 90 FPS or GTA IV compatibility.
