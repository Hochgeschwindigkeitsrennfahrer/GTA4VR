# 013 — Profile and improve the native-refresh frame budget

## Goal

Explain and reduce the remaining scene-dependent frame-time cost now that #006 lets OpenXR pace stereo and removes desktop VSync. Target the configured 90 Hz headset with measured evidence. Roadmap stage 5; coordinate camera/auxiliary validation with #011.

## Tasks

- [x] Diagnose DXVK-specific grain/noise reported on some textures in the right eye; compare D3D9 producer eye readbacks with headset output before backend promotion. Normal-map mip filtering differed between eyes; scoped state restoration passed captures and user headset checks on affected surfaces. Broader material/weather coverage remains in compatibility validation.
- [ ] Decouple VR eye-target dimensions from desktop resolution: query OpenXR's recommended per-eye size, add an explicit render scale, and resize the dependent G-buffer/post-processing/viewport resources consistently. Measure a held DXVK scene before/after with full runtime FOV preserved; distinguish pixel-cost savings from CPU draw/state/simulation cost. Do not crop a 16:9 image and assume its side pixels are unused.

- [ ] Establish a post-#014 baseline (`82d2e57`) before controller work. Keep accepted room-scale collision, visibility and render-command capacity fixes enabled.
- [ ] Compare warmed stationary apartment and street/subway views before transitions and driving. Capture SteamVR timing alongside internal spans; distinguish compositor misses from application submission intervals.
- [x] Implement the first bounded profiling capture and percentile summaries, with CPU spans, delayed D3D9 GPU timestamps and ordinary desktop Present attribution. Verify capture lifecycle, bounds and concurrency; runtime coverage remains to be checked.
- [ ] Add bounded timing attribution for game update/list building, per-eye D3D9 rendering/fence, D3D11 transfer, xrWaitFrame and desktop presentation. Separate intentional runtime wait from useful work and stalls.
- [ ] Measure a fixed apartment/street/vehicle route in Release. Record median/p95/p99 frame time, missed submissions and streaming spikes with graphics settings and headset/runtime versions.
- [ ] Determine whether observed drops come from CPU work, GPU work, interop synchronization, streaming, a safety limiter or runtime throttling; do not infer a cause from submittedHz alone.
- [ ] Follow elevated street CPU samples into native draw/state-update paths: quantify duplicate vertex/pixel constant updates and preserve stereo/shadow overrides when testing any reduction. Investigate pre-render OpenXR pacing separately, with thread/resource ownership and pose-age evidence.
- [ ] Evaluate the FusionFix DXVK/Vulkan backend with a bounded feasibility spike: identify the installed x86 DXVK build, measure warmed VR-disabled D3D9-versus-DXVK in the same scene, query DXVK device/texture Vulkan interop interfaces and x86 SteamVR Vulkan binding support. If feasible, prototype a same-device Vulkan eye-copy/submission path before changing the supported backend; require synchronization/reset/image correctness and measured Release gains.
- [ ] Implement a scoped improvement supported by that profile. Preserve one simulation update and correct synchronization; do not remove game loading/cutscene/minigame safety limits blindly.
- [x] Validate initial x86 Vulkan discovery: installed DXVK v3.1.1 device/texture interop, SteamVR-required instance extension bootstrap, matching runtime GPU, same-device OpenXR session and swapchain. This is the discovery subset of the backend evaluation above; copy correctness and performance remain open.
- [ ] Repeat the route and flat-screen regression. If the hardware/settings cannot meet 11.11 ms, document the measured limit and usable settings without claiming all-scene native FPS.

## Done when

The measured bottleneck is explained, an evidence-supported improvement is verified, and the repeatable route has a published frame-time distribution and native-refresh coverage or explicit hardware/settings limitations.

## Notes / evidence

Further performance work is shelved at the user's request on 2026-10-05. Keep this issue open; do not treat it as accepted or resume it for unrelated work. The [checkpoint](../../docs/performance-013-checkpoint.md) summarizes the working installed path, disabled experiments, retained shaders/tools/patches, local evidence and recommended next steps. All current work is being committed for a clean working tree; the installed game is left unchanged.

- Added explicit sparse draw attribution (`profile-start-cpu-draws`, client `-Draws`) after the CPU/GPU cross-check. Counts DrawPrimitive/DrawIndexedPrimitive calls, requested primitives/indexed vertex ranges and failures per eye/phase; times roughly1/64 original device calls with positions rotated across frames. No per-call locks, state getters or timers on ordinary draws, and no draws suppressed. Device-call wall samples exclude native preparation and may include driver waits; UP/bypassing calls are not covered. Six paged `profile-draws` results fit the existing bridge limit. Epoch restart/stale batch, mode isolation, invalid bounds/input and units/payload tests pass. x86 Debug/Release pass after correcting the Windows max macro conflict; PowerShell parser and diff checks pass. Deployed Release6F36EA8E91A122CF95471929177B9710D02C95AF10DAFB93E00C24644E4BE6DE after bridge quit/actual exit, with ASI/INI backups and matching new ASI/PDB in ignored build/issue013-draw-sample. Live warm-up and ordinary/draw/ordinary overhead control are pending; no performance improvement claimed.

- CPU/GPU cross-check after user skepticism: ReleaseB6D5A3C3/PID24788/render thread19852, held street confirmed, FOCUSED/no drops/constant observer off/2560×1440. GPU-query-on and CPU-only controls mean27.13/26.37ms, eye command medians11.17+10.44 /10.92+10.15ms, pre-wait21.73/21.21ms; OpenXR frame-wall1.86/1.80ms. Render thread96.81/97.25% of one logical CPU. Windows GPU Engine graphics_1 game means31.93/32.52%, peaks35.40/33.78%, with32 samples/run; no saturated reported competing engine. GPU timestamps11.50/10.69ms include command-starvation opportunity and are not utilization. This supports CPU-side native/DXVK command throughput or serialization rather than continuous GPU saturation, while useful work versus spinning remains unresolved. Two live-view runs cannot isolate the small query-on difference. Source shows two world streams, shared auxiliary phases only on eye0; no third desktop world pass established. Report/counter semantics/ignored raw filenames documented; no new performance improvement claimed.

- User headset check with ReleaseB6D5A3C3: “yes, it looks like it's fixed now.” Final held view captured at22:44+02:00 into ignored build/issue013-normal-mip/confirmed-after, with fresh final/G-buffer images and draw-state records. Noise fix is accepted for the tested affected surfaces; broader graphics compatibility and #013 performance route acceptance remain open. This correction establishes image-quality evidence, not a new frame-time gain.

- Noise isolation/fix experiment on2026-10-04: affected gray road captured in a vehicle at22:30+02:00; G-buffer1 already shows much stronger right-eye grain, including after observed native packed-normal decoding. Albedo/G-buffer0 and specular-color/G-buffer2 are comparatively clean. Bounded actual-device draw-state diagnostic Release6A386E9A (Debug/Release passed) captured a second affected view:64 matched shader/used-texture tuples;14 had sampler1/2 MIPFILTER=LINEAR(2) in eye0 versus NONE(0) in eye1. Recorded render-state samples matched (no sRGB-write, dither or blend discrepancy). This supports inherited sampler state between eye passes rather than a Vulkan-copy defect; the diagnostic is incomplete beyond64 tuples/eye and8 sampled texture slots.
- Implemented a scoped G-buffer entry correction: save actual pixel-sampler mip filters before eye0 and restore differing filters before eye1, without changing the native software cache, camera constants or requested filtering policy. Deployed ReleaseB6D5A3C31D7C8FFA75AA507EF692CABEB6DA4447D3AACBFBC3C4D9714ED476F5 after x86 Debug/Release and bridge quit/actual exit; ASI/INI backups and matching new ASI/PDB under ignored build/issue013-normal-mip. PID24788 reached FOCUSED continuous DXVK-Vulkan rendering. Warmed capture has64 matched sampled draw records and zero used-sampler differences; fresh right-eye final image shows a rendered street/crossing. User headset confirmation and an affected-surface image comparison are pending. No performance gain or complete graphical compatibility claimed. Native shader/texture assets remain out of Git.

- Observer overhead control on2026-10-04: Release9372B6D8, PID30048, held street/2560×1440 per eye/SteamVR2.17.10/90Hz/DXVK3.1.1, consecutive CPU-only observer-on/off/on. Mean submission intervals20.02/16.75/20.41ms, medians19.82/16.61/20.17ms, p95 21.63/18.10/22.28ms; FOCUSED and no dropped samples. Detailed observation adds about3.3–3.7ms in this view. It is now explicit via `-Constants`; ordinary captures leave it off. Inactive gameplay already had it off, so no gameplay optimization gain is claimed. The client switch/result naming collision was fixed; B was recovered from the completed epoch, A1 excluded, A2/B2 saved normally. See the profiling report for limitations and local filenames.
- Right-eye road noise remains unresolved and appears before Vulkan transfer. Added bounded lighting pixel shader logs and G-buffer0/1/2 readbacks to isolate its stage; no shader repair or call suppression. Debug/Release passed, deployed Release5044DA143D94CDCE0116C4E72B978EEEFB97924CDC467D45A0F055F1C20C6F57 after bridge quit/actual exit, preserving prior ASI/INI and matching new ASI/PDB in ignored build/issue013-lighting-input. First launch stopped at Rockstar's error dialog before new ASI evidence; Retry reached PID29564/FOCUSED continuous stereo. All six intermediate files captured at2560×1440/format21 (A8R8G8B8), with no readback failures. Awaiting a held affected road view for comparison; these initial images alone do not establish the noise source. Fresh window capture showed a rendered interior view with desktop overlays; no flat-screen regression or new headset quality acceptance claimed.

- Elevated DXVK street trace completed by the user: `dxvk-street-20261004-212510.etl`, PID14212/render thread25616, Release66010,2560×1440 eye targets, CE1.2.0.59/SteamVR2.17.10. FOCUSED before/after, no command-buffer overflows; ETL50.76seconds includes rundown, first30seconds analyzed, zero lost events/buffers. Render-thread28717 sampled stacks: native game45.17%, unresolved module36.48%, kernel6.38%, ASI5.55%. The unnamed stack module matches DXVK's image span, but exact attribution remains provisional. Independent all-thread module report resolves GTAIV33.81%, DXVK24.24%, NVIDIA Vulkan driver13.25%, ASI2.76% of70.46seconds sampled CPU weight. These are execution shares, not frame-wall-time shares or a matched backend comparison. Matching installed ASI PDB was overwritten by a later build; function-level attribution is limited. Native capture helper now records installed ASI SHA256 and warns about incomplete WOW64 module enumeration; PowerShell syntax and diff checks passed. Details/limits in `docs/profiling-013-results.md`.
- Fresh native window capture and stereo producer readbacks show the user-held street/vehicle crossing view. Crossing paint and road are noisier in the right eye before Vulkan transfer. Preserved uniquely named image pair/crops locally, outside Git. Installed saved FusionFix config has AmbientOcclusion=0, weakening AO as a cause; prepared bounded AO bypass is not deployed. Reflection/specular lighting and filter inputs remain leads, not verified causes. Game remains continuously rendering; no graphics setting or rendering behavior changed for this investigation.

- The user reports continued substantial SteamVR CPU Late Start and CPU graph near11ms, then corrects a new DXVK texture grain/noise observation to the right eye. Initial both-eye confirmation establishes live rendering but does not close graphical compatibility. Added render-before-wait, frame begin/view-location, per-runtime-call DXVK queue-lock acquisition, runtime callback wall time and pose prediction timestamp lag metrics. Same-thread boundary consumption, inactive/stale epoch and lifecycle/concurrency tests pass; Debug/Release builds pass with existing PDB/test module-option warnings. No pacing or rendering behavior changed in this instrumentation. Fresh noise reproduction/capture and runtime timing attribution pending.

- Vulkan queue-fix acceptance update: the user confirmed live, correctly oriented stereo in both eyes and head movement. Explicit bridge quit completed all runtime destroy calls and GTAIV actually exited; controlled Vulkan/system/Vulkan relaunches passed. Saved-apartment held-view CPU-only captures on the same Release: mean11.1232/14.4609/11.1144ms, median11.1235/13.6177/11.1009ms, p95 12.1646/18.6599/12.2109ms; interval counts2695/2073/2697, no dropped samples, FOCUSED throughout. Vulkan shows about23% lower mean interval in this diagnostic view (roughly90 versus69 submissions/sec). Headset resting close to the body/unusual angle limits generalization; fresh screenshots and native model counts support approximately matched content. Eye command medians2.82/2.52 vs4.21/3.97 vs2.87/2.64ms support reduced frontend command cost. No GPU queries/builds/screenshots during captures; no fixed limiter activity. Raw captures ignored; tracked details in `docs/profiling-013-results.md`. Uncontrolled street Vulkan mean14.7629ms/p95 20.8709ms still misses target. Reset, failure/fallback and matched street/vehicle comparisons remain open. Supported default unchanged; local prototype left running continuously.

- 2026-10-04 Vulkan game diagnostics: default descriptor-heap DXVK lost the Vulkan device at the black `Load Successful` frame before eye publication. OpenXR enabled with stereo disabled also lost the device while driving; VR-disabled DXVK rendered correctly and bridge quit exited in the observed comparison. `DXVK_DEBUG=hang` confirmed a hang without a failing-command report. Disabling descriptor heap/buffer selected legacy bindings but still lost the device after one stereo submission. Preserved logs locally; temporary `dxvk.conf` removed. User reported a cutscene during one closure, but that does not establish the cause of the earlier device losses.
- Found and corrected missing external queue synchronization: DXVK's worker thread and OpenXR begin/end frame and acquire/release calls were accessing the same Vulkan queue. Added scoped DXVK queue locking around all four runtime calls and our submission; keep waits outside the lock and prohibit D3D9 calls inside. Debug/Release builds pass (existing dependency PDB warnings); both rebuilt standalone probes pass all32 pixel copies and teardown. Deployed Release `CCAC483C5DE0E7E65AFF7F3570538C244EF70F77C860A7BA9AA5DE8487158704`, PID24972, with default descriptor heap and diagnostic mode removed. FOCUSED status shows1071 stereo layers, no failure; fresh image shows street geometry. This is initial recovery evidence, not lifecycle/performance acceptance. Timing capture and headset confirmation pending.

- Vulkan copy/prototype update: standalone x86 Debug and optimized Release pass 32 changing-color copies/readbacks (4096 checked pixels each), alternating source slots and all three swapchain images; teardown succeeds. The probe exercises the actual game copier. Flush/queue-lock-only variants failed with zero/stale pixels and device-loss logs; retain bounded DXVK source-resource completion plus copy fence. No x86 validation layer available; no performance benefit claimed. Added opt-in game Vulkan binding/copy, ordinary D3D9 target allocation, pinned public Vulkan header submodule v1.3.231 (`98f440ce6868c94f5ec6e198cc1adda4760e8849`), default-off config and status backend attribution. Debug/Release compile; CI recursive submodule checkout already covers the dependency. Backups in ignored `build/issue013-vulkan-baseline/`. First live attempt created the DXVK OpenXR session but exited at eye setup; corrected an Ex-interface dereference in presentation-hook setup before retrying. Game correctness/performance/lifecycle gates remain open.

- Vulkan discovery (2026-10-04): standalone MSVC x86 `/W4` probe using installed DXVK v3.1.1 and SteamVR/OpenXR 2.17.10 on RTX 4070 Ti. Interop queries succeed. Plain DXVK lacks four runtime-requested instance extension names and GPU selection fails; optional preload of SteamVR's existing x86 OpenVR library activates DXVK's extension provider, matches the GPU and permits Vulkan session plus 64×64 BGRA8 sRGB swapchain creation (three images). No installed settings/ASI changed. No image copy, frame submission or speedup tested. [Research/recommendation](../../docs/vulkan-openxr-feasibility.md), [reproduction](../../tools/bridge-probe/README.md). Proceed to standalone same-device copy correctness before deciding whether to integrate; #013 remains open.

- 2026-10-04: the user prioritizes profiling before new gameplay features. The accepted #014 build easily reaches 90 FPS in the starting apartment but drops below 90 FPS on the street. Preserve correct geometry during optimization. Baseline ASI SHA256 `7F3520C08C00316E8049E44902B942B7BDA879F2646CE967E15B68FF0D586530`.
- A fresh SteamVR 2.17.10 timing capture shows prominent CPU Late Start bands and little application GPU time in that snapshot. This is a lead, not an outdoor bottleneck diagnosis.
- [Profiling procedure](../../docs/frame-profiling.md): bounded 30-second CPU wall-clock distributions via the development bridge, plus delayed per-eye D3D9 GPU timestamps and ordinary Present attribution. Command dispatch/fence waits are not GPU duration. Zero samples indicate a path/query was not observed; no GPU duration should be inferred from CPU waits.
- Instrumentation x86 Debug and Release builds passed (existing deprecated-event and dependency-PDB warnings). Standalone x86 tests passed for known median/p95/p99 values, 11.11 ms budget counts, concurrent recording/8192-sample capacity, restart isolation, stop and unknown metric rejection. PowerShell capture client syntax passed.
- Deployed Release instrumentation ASI SHA256 `901873FA17F9E2445A30F37DB6FDFC8A09E97E1160191A8CDCA5E7CC4673D8ED` after bridge quit and confirmed process exit. Preserved the accepted ASI/INI/backend config in ignored `build/issue013-baseline/`. Disabled installed `ProbeFirstPersonAnchor` periodic file logging for captures; gameplay/graphics settings unchanged. GTA IV CE executable 1.2.0.59, SteamVR/OpenXR 2.17.10, 90 Hz, system D3D9Ex; runtime log confirms 2560×1440 eye images. Apartment/street focused measurements pending.
- Runtime smoke test (PID26036, SYNCHRONIZED with zero stereo layers) confirmed CPU metrics and delayed D3D9 GPU queries: 2069 CPU samples and2068 samples per GPU eye, no sample-capacity drops. Capture stop and unknown metric rejection worked. This unfocused/background run is instrumentation coverage, not the apartment performance baseline. Ordinary Present had no samples, so PresentEx timing was added with nested-call deduplication; its x86 Debug and Release builds passed.
- Device Present/PresentEx still had zero samples. The final instrumentation installs presentation hooks on the actual world device and its swapchain, avoiding startup dummy-device ambiguity and covering swapchain presentation directly. Final x86 Debug/Release builds passed; deployed ASI SHA256 `6A4B83A56C8E42AB5A127F86DF055FF50BC0CF83564AE20D657F49A3A2375E4D`.
- SteamVR temporarily reported a disconnected headset display cable; the user fixed the connection. The old session fell back after loss, so it was closed through the bridge and relaunched before focused measurements. Do not use the disconnected/unfocused captures as gameplay baselines.
- Final runtime coverage passed in PID32748: 1481 desktop presentation samples and1484 delayed eye0 GPU samples in a short warm-up capture, no capacity drops. Direct swapchain coverage resolves the missing desktop samples. Session remained SYNCHRONIZED with zero stereo layers, so these data verify instrumentation only. The game remains running, profiler stopped, continuous stereo enabled; focused apartment/street captures are the next step once the headset is worn.
- The user subsequently reported a black headset view. PID32748 kept publishing D3D9 eye pairs without command-buffer overflows, but SteamVR stayed SYNCHRONIZED, returned `shouldRender=0` and accepted zero layers. The compositor log repeatedly recorded `NvAPI Invalid refresh: 0` / `NvAPI::WaitForPresent failed` after the earlier display-cable disconnect. This supports runtime/display recovery investigation; it does not establish a profiling-render regression. Closed GTA IV through the bridge, verified exit, closed SteamVR normally and restarted it before relaunching the game. No performance baseline collected in this state.
- After runtime restart, PID11448 reached VISIBLE and `shouldRender=1`, but eye resource creation repeatedly failed at `FullScreenCopy` with D3D error `0x8876017C` (out of video memory). `nvidia-smi` reported RTX4070Ti 12282MiB total/4475MiB used/7520MiB free; GTAIV virtual allocation was about2.35GB. Executable is large-address-aware. Free VRAM does not prove sufficient address space/driver allocation availability. The process subsequently disappeared before a bridge quit; cause not captured. Retrying a fresh game device before profiling.
- Headset rendering recovered; the user confirms a correct headset image. PID17808 is FOCUSED with continuous eye submission, no command-buffer overflows, same final Release instrumentation ASI. First 30-second capture is on the street (user confirmation), saved locally as `build/profiles/focused-current-baseline.json`: 2138 submission intervals, mean14.0191ms/median12.2599ms/p95 21.3347ms/p99 23.9671ms; 1795 intervals exceed11.11ms. These are application submission intervals, not compositor missed frames. Eye0/1 GPU-timeline medians4.1349/3.4959ms and p95 7.5633/6.8762ms; command-dispatch medians3.8598/3.5482ms. First-eye acquire/wait/copy/release span p95 6.8336ms versus0.2839ms for eye1. No profiler capacity drops. Need stationary repeat, apartment comparison and finer runtime-wait attribution before diagnosing the bottleneck; GPU timestamp intervals may include starvation, not just GPU busy time.
- The user reports neither CPU cores nor GPU utilization are close to 100% during the outdoor slowdown. These are user-observed utilization readings, not a captured timing trace. Include synchronization stalls, CPU thread dependencies, streaming and runtime throttling in the investigation; low utilization alone does not distinguish them.
- The user observes that the outdoor drop is likely game performance rather than a cap. This is consistent with scene-dependent rates after IMMEDIATE presentation and gameplay-limiter bypass, but CPU/GPU attribution is not yet measured. Prioritize profiling the route rather than increasing an arbitrary limiter.
- #006 final Release ASI SHA256 `CED41E3039DC0F4DEF56ECAAB8EED580DFD5C135B803FE749FB2BE04561009F1`, GTA IV CE 1.2.0.59, SteamVR/OpenXR 2.17.10, RTX 4070 Ti, system D3D9Ex → D3D11, 2560×1440 per eye. Actual desktop presentation is IMMEDIATE, and xrWaitFrame supplies pacing. Two five-second samples in PID 31412 measured 90.00 layers/sec; after reboot PID 16664 sampled 89.80 and later 49.80 layers/sec while FOCUSED. These are coarse projection-layer rates, not unique compositor frames or percentile/GPU timings. The later drop's cause is unmeasured.
- The user's 120 FPS FusionFix menu preset remains saved for ordinary gameplay; active rendered stereo bypasses that limiter. Existing apartment-exit stutter was reported before the stereo/pacing change.
- Stationary repeat street capture: median submission interval11.1968ms/p95 16.5458ms. Apartment captures instead measured median17.16/17.20ms and p95 19.88/19.54ms (1730/1734 intervals). The user notes the graph contradicts the initial apartment90FPS assumption; retain these observations without claiming a universal apartment advantage. Actual Present median0.26ms and xrWait median0.02ms in the apartment. The user raises a possible60FPS cap; direct limiter-branch timings are being added to verify the pacing bypass and preserved60FPS safety path, rather than inferring a limiter from58 submissions/sec alone.
- Ghidra/runtime verification showed the original process-hook target at004016A0 is a live RET stub (PID28652, base006D0000, runtime006D16A0). Initial gameUpdate near-zero samples cannot establish simulation cost. Persisted an issue013 bookmark, read it back and saved GTAIV.exe in the Ghidra project. New profiling labels separate processStub and FusionFix processCallbacks; real simulation timing remains open.
- 2026-10-04 limiter verification: x86 Debug/Release and profiler lifecycle/percentile/concurrency checks passed; deployed Release ASI SHA25698BE0512E34C9A9C938C99FCC0488C1D1BDFF44C0D67E0D9D5D0CBC952D3F34F with the prior profiling ASI preserved. PID4176 remained FOCUSED before/after the30-second apartment restart capture, 2344 submission intervals (~78/sec), median12.00ms/p95 18.28ms. All2346 observed limiter-hook calls took the active OpenXR pacing bypass; gameplay, cutscene, loading, loadingSafety60 and minigame limiters each had zero calls. This run has no fixed60FPS ceiling; the earlier58/sec alone did not prove a cap. Actual desktop Present median0.23ms/p95 0.31ms. Acquire/wait per eye was below0.01ms at p95; first-eye D3D11 copy/completion span p95 6.82ms versus0.34ms for eye1, locating the earlier xrCopy tail in copy completion rather than swapchain acquisition. No optimization yet; held view differs from prior captures and cannot support a speedup claim. Raw capture remains ignored at build/profiles/apartment-limiter-verification.json.
- 2026-10-04 continued street profiling, PID4176/current Release98BE0512: user confirmed street and actual1080p; fresh allocation log confirms finalColor1920x1080. Do not carry forward earlier1440p settings as the current configuration. Warm street capture current-copy-wait-repeat:1823 intervals, median16.19ms/p95 18.72ms; eye0/1 commands median4.90/4.59ms, first-eye copy completion p95 4.29ms. No limiter calls. Fresh SteamVR timing screenshot shows substantial CPU Late Start and little submitted D3D11 GPU scene time, which does not account for the separate D3D9 producer.
- Controlled user-selected720p confirmed by finalColor1280x720 allocation log. First street-720p-scaling capture overlapped local compilation and is diagnostic only. No-build repeat street-720p-clean-repeat:1783 intervals, mean16.81ms/median16.67ms/p95 19.08ms; eye0/1 commands median4.86/4.60ms, GPU timeline5.34/4.43ms, first-eye copy completion p95 2.89ms. Lowering pixel count to44% did not meaningfully reduce eye dispatch time or total submission time in this held view. This argues against a dominant pixel/fill bottleneck here, but is not a complete CPU diagnosis; traffic/time vary. Restore1080p and compare before deploying the finer phase metrics.
- Restored1080p street capture confirms mean15.74ms/median15.21ms/p95 18.78ms, slightly faster than clean720p; no meaningful benefit from44% pixel count in this view. Original resolution restored and allocation verified. Added tracked summary docs/profiling-013-results.md with limitations and comparison table.
- Finer profiler Debug/Release and bounded tests passed. Deployed Release9DC81C6860C14F3DA16099A59E78AA4A47F2963B7B107D34D18126ED06FABC68 after bridge quit/verified exit; relaunched PID24772 to apartment. FOCUSED capture apartment-phase-breakdown:2293 intervals, mean13.0728ms/median12.0620ms/p95 18.6128ms. G-buffer CPU median2.4320/2.2722ms; scene drawing1.2920/1.1896ms; lighting~0.07ms per eye. First-eye completion polling p95 6.7421ms versus query-create0.0118ms, Flush~0.06ms, CopyResource<0.01ms. Thus event-query allocation is not the observed transfer tail. Next compare CPU-only capture against D3D9 GPU timestamp queries to quantify measurement overhead before choosing a render optimization.
- CPU-only query control verified in Release0051B8D6F85CC3A46DD037CE74BE8C125B6885D169415E4644B09145E5ECC52C, PID21284. Same apartment hold, FOCUSED throughout: GPU-on1 mean12.47ms/median11.70ms/p95 18.39ms, CPU-only mean12.72ms/median11.89ms/p95 18.55ms, GPU-on2 mean12.10ms/median11.49ms/p95 18.19ms. CPU-only had zero GPU samples as intended. No meaningful query-induced slowdown demonstrated; retain GPU timing as available diagnostic. No screenshots/builds during these captures.
- Added coarse per-thread CPU deltas to the capture client. apartment-thread-cpu window31046.08ms: thread30312 consumed29718.75ms CPU (95.72% of one core), thread17844 consumed26390.62ms (85.00%), thread8284 consumed6937.50ms (22.35%). Fresh queued marker logs confirm30312 is the render thread. This measures CPU residency, including spin/poll work; it is not useful-work-only cost or physical-core occupancy. Another thread17844 remains unidentified. Individual core utilization can look lower with migration. Next measure aggregate alias-routing lookup cost and render-thread time between eye streams; do not infer complete native simulation timing.
- Routing profiler Release788096083E62E135D28088C68613C7A6119FAAF49D52038467086F4AD78C40F5, PID30216. Debug/Release and aggregate epoch/one-publication tests passed. Apartment lookup median0.11/0.10ms, outside-eye gap4.42ms/p95 11.57ms, inter-eye gap0.04ms. Street-routing-thread-baseline (user-held street, FOCUSED, no builds/screenshots):2009 intervals, mean14.9202ms/median14.0344ms/p95 18.6372ms; eye commands5.81/5.32ms, routing0.12/0.12ms, first-eye completion polling p95 5.71ms. Top thread22020 consumed30062.50ms/96.74% one core over the coarse window; thread9196 79.25%. Native rendering plus synchronization remains the target; lookup cache alone cannot recover the missing budget.
- Installed Windows Performance Toolkit found, but direct xperf CPU trace failed Invalid flags (0x3ec), and WPR CPU profile failed to enable system-performance profiling policy (0xc5585011). No trace was started and WPR-status confirms not recording. System policy unchanged. Native CPU stack attribution remains unavailable through ETW in this session. Proceeding with bounded nonflushing D3D11 completion-poll comparison; explicit End/Flush, completion requirement and timeout remain. Debug/Release and stop-restores-polling tests passed; deployed Release0A89B250AD02F65855F14B836BF3168C7B252E38770B4B46B930DAC748344074.
- Bounded D3D11 nonflushing poll control ran in Release0A89B250AD02F65855F14B836BF3168C7B252E38770B4B46B930DAC748344074, PID2844. Normal1 / noflush / normal2 mean intervals14.17/14.09/14.63ms; p95 18.62/18.61/18.70ms; first-eye polling p95 6.44/6.53/6.18ms. No meaningful improvement demonstrated, no synchronization failures observed. Ordinary polling remains the default and restored after capture. The noflush diagnostic retains explicit Flush and successful completion before release.
- Confirmed this process is not administrator. The user offered elevated execution; prepared tools/capture-native-cpu.ps1 with elevation/existing-recording/focused-stereo guards,30second recording and finally-stop, storing trace and module-base metadata only in ignored build/profiles/native. Syntax and non-admin refusal passed. Requested user run the helper elevated in a held street view; successful CPU sampling is pending.
- The user reported sustained 250ms stale-pose desktop fallback before running the elevated trace. PID2844 remained FOCUSED/running but reported only2.38 submissions/sec; stereo builtFrames27116, command-buffer peak612688 of8388608 bytes and zero overflows. Profiler inactive, nonflushingPoll=false. Last stereo log reported fresh poses unavailable. The process subsequently disappeared before a diagnostic capture could be started; exit cause and initiating slow phase remain unknown. Do not infer an overflow or blame the bounded polling experiment from these observations.
- Relaunched the same Release0A89B250 without source/config changes, PID18212. Fresh window capture shows apartment geometry; focused continuous stereo resumed. CPU-only apartment-fallback-recovery capture:2225 intervals, median12.52ms/p95 18.46ms/max76.89ms, no sample drops;2227 runtime-pacing bypasses and zero limiter calls. No sustained stale-pose fallback in this30second window. Restart recovery is not a root-cause fix. Elevated native trace remains deferred until the user confirms the headset is usable.
- Elevated street CPU recording completed by user, street-20261004-184231.etl. ETL50.69s including rundown, zero lost events/buffers; restricted first30s/PID18212/render thread26952/Sampled Profile gives28000 samples. D3D9 exclusive28.54%, native game27.57%, x86 ntdll19.85%, ASI3.89%. Vertex/pixel constant hooks inclusive8.40/8.38%, exclusive27/35 samples: underlying D3D9 updates dominate their paths. These are sampled CPU shares, not frame-wall-time fractions. Tracked report records precise limitations and next experiments. Raw ETL/reports/cache remain ignored.
- User's SteamVR image shows substantial Late Start. Current bridge executes D3D9 eyes/publication before xrWaitFrame, so runtime graph attribution begins late relative to scene work; small runtime GPU bands do not cover all producer work. No fixed60Hz cap or native90Hz success inferred. Ghidra bookmarks0042623A (possible DrawIndexedPrimitive call, live slot identity unverified) and004319A4 (return from verified viewport update00430F00) previewed/applied/read back/saved as Issue013CpuSampling. No game/source runtime behavior changed during this analysis.
- Native rewrite/parallelization investigation: read existing Ghidra findings, then rechecked live00430F00 in PID14212/base00980000. The viewport update performs three matrix multiplies and conditionally publishes shared shader constants. Its sampled caller has4701/28717 inclusive stacks (16.37%) in DXVK, not16.37% exclusive matrix math. Rechecked004307F0 in new PID1696/same base: packed aligned SSE MOVAPS/SHUFPS/MULPS/ADDPS is already used. Added Issue013 bookmark at00430F00, read back and saved. Avoid parallel device/state mutation; isolate pure, independent batches and measure exclusive cost before replacing native routines.
- Added bounded shader-constant observer: per-eye source-call/vector/repeated-value candidate counters and aggregate original device-setter wall spans. It changes no calls, uses epoch/thread-owned totals and invalidates known VR overrides. Unobserved state-block/internal writes mean repeats are candidates only, not a safe skip rule. Deterministic exact-byte/overlap/invalidation/bounds and capture-epoch tests pass; x86 Debug/Release pass with existing PDB/module-option warnings. Deployed ReleaseA233445AB7503970FF734288A003BEA420D1B3AD10C7356E60A85868BEFBCF73 after bridge quit/actual exit, backed up installed ASI/INI and new matching ASI/PDB pair. PID1696 reached FOCUSED continuous Vulkan stereo, no command-buffer overflows. Runtime counts/call costs pending held street capture.
- Held street observer capture completed (user positioned), PID1696/render thread27924, ReleaseA233445A,1310 eye streams/1309 intervals, FOCUSED/no drops/overflows. Eye command medians9.88/9.22ms, render-before-wait19.24ms, submission22.65ms/p95 25.19ms. Setter medians0.58/0.57ms vertex and0.67/0.66ms pixel are nested in those spans. About2.5ms setter sum cannot explain/recover the whole missing budget; G-buffer5.48/5.04ms and scene3.95/3.76ms are larger. Observer costs unquantified; this is not a matched speedup/regression. AO recorded zero calls. Source call/vector totals and repeated-candidate limitations recorded in profiling report. Render thread used30.00s CPU over31.08s client window (96.53% logical CPU); no high overall machine utilization is required to hit this limit.
- Established live SSE matrix routine004307F0–004308E8, RET8 at004308E6. Earlier DXVK CPU trace has962/28717 exclusive samples in that range,3.35% across all render-thread callers, versus16.37% inclusive parent viewport path. A rewrite of this math alone has limited potential. Previewed/applied/read back/saved additional Issue013 bookmark004307F0. Native slot82 draw call rechecked, but device/vtable identity remains open. No parallel workers or skipped graphics calls added; #013 remains open.
- Sparse draw observer startup validation: initial Release6F36EA8E runs repeatedly reset the D3D9 device/OpenXR session. The first run ended with DXVK pipeline compilation and memory-allocation errors; Git also reported an allocation failure. A startup draw capture contained only four G-buffer streams and is excluded from performance evidence. The preceding accepted ReleaseB6D5A3 build also repeated resets in a comparison launch, so the new observer alone does not establish the cause. After runtime recovery reported by the user, B6D5A3/PID19376 completed over4500 stereo frames; redeployed6F36EA8E/PID27132 then completed over1300 stereo frames with zero command-buffer overflows and no recurring resets in the observed warm-up. Recovery is not a root-cause fix. Preserved failure logs in ignored build/issue013-draw-sample; ordinary profiling keeps the draw observer disabled. Proceeding with bounded validation and held-view overhead controls; no speedup claim.
- Sparse observer warm-up succeeded in6F36EA8E/PID27132:2623 streams per eye/phase, zero failed draws/dropped samples, ~1/64 timed, ordinary mode remains off. Sampled device-call means0.22–0.31microseconds; apartment submission mean11.4323ms/p95 13.3439ms. This is validation only; no user-held view or matched overhead control, no street speedup claim. Details in profiling report; requested a held street ordinary/draw/ordinary comparison.
- User-held street sparse draw off/on/off completed in6F36EA8E/PID27132: mean submission12.86/12.90/12.65ms, p95 18.17/18.13/18.18ms, FOCUSED throughout, zero drops/overflows. On-mode2324 streams per eye/phase,1.561–1.564% timed, no failed draws, device-call means0.259–0.339microseconds. Observer adds no demonstrated multi-ms penalty here; live-scene variation limits exact overhead. Actual draw API wall spans do not explain most of enclosing eye execution; investigate state/binding updates/native preparation and deferred DXVK work next, alongside first-eye copy-completion tail. This is a lighter street view than prior27ms runs, not a speedup. Final capture inactive/draw mode off. Detailed evidence and limitations in docs/profiling-013-results.md; issue remains open.
- Continuing optimization with sparse state/binding attribution: added an explicit30-second CPU-only mode for ten D3D9 categories (texture/render/sampler state, shaders/float constants, vertex buffers/indices/declarations), ~1/64 original-call timing and thread-owned per-eye publication. No calls suppressed, arguments/results preserved; VR routing/correction and direct trampoline writes excluded from timing. Debug build and state-counter bounds/epoch/mode tests pass; capture-client PowerShell parses. Release/runtime coverage and ordinary/state/ordinary overhead control pending. This is profiling infrastructure, not a gameplay speedup.
- Sparse state observer Release build passed (existing dependency-PDB/deprecation warnings). Deployed SHA2562DAD2A21B53FEBE50976E2D3023F6C1D63F80E062E0C49CC451A19CC7FD93871 after bridge quit and actual process exit; prior ASI/INI and matching new ASI/PDB preserved in ignored build/issue013-state-sample. No saved configuration changed. Runtime counter/observer-cost validation pending.
- Sparse state coverage and held street off/on/off passed in2DAD2A21/PID18996: all ten categories in both eyes, no failed calls/drops/overflows, FOCUSED throughout. Submission mean29.80/30.62/29.76ms; observer adds~0.82–0.86ms here. Native eye execution medians12.12/11.32ms off versus12.50/11.72ms on; later OpenXR frame wall~1.73ms. Constants~26k vertex/~30k pixel calls per eye, but sampled original device calls~0.044microseconds (timer overhead included); counts do not justify suppression. Report records limits and all ignored capture names. Removing five observer-only detours from ordinary gameplay before further optimization; no game-quality/FPS improvement claimed.
- Observer-only hooks now temporary: Debug/ReleaseC131BB36 passed; deployed with backup/matching symbols. PID7388 reached continuous FOCUSED Vulkan stereo; explicit start produced state counters and install log, stop/ordinary restart produced removal log and zero state pages. Final capture stopped. Five diagnostic-only detours absent from ordinary gameplay; no matched FPS gain claimed. Old held captures remain2DAD2A21 evidence.
- Followed native shader parameter dispatch live in PID7388/base00B00000:00437170–004372A9 and004372B0–004373DF, with metadata mapping loops and shader/constant-style device dispatch. Exact device/types remain tentative; static imported bytes encrypted. Existing DXVK trace exclusive ranges325/315 of28717 samples (~2.23% combined), so no high-payoff rewrite established. Previewed/applied/read back Ghidra Issue013StateDispatch bookmarks and saved program; report records addresses, uncertainty and evidence. Remaining target: native command execution/deferred DXVK work outside the observed API spans. No native code patched; #013 open.
- 2026-10-05: continued into raw DXVK instruction samples;961 render-thread samples in ETL1–2sec,328 within DXVK. Read-only loaded-module inspector maps real-device slots104/94/109 to index/float-constant code; native004261BE stack pairing persisted/read back/saved in Ghidra. Testing documented constant-buffer streamingFalse against Auto; no speedup claim. User authorized native test-location restoration; bounded simulation-thread location commands/helper/parser tests implemented, runtime validation pending. Details in profiling report.
- Location automation validation: x86 Debug/Release4D9954A3 and deterministic parser tests passed; preserved prior installed ASI/INI and matching new ASI/PDB. PID8380 reached FOCUSED continuous stereo. NaN restore rejected. Native apartment capture, queued restore of the user-authorized read-only street root, immediate native readback and8-second recapture passed:1332.324463,-534.001831,14.841158 to1332.324097,-534.001953,14.841155 (~0.00039 units); heading normalized351.429352. Fresh image confirms exterior scene (resting low headset causes body clipping, so no headset quality acceptance claimed). No save writes. Matched Auto/False experiment continuing; no speedup yet.
- Restored street Auto/False/Auto control completed with Release4D9954A3: mean submission24.413/24.582/24.766ms,p9526.029/26.267/26.567; similar native eye spans, no benefit for disabled constant streaming. Original absent dxvk.conf restored; no setting promoted. Stored position/heading readback and fresh images verified; traffic/time/pose variation remains. Continuing source constant-hook fast forwarding with marker-owned TLS; every API call remains forwarded, build/runtime acceptance pending.
- Constant-hook forwarding Release5DF3AA6A built Debug/Release with profiler lifecycle/percentile/concurrency tests passing; deployed after bridge quit/verified exit, prior4D9954A3 and matching new ASI/PDB backed up. PID7380 restored the exact native street coordinates and reached FOCUSED continuous stereo. Short selected-constant capture produced102 streams per eye and nonzero vertex/pixel calls; stop then ordinary CPU capture disables selected observation. Fresh image remains comparable to earlier resting-headset/body-clipping scene. Runtime performance comparison continuing.
- Fast forwarding capture5DF3AA6A/PID7380: mean submission24.3735ms/median24.2047/p9525.9588 versus prior controlC24.7664/24.5865/26.5667; eye command means9.4227/8.8843 versus9.5536/9.0628. ~0.39ms difference overlaps baseline scene variation; no proven speedup. Profiler stopped/all selections off/original DXVK config restored. User-elevated fresh native CPU recording requested with this exact matching ASI/PDB; game/build held unchanged. #013 remains open.

- 2026-10-05: Received/analyzed forwarding ETL with exact5DF3 PDB: render TID17544,28,962 profile samples/no loss; GTAIV50.58%, stripped32.19% (raw sample corroborates DXVK), ASI6.40%. Identified removable ordinary draw-hook/profiler-call overhead; x86 Debug/Release and focused lifecycle tests pass. Deployed12A58736, native street restoration succeeds; draw diagnostics install/count770,258 indexed calls/zero failures then remove. Tracking was lost and user restored it; across-relaunch timings do not prove speedup. Preparing same-process draw-forwarding control. Native004462F8 hotspot persisted/read back/saved in Ghidra, actual role still uncertain. See results document; #013 remains open.

- Same-session ReleaseC03D08A1 off/forwarding/off means21.7001/22.1381/22.4910ms; render-before-wait16.3051/16.6078/16.9546. Monotonic drift means no established speedup. All FOCUSED/no overflows; forwarding counters zero by design; focused isolation/lifecycle tests and PowerShell syntax pass. Ordinary draw hooks are removed, diagnostics restore correctly. Exact symbols retained; awaiting elevated `dxvk-street-clean-hooks` recording to verify exclusive-cost removal and select the next larger native/backend target. Acceptance remains unmet.

- 2026-10-05 clean-hooks ETL received: exactC03D08A1, profiler off before/after,28,548 render samples/no loss; indexed-draw diagnostic and profiler Begin sampled costs absent. GTAIV46.82%, stripped35.46%, ASI5.64%. Texture routing now largest ASI entry758hits/2.66%; native004462F8 still unverified. Added resource-owned immutable pointer cache; x86 Debug/Release065F6645, routing collision/reset/identity tests, profiler lifecycle tests and client syntax checks pass. Preserved/deployed build after verified exit; same-session cache/linear/cache comparison pending. No90Hz acceptance.

- Texture cache runtime comparison completed: initial sequence contaminated by a stuck analysis reader (confirmed/stopped/exit verified), repeated clean A/B/C without analysis work. Per-eye routing means0.2589/0.2928/0.2667 and0.2609/0.2949/0.2682ms show tiny repeatable local reduction; mean submission24.7640/24.8641/25.5496ms does not establish FPS improvement. All FOCUSED/no overflows/no metric drops, observers off. Retain bounded cache; pursue larger native/DXVK cost, no90Hz closure. Native hotspot clean-trace annotation persisted/read back/saved in Ghidra.
- 2026-10-05: bounded exact-eye entity rejection reduced same-session street submission mean26.3376/26.4456ms broad controls to19.5246ms (eye command costs ~30% lower); all FOCUSED/zero overflows. Streaming/grid/interior traversal remain broad, native classifications/distance retained; focused math/lifecycle tests and x86 Debug/Release03484B3B pass. No promotion yet: resting headset/body-clipping view is not visual acceptance, shadows/shared-query dependencies require checks. Added reversible continuous development visibility test for that validation; #013 remains open and90Hz unmet. Details/ignored captures in profiling report.
- Continuous visual-test toggle built in Debug/ReleaseC30B65AC1B5AABCE816C82290E7E867918FC0A3CFECC469BE005127FE2891C8A; preserved03484B3B ASI/INI and matching new ASI/PDB. PID8620 reached FOCUSED continuous DXVK stereo; off/on commands accepted. No change to saved configuration; headset geometry/LOD/shadow acceptance pending. Exact comparative timings above belong to03484B3B, not this later toggle build.
- User accepted continuous exact-eye culling visuals: LOD, shadows and everything look correct. Promoting it to normal opted-in VR; preserve broad streaming/interior traversal. Diagnostic comparison now uses -BroadVisibility, while ordinary capture uses the accepted tighter entity culling. Further profiling and90Hz acceptance remain open.
- Accepted optimization is normal opted-in VR in Debug/Release56A18C86; focused tests/client syntax/diff checks pass. PID12360 FOCUSED continuous DXVK-Vulkan, stored native street restoration verified. Ordinary default capture averages19.3377ms (p9521.2949), eyes7.4309/7.0466ms, render-before-wait14.5611ms, no drops/overflows. Similar to prior tighter-culling result, no extra across-relaunch gain claimed. Need fresh native CPU samples for remaining cost;90Hz acceptance remains unmet.
- Fresh accepted-culling ETL analyzed with exact56A18C86 symbols: render28,639 stack hits/no loss; native41.90%, unknown40.76%, ASI5.17%. Native462F8 now2.41%; MapTexture2.55% retains inactive stamp initialization. Added scoped unprofiled routing early returns; Debug/Release9AA25E8C and lifecycle tests pass. Identified hot private mid-hook trampoline001B3112/CE Ghidra00C1F4F1 matching water-multiplier signature; tentative bookmark previewed/applied/read back/saved. Worker25816 is mostly NVIDIA/backend activity, so generic parallelization is not justified. No frame-rate gain claimed for routing change yet; #013 open.
- Post-culling state observer9AA25E8C/PID6028 adds~1.0–1.8ms (ordinary/state/ordinary submission18.6920/20.4744/19.4608ms); all20 category pages/zero failed setters. Unprofiled submission helper measures52.806–52.958Hz without enabling profiling. Built/deployed routing-owner fast pathE5086209 with backups/matching symbols; PID1824 native street restore/fresh mirror/continuous stereo succeeds, unprofiled52.309–52.491Hz/zero overflows. No matched forwarding speedup demonstrated. Both-eye routing check pending;90Hz/closure still unmet. More detail in profiling report.
- User confirms both eyes and live scene still look correct after the routing-owner fast path. Runtime visual gate accepted for this forwarding change; no FPS gain inferred from across-relaunch rate samples. Continue into duplicated scene-command/state-binding work; #013 remains open.
- DXVK3.1.1 source review: unchanged texture/index bindings already skipped; no built-in multiview path, and current eye resources/shaders need substantial changes for single-pass stereo. No backend modified. Construction-only sphere observer saw zero queries/120 frames, so removed it. Existing ETL thread1300 shows shared SphereCull7.22% inclusive; live00431E40 contract rechecked and corrected Ghidra bookmark previewed/applied/read back/saved. Guarded scalar classification with legacy control and120-frame native comparisons prepared; x86 deterministic boundary tests and Debug pass, Release/runtime equivalence/timing pending. No90Hz claim.
- Scalar experiment Debug/Release628B00EC/PID4508: native comparisons2,142,425 and2,168,412 queries/zero mismatches. Matched legacy/scalar/legacy means19.8253/19.8136/19.6575ms, render thread~96% core throughout, FOCUSED/no drops/overflows. No demonstrated gain; legacy restored, profiler off, experimental default remains off. Preparing unmodified x86 MSVC DXVK3.1.1 with Release PDBs to resolve stripped backend costs; build/runtime validation pending. Detailed source/dependency/evidence in profiling report. #013 remains open.
- Public DXVK source build succeeded with x86 MSVC Release symbols, DLL BCC8A9708EEE; backed up installed vulkan.dll/configs and deployed after verified game exit. PID24500 fresh log confirms v3.1.1/x86 MSVC19.51.36260; native street restore, fresh comparable mirror,4,500 continuous FOCUSED stereo layers/zero overflows. Matching DLL/PDB/ASI/PDB and manifest retained locally; reproduction/rollback documented, capture metadata includes installed backend hash. Headset compatibility and elevated dxvk-msvc-street trace requested. No new performance gain/90Hz acceptance; issue remains open.
- New elevated trace received/matches both exact hashes: renderTID29580,28,542 stack hits/no loss, DXVK43.53%/native40.07%/ASI3.26%. Named binding functions (stream source4.80%, textures4.52%, indices3.66%) and constant/CS-chunk work now resolved; ownership removal not justified. Testing compiler-only /GL + /LTCG source build (b_lto alone emitted no flags, excluded). Verified explicit compiler/link flags, x86 Release4A8F369E/PDB archived; non-LTO baseline and native location captured, optimized backend deployed after verified exit. Runtime/comparison pending, no90Hz claim.
- Compiler-only MSVC baseline/LTO/baseline comparison complete: mean eye-command sum15.4802/15.1739/15.1864ms, submission21.4600/20.5125/22.0486ms, FOCUSED/no drops/overflows. B matches repeat control C; runtime copy waits vary, so no reliable LTO gain and no promotion. Restored baseline, then original GNU backend for source-build cross-check. Exact results/limitations archived in profiling report; issue remains open.
- Prepared default-off public DXVK patch for deferred index bindings, preserving COM state/locks and using the existing draw-time dirty binding path. x86 Release FD0668B5/PE014C/PDB archived, patch reproducibility verified; Debug build running, no deployment/acceptance or performance claim. Original GNU backend restored/running PID2508; timing comparison awaits FOCUSED headset. Computer Use and development workloads kept outside capture windows per user preference.
- Deferred-index prototype Debug and Release compile successfully, Debug/Release PE014C verified. Exact patch/source/DLL/PDB manifest retained locally; diff check passes. Still not deployed or measured. Focused headset input is the remaining gate for the next runtime comparison.

- Post-Windows-restart deferred-index off/on/off completed on the same x86 ReleaseFD0668B5: submission means20.7970/21.2833/20.8680ms, eye-command sums15.4383/15.8535/15.5682ms. FOCUSED/no metric drops/overflows,1920x1080 per eye, isolated timing windows. No benefit; no promotion. Original GNU265888C3 restored after verified exit and temporary dxvk.conf removed; ASI/INI unchanged. Earlier interrupted Windows runs excluded, not attributed to this patch. Exact captures/limits recorded in profiling report; #013 remains open.

- Next public-source experiment: default-off release-order store on the existing D3D9 scoped device lock; acquire/recursion/contested path/resources remain intact, Vulkan queue lock unchanged. Separate reproducible patch and actual-header concurrency test added; Debug build passes, Release/test running. This is a measured experiment, not removal of thread protection or an established bottleneck. No deployment or performance gain yet.


- Lock-order prototype and separate pinned guard-move correction pass x86 Debug/Release and actual-header concurrent/nested/move/disabled tests. Enabled unlock mov/default xchg verified in x86 assembly. Candidate509BC6FC deployed default-off after verified exit; native street restore/readback passed once loading completed. Matched comparison underway; no FPS gain/acceptance yet.

- Lock-order off/on/off complete: eye-command sums15.9066/15.4444/15.4237ms, submission22.5038/22.3369/21.5588ms. Enabled matches repeat control; no proven gain, no promotion. Original GNU backend/config restored after verified exit. Exact evidence and old test-dialog/resting-headset limits recorded. Actual-header guard-move defect separately reproduced/fixed; no GTA IV crash attribution. Next capture requires representative head-height street view; user positioning requested. #013 open.

- Head-height control/draw observer/control valid:17.5314/17.6726/17.5648ms mean submission,~57Hz controls, G-buffer~9.05ms/~73% of combined eye commands. ~5359/5431 indexed G-buffer draws per eye; observer adds~0.11-0.14ms. Initial focus-changing capture excluded; client now records comparison validity and errors after preserving diagnostic data. Script parse passed; valid three-capture runtime path passed. Live nine-field append004462E0 remained semantically tentative, persisted/read back/saved in Ghidra. No visibility reduction or new performance gain; #013 open.

- Fresh headheight-gbuffer ETL analyzed: exact628B ASI/265888 GNU backend, CE1.2.0.59/PID10276/base007E0000, FOCUSED/profiler off/no overflows. RenderTID28776 first30sec28,687 raw samples/28,538 stack hits/no loss; mapped raw native45.96%, DXVK36.60%, ASI4.16%. Nine-field append whole4.17%, matrix2.87%; ownership/callers remain tentative, updated/read-back/saved Ghidra bookmark. New report mapper validates actual module totals and rejects corrupted/missing-PID reports. Later ordinary capture53.125Hz is separate from earlier57Hz controls, not a gain. Next target: bounded binding-transition coverage before native batching; no blind resource/lock removal or shared-buffer parallelism. Exact evidence/limits in profiling report; #013 open.

- 2026-10-05 native scene-trigger investigation: user clarifies most locations reach90Hz, only some views hitch; broad42% attribution is not a cause. Resolved004462E0 as immediate dynamic-VB vertex emission via verified lock/write/unlock/DrawPrimitive helpers; viewport branch includes native constant-cache compare/copy/upload. Added bounded read-only command-prefix observer; actual5541-command partial prefix includes3850 native model callbacks008DCBF0, with model IDs now attributable. Prefixes are not per-frame/eye and repeated IDs do not prove redundant draws. New findings previewed/applied/read back/saved in Ghidra; utility Release/live scanner and snapshot path validated. No ASI/backend changes or performance gain claimed. Fast outdoor90Hz comparison requested to isolate location-dependent excess work; #013 remains open.

-2026-10-05 user-selected fast outdoor scene compared fast/slow/fast on unchanged628BASI/265888GNU backend. Valid head-height draw captures submission11.1147/20.9652/11.2313ms; G-buffer~1066/1022 ->5524/5561 ->1431/1361calls per eye, combined eye commands4.364/15.222/5.040ms. Constant calls~3.2-3.3x higher at slow courtyard. Model hashes now resolve fences, trees, procedural plants/stones and apartment geometry; native prefix confirms hundreds of distinct placements, not proven duplicate work. Improved bounded snapshot helper compiles/8live reads pass; script syntax passes. Ghidra differential evidence previewed/applied/read back/saved. Natural scene drift and observer overhead explicit; no optimization gain. Returned game to fast position for exclusive native-cost-per-frame ETL comparison. #013 remains open.

- 2026-10-05 submission-counter audit after native-slow-corona-path trace: `openxrvr.ixx` increments `stereoLayers` once after a successful `xrEndFrame` containing one projection layer with two eye views and completed eye transfers; this is an eye-pair count, not a per-eye count. `submittedHz` is a rolling approximately five-second counter rate, while `VRProfile::Submission` measures QPC intervals at the same success point. Revised WPR metadata records 30758 -> 32034 successful layers (1276 pairs), with RPC-bounded elapsed interval 30.0025827–30.106136 seconds, giving 42.3834–42.5297 submissions/sec for this particular recording. Built-frame delta is also 1276; runtime endpoints FOCUSED, no command-buffer overflows. Earlier ~47/~57 values belong to other captures, not this exact trace. This does not count compositor-generated/reprojected display updates or prove distinct pixels each submission. User reports perceived headset smoothness above 60 FPS; retain that observation and distinguish display/head-motion updates from newly rendered scene submissions. Native ETL attribution remains pending; no optimization or closure inferred from the graph.

- 2026-10-05: native-slow-corona-path ETL verified recorded PID24384/TID14092 (later-run9188 excluded),28309 raw render samples/lost0; native45.05%,DXVK36.60%,ASI4.32%. Corona draw2.10%,immediate writer4.00%,matrix2.38% exclusive; no sole-cause/per-frame-ms claim. Ghidra confirmation previewed/applied/read back/saved. User requested packaged grass/prop mod test, explicitly no integration into fork. Installed supplied v1.2 ASI6C032A33+unchanged default INI after verified bridge exit; no FusionFix source/backend changes. CPU-only off baseline recorded; PID30092 mod log PATCH_APPLIED and native courtyard restore/readback passed. Mod-on measurement pending. Report contains source/package exact hash match, launch flags, counter/trace window limits and rollback details; #013 remains open.

- Packaged grass-fix off/on/off controls completed: mean submission17.8683/20.6321/21.0497ms; paired eye-command sums12.1226/15.4004/14.9093ms. Focused/zero drops+overflows. Daylight-to-night and camera/idle-body differences prevent claiming mod regression; onA matches night offB broadly, no demonstrated90Hz recovery. Restoring separate packaged ASI for user testing; configs/source/backend unchanged. Exact limits and rollback recorded in profiling report; #013 remains open.

- Final packaged mod state: enabled, PID23736 focused continuous stereo/PATCH_APPLIED, courtyard restore and fresh live rendering verified. No source integration or proven performance improvement; #013 open.

- User requests retaining the separately installed grass/prop mod; keep its packaged defaults enabled during subsequent optimization. Added default-off five-second model-command observer to distinguish actual main-eye callback execution from the earlier partial command-prefix samples; fixed512 rows, phase/thread scope, IDs/hash/counts/inclusive QPC time, no renderer decisions changed. Native entry008DCBF0 thiscall/plainRET verified live PID23736/base007E0000. x86 Debug passes; Release build/runtime attribution pending. No performance gain claim.

- 2026-10-05 native function profiling: user correctly requests hot addresses/disassembly rather than module percentages. Added offline ETW sampled-stack export/flame graph, exact first30s raw totals verified slow28309/fast22865, stacks28271/22754, lost0; partial WOW64/native unwinds recorded. Verified indexed-draw00426180, geometry/material00444C50 and shader-cache ranges live; report includes inclusive/exclusive differential and local zoomable graphs. Synthetic PDB possible (RSDS present), optional labels not a blocker. Added default-off model observer; x86 Debug/Release5DC6AE19 pass. Initial512-table overflow excluded,4096 repeated capture zero drops/equal streams; courtyard2682 vsfast379 G-buffer callbacks/eye, spread across barriers/plants/stones and other models. Separate packaged grass mod stays enabled. No optimization gain/closure; next trace draw/model/material/constant chains. See profiling report for exact numbers and limits.

- Ghidra native indexed-draw and geometry/material annotations previewed, applied, read back and `save_program` succeeded. Diagnostic hooks have completed/detached; live fast scene FOCUSED~89.6Hz rolling,zero overflows. This rolling readback is not a new matched performance benchmark. Tools build/runtime validations and generated JavaScript syntax pass; diff whitespace check passes. Continue #013 without requiring synthetic symbols first.

- Stereo sharing/parallelism investigation: current BuildFrame repeats only three world streams per eye, not simulation/shared shadows/reflections/UI. Live008DCBF0 mixes resident drawable lookup, mutable command quaternion/world-transform preparation, shared scalar/constant state and drawing. Verified006708D0 is camera-independent quaternion-to-matrix (20/28309 exclusive samples); isolated cache not sufficient. Native callbacks cannot simply run concurrently because shared RAGE/D3D9 state would race. Investigate extraction/reuse of immutable instance/material preparation and separate per-eye packets; worker/GPU/multiview options remain possible, not implemented. Source/disassembly evidence, limits and Ghidra saved annotation in profiling report.

- Larger-target investigation: bounded material-binder probe004373E0 built x86 Debug/Release151550BC; same-process courtyard244/fast450 equal six-phase streams,zero drops. Inclusive binder pair3.37548 vs1.18097ms (nested overhead, no gain claim); GBuffer~5331/5241 vs1399/1338 binds/eye. Existing shader/buffer caches prevent blind binding skips. Prioritize compatible static-instance batching and shader-driven billboard generation, leave0.071% helper alone. New standalone authored SM3 instancing shader/runner passes installed x86 DXVK pixel equivalence in both synthetic eyes,zero mismatches/nonempty output; no game shader replacement or speedup yet. Native shader/occlusion/depth/blend/reset/fallback gates recorded. Grass mod retained; Ghidra binder evidence saved; #013 open.

- Native GPU billboard contract captured with default-off getter observer around00C1F940/945:233 equal streams,zero dropped rows, valid DrawScene snapshots both eyes. Position/center/color/UV36-byte declaration and native depth-sampling/fog shaders verified. x86 Debug/Release4B35C700 pass after local generated-project compiler parallelism workaround; backup/deployment/restore/live image pass. Authored runtime instance-expansion prefix retains native shader body; installed DXVK accepts it. Standalone captured-native-shader pixel comparison passes eye0/1 visible144lit and occluded0lit, all zero pixel mismatches. Actual native shader assets stay local out of Git; no in-game replacement/speedup yet. Preserve atlas/near-light/shift/variants/state-cache/reset/ordering before connecting compact records to draws. Ghidra contract saved, runtime base00BF0000 recorded; #013 open.

- Compact GPU billboard integration: native32-byte records match first six expanded vertices exactly both eyes; captured-native-shader colored-atlas/depth/shift pixel tests zero mismatches. Default-off in-game native batch replacement built x86 Debug/Release206104DC; backup/verified exit/deploy/courtyard restore passed. PID25644/base00BF0000/SteamVR2.17.10/GNU265888C3, both main eyes, runtime9076batches/24669456lights/0fallbacks. Off/on/off30sec FOCUSED captures with no Computer Use/builds: DrawScene pair5.2360/4.8218/5.3612ms, submission23.5235/23.2936/23.9637ms, no drops/overflows. Candidate0.4-0.5ms local benefit, no sustained90Hz or tail improvement; nighttime/body obstruction/traffic drift and broader graphics/lifecycle gates explicit. Experiment disabled after test; native fallback/defaults retained. Ghidra runtime bookmark saved. Continuing larger GBuffer mesh/material batching opportunity with bounded actual-draw grouping; #013 open.

- Larger batching census built x86 Debug/Release8E9DB5A8; native model probe203equal six-phase streams, no drops. First eye0 GBuffer5146indexed draws/1988distinct complete binding keys/3158repeated-key draws, getter failures0/table drops0. Model788178repeats, model1838three142repeat groups, model761two81repeat groups. No draw decisions changed; textures/constant/extra-stream/animation/render-state/ordering compatibility not yet proven. About61% repeated keys is an opportunity estimate, not savings. Proceeding bounded constant/texture variance check and runtime shader inspection; GPU billboard test stays default-off. Exact local evidence and limits in profiling report.

- Native mesh state dependency followup built Debug/Release0849130A then refined2BC8AB1A. Completed getter-only first-frame captures no failures/drops; sampled model1838group135draws/104state samples. Runtime VS uses per-instance c0..3(world) andc8..11(clip), other read constants stable; API changes at shader-def/unused registers irrelevant. Native GBuffer PS reads onlys0 which stayed identical; changedtexture1/2/4unused. Native fade/UV controls stable in sample. Promising per-instance matrix payload for substantial mesh batching, preserve original alpha/texkill/depth/MRT. Sampler/render states/extra streams/int-bool/ordering and broader animation/fade gates remain. First-stream1879vs5480population differences mean no benchmark inference. GPU light experiment live inPID9256for user visual acceptance, default-off; mesh batching not installed. Source/report evidence persisted; #013open.

- Authored rigid-matrix GPU prefix and standalone test built x86 Debug/Release without warnings. Captured native VS/PS with16 transformed instances/two clip-eye matrices: original16draws vsinstanced1draw, all4GBuffer color targets identical, visible5121lit/cutout0lit, everycase0mismatches. Shader648->840bytes accepted on GNU DXVK/RTX4070Ti. Readback test-only; no game mesh batching, depth-buffer/query/order/reset or FPS acceptance. Final guards/cutout assertion rebuilt/retested. User reports GPU billboard lights look unchanged in both eyes while moving; liveopt-in retains default-off/fallback. Larger mesh batcher remains next; #013open.
- Rigid shader parity also passes with D24S8 native shader depth: nearer instances remain visible against later0.5depth magenta plane, farther ones occluded. Both eyes/all4targets0pixel mismatches,13829plane/2555foreground pixels, cutout0. Indirect depth behavior proof, not raw-depth-bit or game query/order/lifecycle/FPS acceptance. No mesh batcher installed yet.

### Additional fixture: city across the bridge (2026-10-05)

User identified the current view as looking across the bridge toward the other
part of the city, and reported it is slower than the courtyard. Preserved locally
as `build/profiles/city-across-bridge-location.json` with completed native capture
and readback: (1346.939331, 255.391068, 34.286610), character heading 0.305226.
`city-across-bridge-view.json` records configuration, frame 47353, the four emitted
right-eye world-camera rows, and both fresh 1920x1080 eye image paths. Evidence
is under ignored `build/profiles/native/city-across-bridge-*`; game CE 1.2.0.59,
SteamVR 2.17.10, DXVK 3.1.1 Vulkan OpenXR, GPU billboard experiment enabled.

The location helper restores position/character heading, not headset orientation.
Match the saved image and camera rows when repeating this perspective. The captured
viewpoint is low with body obstruction; location and image capture were sequential,
and traffic/weather/time are not restored. Only right-eye matrix rows were emitted;
do not infer missing left-eye rows. This records a user-reported heavy scene,
not a matched performance result or a new acceptance claim.

### City mesh compatibility and ordering census (2026-10-05)

Release E796F86533FCFA06D52A3706363D674023399A7B064E562F8999167D98767271,
PID26884/render24272, live module base00BF0000; CE1.2.0.59, SteamVR2.17.10,
GNU DXVK3.1.1/265888C3. Debug and Release builds passed (existing dependency
PDB warnings). ASI/INI backed up; clean bridge quit/exit verified before deployment.
Restored city-across-bridge location with completed native readback; fresh window
image confirmed the skyline/fence, at night and low headset height with body
obstruction. Not a head-height comparison against earlier street captures.

Bounded first-eye GBuffer census:6490 indexed draws,1923 exact keys,4567 repeated
key draws, failures0/dropped0;155 six-phase streams. Only198 repeats are consecutive
within the indexed sequence. This is an upper bound for adjacency opportunities:
non-indexed draws, query boundaries and intervening state changes still matter.
Grouping globally would require verified ordering/state rules, not just a key match.

Selected modelKey3695,25 samples after32nd match: stream0 stride52, VB/IB usage8
(write-only, not dynamic), stable other streams/targets/viewport/scissor/sampler0.
COLORWRITEENABLE and COLORWRITEENABLE1 vary between instances. Actual VS has a
fifth tangent input; actual PS reads s0/s1, and texture1 varies. Active fade c39.x
also varies. Existing four-input rigid prefix correctly excludes this variant.
Native shader bodies and details saved only under ignored mesh-compat-city files.
Live binder helpers437170/4372B0 traverse mappings;437C40 already checks sampler
cache values before forwarding changes. No broad constant/sampler skipping applied.

Observer detached for clean30-second CPU-only city baseline,1040 frames/drop0:
mean GBuffer pair12.3995ms, lighting pair1.1607ms, DrawScene pair8.0285ms,
renderBeforeXRWait21.7244ms; submission interval28.8294ms(p9530.9670ms).
GPU billboard experiment off, no Computer Use/build/offline analysis during the
capture. These are wall spans, not pure CPU exclusive work or achievable savings;
night/low view and evolving scene state limit comparisons. Profile remains local.
Eight-group bounded state census prepared to avoid choosing a target from one
arbitrary material; mesh instancing remains a prototype, not an installed optimization.

### ZolikaPatch compatibility check (2026-10-05)

Checked the author's ZolikaPatch IV page and changelog:
https://zolika1351.pages.dev/mods/ivpatch and
https://zolika1351.pages.dev/mods/ivpatch/changelog . Supported executables listed
are1.0.8.0,1.0.7.0,1.0.4.0,1.0.0.0; our CE1.2.0.59 is not listed. Compatibility
with current Steam data/save files refers to older supported executables, not
proof of CE executable support. The advertised leftover-debug-code performance
option is explicitly for patches4/7/8. The bundled optional downgrader would
change our analyzed executable and invalidate current native hooks. Not installed;
no claim that its memory/debug options improve this measured renderer path.
Feature overlap includes borderless/skip-intro/high-FPS camera fixes already
handled by this fork. No binary patch-by-patch collision audit was performed,
because executable compatibility fails the prerequisite for an installation test.

### Tangent shader proof and multiple material groups

Release F5A80BDC949BBC6B48FFF39BE7E7FC4CF3D8DC572F2CECCE00A0DF14E83AA1BE,
PID7252/render27788,151 equal phase streams,zero drops; first GBuffer6513 draws,
1936keys/4577repeats. Eight groups sampled with zero getter failures. Two groups
of model1930(hash9605340C,modelKey1931),56draws each, have stable sampled render
state/sampler0/streams/targets; changed texture3 is unused by the actual PS.
Changed VS/PS registers are c0..11 only; active shader reads/def overrides must be
used to distinguish world/clip matrices from unused inverse/stale registers.
Other sampled models have active fade/texture/color-mask differences and need
additional grouping/instance data. Native scene order remains a separate gate.

Extended the shader prefix's opt-in tangent variant: originalv0..4 preserved,
8matrix instance inputs v5..12; original default still rejects tangent shaders.
Standalone authored52-byte vertices, native city VS/PS852->1044bytes,16transforms,
both eyes,4MRTs,depth and fade clip. Debug/Release compile clean. Initial fixture
correctly failed acceptance because albedo multiplier was zero and texture-alpha
was not this shader's clip mechanism. Fixed authored material/sample bindings and
exercised its actual fade texkill. Retest:zero pixel mismatches on all targets;
visible16384lit,plane13829/foreground2555,fade-clipped0. Original four-input
control also still passes. Local rigid-tangent-city outputs/shaders stay ignored.
This proves the GPU representation for that shader; no in-game mesh batching or
performance gain is claimed. Query/non-indexed/model-run boundaries are being
instrumented next, retaining original commands and default-off probes.

Query/order observer Release1DF18A4F9959AFAAFCDA16C5651EE02A558623ADAED58AB3E5074BC0D1E4827C,
PID12448/render28796:Debug/Release build pass;155equal6phase streams/drop0.
FirstGBuffer6415indexed draws/1897keys/4518repeats,failures0/drop0. This backend's
Issue hook attached; within the first observed stream occlusion BEGIN/END/other
query calls0,ordinary nonindexed0,UP0. This does not prove no query was active
before instrumentation or cover every backend's query implementation. Hooks
forwarded all calls and detached before logging. Model1930/BM_wall_light_04
has112draws over28 model runs,max12; merely batching that asset is a limited
opportunity. Unknown model context0 accounts for2150indexed draws/161runs,
max220:these are outside the specific008DCBF0 callback, not actual model0.
Added an independent bounded8-group state/caller census for that command path,
using CaptureStackBackTrace only on first sample; no ordinary-frame stack walk.
The initial eight known-model selection missed that substantial draw population.

Corrected a leftover selection guard that excluded modelKey0 from its new census.
Debug/Release pass, Release81AC239B6A5D429A8B93FA838F1A228B99245A37D9DF06F73DFFF80902A4B4A8,
PID4036/base00BF0000:150equal streams,6376indexed draws/1887groups/4489repeats,
failures/drop0;unknown2129draws/164runs/max220. Eight unknown groups67-69samples,
stable sampled render/streams/targets/viewport/scissor/sampler0,tex3 changes,
VS/PS c0..11 vary. Ordinary x86 stack walk truncates at00426240, so it does not
identify the outer dispatch. Live00463490 is a geometry thiscall with optional
stream1 and two calls to00426180; direct-call scan finds no callers. Added a
bounded virtual-dispatch return-address observer; pending runtime capture.
Ghidra Issue013GeometryDispatchCaller00463490 previewed/applied/read back/saved.
The restored scene still has body obstruction; these are diagnostic results,
not a matched head-height performance improvement. No game batching yet.

Geometry-dispatch observer Release24F6C99324A0C83B28DFD9FC059BE773C46B83DF5D839BC5DF9D4B1DEFDE3510,
PID12392/base00BF0000:143equal streams,6400draws/1907groups/4493repeats,
unknown2145draws/153runs/max220,failures/drop0. All eight unknown samples and
knownmodel1931 record geometry return0042EC83. Live0042EBE0 binds material through
004373E0 then geometry virtualslot+8 at0042EC80. Direct callers00444BA7,00444F15,
00AC1AF8;00444C50's direct callers006A4385,006A461A,008DEA8F. Native globals
are shared, so this finding still does not permit parallel invocation.
Ghidra Issue013RepeatedGeometryDispatch0042EC80 previewed/applied/read back/saved.
Exact unknown VS/PS standalone GPU instancing parity passes both eyes/fourMRTs,
depth foreground2555/plane13829 and fade-clipped0,zero mismatches;852->1044B VS.
Local authored geometry only; no native mesh readback, no performance claim.
ReleaseA5F27FC9ACA1BF6FE589D3FC4577009E2635BAD212DC03E0EFF40C776D72D041
adds a bounded outer00444C50 loop caller observer (thiscall/RET0C validated live).
Startup retries encountered the separate pre-window0xC000001D failure; no
observer activates until the explicit bridge command after scene readiness.

Outer-loop capture PID27788/base00BF0000,ReleaseA5F27FC9:141equalphase streams,
6378indexed/1892keys/4486repeats,unknown2146draws/158runs/max220,failures/drop0.
Eight unknown groups have outer return006A461F; known1931 has006A438A. These are
instrumented boundary return addresses appended after the truncated OS stack,
not an assertion that CaptureStackBackTrace recovered the entire call chain.
Live006A43A0 selects a part list, chooses transforms using part byte+17, builds
and optionally composes3x4 transforms, publishes them through00431940, then calls
00444C50. Wrapper006A1D30 invokes it; simple wrapper006A1D10 invokes006A4320.
Ghidra decompilation corroborates this role but loses arguments/register liveness;
no recovered prototype applied. Renamed006A43A0 to
DrawDrawablePartsWithSelectedTransforms after preview; read back and saved with
Issue013TransformSelectingDrawable bookmark. No procedural/skinning identity claim.

Relabeled existing local slow/fast flame graphs with verified dispatcher/drawable
ranges using each trace's image rundown; no new CPU trace or timing claim.
Selected-transform loop:slow395inclusive/140exclusive of28271samples,
fast210/85 of22754. Shared-transform loop1189/53 vs498/16. These sparse/partial
stacks do not bound descendant costs or convert to per-frame savings. Target is
the repeated material/geometry pipeline, not just the small matrix loop itself.
For batching, native material/state/order and transformed-part lifetime must be
retained; eight compatible samples are not permission to reorder all opaque draws.
GPU billboard experiment restored on after probes; native observers detached,
live OpenXRFOCUSED/eyeImagestrue and command overflows0. #013 remains open.

### 2026-10-05 — Production mesh representation compatibility stage

Added `source/includes/vrrigidinstance.hpp` and opt-in bridge command
`rigid-instancing-test on/off`. Main-eye G-buffer triangle draws with verified
rigid/tangent declarations carry actual VS world c0..3 and clip c8..11 in a
128-byte instance record. One replacement per original draw, native order and
material/pixel state retained; unsupported variants/extra streams use native
fallback. A64-entry shader/declaration cache owns its references. The dynamic
8192-record default-pool buffer appends NOOVERWRITE and wraps with DISCARD.
Reset/shutdown disable and release it. No native geometry readback or callback
suppression, no batching, and no performance improvement claim.

Win32 Debug/Release builds passed; existing dependency PDB warnings only.
Release SHA256 A0218F643B42D6C1AF952B2FAB8C7BAB35B942DE002D419C99F0F791E2705795.
The standalone authored fixture exercises this same production renderer in
addition to native and16-instance paths. Both four-input and tangent native
shader fixtures passed both eyes/all four color targets with zero compatibility
mismatches, including depth-plane and fade/cutout cases.32 production replacements
per eye, zero failures; shader/declaration/stream1/frequencies read back after
EVERY draw and restored. Native shader inputs and results remain ignored/local.

Installed after bridge quit and verified exit of PID27788; previous ASI/INI
backed up under ignored build/issue013-rigid-live-backup. No INI/backend change.
CE1.2.0.59, SteamVR2.17.10/90Hz, x86 DXVK3.1.1,1920x1080 per eye.
PID23724/render28256, saved city-across-bridge position restored and read back
(1346.939331,255.391068,34.286610,heading0.305226). Runtime experiment stopped
with4395236/4435087 replacements,596527/609003 native fallbacks, failures0 and
restorationFailed0. Fresh Sky image and both fresh captured eye BMPs contain
live city/bridge geometry; low resting HMD/body obstruction limits visual
coverage. Headset moving-view acceptance requested separately, still pending.
These aggregate counts are not performance measurements. Per-draw getters and
shader warmup intentionally add overhead; disable this compatibility stage
before ordinary timing captures.

Next: combine genuinely compatible draw packets at the native material/geometry
boundary. Keep order/query barriers and full material/resource lifetime rather
than grouping by geometry key alone. The captured tangent PS defines c0,c1,c2
inline; sampled PS c0..11 variation is therefore not proof of per-instance pixel
work. Its external reads include c39,c51,c52,c66,c72..75,c209 and s0,s1,s2,s4;
all must be retained, with native depth-writing/fade behavior. VS camera c15 and
UV/material c39,c40,c45,c208,c209 remain per-eye/material dependencies outside
instance matrices. This identifies what a batching packet must capture; it
still does not authorize arbitrary shader variants or cross-eye reuse.
No new Ghidra finding/annotation this stage; existing native dispatcher findings
remain saved. #013 stays open; reset, broader graphics, and measured batching
benefit remain unverified.

2026-10-05 headset acceptance: user reports all geometry looks fine in the live
mesh compatibility test. Disabled it through the bridge before further work;
acceptance covers observed graphics, not reset or performance.

Extended the production renderer to accept explicit same-state matrix packets,
with bounded8192-instance uploads/early ring wrap and one indexed draw per packet.
In-game callers still pass only ONE instance; no native grouping enabled yet.
Four-mode authored test compares native, prior instanced reference, production
single-instance and production multi-instance output. Both native shader variants
passed all four targets/both eyes/depth/fade with zero batchMismatches, draw errors
or restoration failures. A deliberately offscreen8190-instance packet followed
by16 visible instances exercises early DISCARD wrap; shader/declaration/stream1/
frequencies restored after each batch. Batch emitter3draws/eye,8222instances/eye
(includes ring stress);32 single-instance control draws/eye. x86 Debug/Release
ASI and probe build pass. Local rigid-production-batch-{control,tangent}.txt.

Revisited ordering capture mesh-order-city-full.txt:6415draws/1897keys/4518repeats,
only195 draw removals from consecutive equal keys (upper bound before full state
compatibility), about3% of draw count, NOT3% CPU savings. Most repeats are
nonconsecutive. Target native packet preparation/replay rather than presenting
adjacent merging alone as the solution.

New runtime boundary evidence PID23724/base00BF0000:0042EBE0 calls material binder
004373E0 then geometry virtualslot+8 at0042EC80. After geometry it conditionally
calls0042C8F0 at0042EC8C. That helper walks active pass+0x18 entries/count+0x1C and
calls device vtable+0xE4 (slot57/SetRenderState) using mapped state/default values:
post-pass render-state cleanup must survive grouped replay. RET0x10 observed for
0042EBE0.00431940 copies world matrix rows to its context then calls00430720 and
00430F00; skipping it wholesale is not yet justified. Live listings remain local.
Ghidra static0042EBE0 lacks a function/bad-instruction;00444C50/00431940 decompilers
show nonsensical static instructions versus verified live code. No flow repair,
rebasing or prototype applied. Persisted Issue013MeshPacketBarrier at0042EC83,
previewed/read back/saved, with runtime evidence and cleanup requirement.
Next native grouping prototype must retain cleanup/state/order, resource lifetime
and per-eye/material dependencies. No mesh batching runtime or speedup claim yet.

### 2026-10-05 native packet proof and rejected binder coalescing

The bounded native scratch capture (CE1.2.0.59, x86 DXVK3.1.1,
SteamVR2.17.10/90Hz,1920x1080 per eye) selected36 compatible static native
mesh draws from2048 inspected/1925 eligible draws,1024 groups and446 drops.
Reference36-draw versus one instanced draw: valid1, restored1,
differingBytes0, nonzeroReferenceBytes3625 across four native-format targets.
This proves isolated nonempty native geometry equivalence only; it excludes
other geometry, ordering, direct depth equality and resource/query effects.
Earlier128-group capture was invalid and is not acceptance evidence.
Authored four-input/tangent fixtures also pass capture/restore and output checks.

The verified native vertex-parameter binder00437170 (RET0C) and texture/resource
helper00437C40 were used for an opt-in adjacency/overlap upload coalescer.
Native callbacks still run; copied float writes flush before texture/resource
callbacks, draws and binder return.64 readback checks passed, no reported
validation failures. Intercepted calls fell roughly31–38%, but millions of
binder scopes contained far fewer intercepted writes; this does not count all
scene constant updates or establish a profitable replacement.

Matched30s CPU-only off/on/off captures, same PID8708 and saved city location,
Release SHA25663D8BDE52B2A62DF25BF68786EDF422B45C93A30AF0BB676602349ED9D678AEA:

| Metric (ms) | Off before | On | Off after |
| --- | ---: | ---: | ---: |
| renderBeforeXRWait median |22.0389|23.4727|21.8669|
| renderBeforeXRWait p95 |23.6090|25.0425|23.5669|
| renderBeforeXRWait mean |22.1777|23.5515|21.9994|
| submissionInterval median |28.9376|30.6190|28.7380|

Local files binder-batch-off-before/on/off-after.json. Valid comparisons,
1033/975/1042 render span samples. No GPU timestamps, builds or computer-use
captures during timed windows. Nighttime traffic changes and the low resting
headset/body obstruction limit this to a local matched test, not a normal
head-height native-refresh acceptance route. These are measured paired-eye
spans, not sums of separate quantiles. Coalescing is slower: keep it OFF.
The isolated diagnostic/helper remains available, never enabled by default.

Persisted/read back/saved Ghidra Issue013BinderBoundary at00437C40 with the
resource callbacks, sampler/texture slots, stack-convention evidence and negative
timing result. No destructive flow repair or invented prototype.

Next diagnostic compares captured native packets in original order against
last-occurrence grouped replay, only in scratch targets. Partial capture/color
agreement cannot establish safe whole-scene reordering or a performance benefit.

2026-10-05 city packet ordering experiment: Release
EB53B348F8A519EF930E977CDC1EAEEC29D265A18C4CCCE0548593E247C6105D,
PID21876, restored/read back city-across-bridge fixture, FOCUSED continuous
DXVK stereo. Fresh Sky image shows the bridge/city; low resting HMD/body
obstructs foreground. Scratch subset1474 draws versus1024 grouped draws:
valid1 restored1 differingColorBytes0 nonzeroReferenceBytes13986076.
Capture2048 inspected/1888 eligible,1024 groups,414 drops, failures0.
This is a larger nonempty color ordering proof, not whole-pass parity, direct
depth equality, safe production scene reordering or measured performance.
Local mesh-order-city-result.txt. Native gameplay callbacks/draws unchanged.

Added normalized D24 depth readback through scratch-only INTZ texture and an
R32F copy shader. Unsupported creation fails the diagnostic; live depth format
is untouched. Both authored native formats pass all color targets/eyes and
zero differingDepthPixels; visible fixture has5121 depth pixels and cutout has0.
Extended ordering capture is bounded8192 inspected/4096 groups/128 instances,
128 shader variants/4000ms; original isolated mode retains2048/1024/64/1500ms.
Coverage now reports actual observed draws separately from inspected/captured.

2026-10-05 expanded color/depth scratch proof, Release
7B46212DB1AE670C423A23F03FEDEE4F3061CE46688883C737B781755B70501D,
PID27884 after retry of known early0xC000001D startup exit. Restored/read back
city fixture; fresh Sky city image; continuous FOCUSED DXVK stereo resumed.
All6510 indexed draws inspected,5745 eligible/captured,3147 compatible groups,
zero capture drops/failures, largest77 instances.5745 reference draws versus
3147 grouped draws produced zero differing bytes in all four color targets,
14452339 nonzero reference bytes. Normalized scratch D24 depth also matches:
zero differingDepthPixels,1361273 referenceDepthPixels. Local mesh-depth-city-result.txt.
This removes2598 draw calls from the eligible SCRATCH subset (~45.2%), not from
live gameplay; no render-thread savings measured.765 excluded indexed draws,
stencil/resource/query behavior and comparison against the actual native frame
remain the next gates. Game callbacks/order and production rendering unchanged.

Next complete indexed-packet replay diagnostic retains unsupported/dynamic/
blended/stencil/non-depth-writing draws as native draws and ordering barriers,
and compares scratch original-order colors against the actual native G-buffer
when coverage is complete. Query/UP observers are required; any observed query
invalidates scratch replay and non-indexed draws prevent a complete native
comparison. State blocks own resources but do not freeze mutable buffer/texture
contents: native parity is a required experimental check, not an assumed property.

2026-10-05 initial full-replay safety guard rejected ALL6525 city draws from
batching (eligible0), treating every enabled stencil operation as a barrier.
4096-group capacity therefore dropped2429 draws. valid1/restored1 only for
partial4096-vs4096 scratch replay; nativeCompared0. Zero differences here are
NOT native parity or batching evidence. Local mesh-replay-city-result.txt,
Release37B4EC74B124D4218878E61A7A4FF544E90738838CE194C323E343BF03679B0B.

Existing mesh-callers-city-full.txt state observations resolve this guard:
stencil enabled1, FUNC=ALWAYS(8), FAIL/ZFAIL=KEEP(1), PASS=REPLACE(3), masks255,
two-sided0, depth enabled/write1, blend0. Writing a fixed stencil marker within
an unchanged signature is distinct from increments or testing prior stencil.
Scratch replay now permits only disabled or ALWAYS/KEEP/KEEP/(KEEP or REPLACE),
with signature changes (reference/masks included) as barriers. Blending, absent
depth writes, unsupported depth function, dynamic/unsupported geometry remain
barriers. Other stencil operations are never grouped. Both native shader
fixtures now exercise enabled ALWAYS/REPLACE marker32 and pass native color
parity, grouped color/depth, both eyes, cutout and later depth-plane checks.
No actual game stencil readback yet; this remains a scratch experiment.

2026-10-05 refined stencil-signature scratch capture: Release
F9775FCFACAFDE6430141C7BEE57387784F3841E80A95E76ED3E2946180A21CF,
PID23792, city fixture restored and fresh image checked.6528 draws inspected,
5437 eligible,6417 captured,4096 groups,111 capacity drops, failures0.
6417 reference versus4096 grouped draws: identical four-target color and
normalized depth (1393872 covered depth pixels), restored1.1290 barriers,
including199 stencil signature changes. Query/other draw observers saw0 events.
nativeCompared0 because coverage incomplete; this is NOT actual native parity.
Local mesh-stencil-city-result.txt. Expanded all-indexed-only group limit5120
(from4096) to cover this measured shortfall; isolated/ordering bounds unchanged.
Observed post-probe process private memory~1.96GB/virtual~3.62GB: stay bounded,
fail allocation gracefully, do not increase limits indefinitely in this x86 game.

Added a read-only stencil fingerprint: fullscreen EQUAL tests for each bit,
KEEP operations, zero stencil write mask and disabled depth writes, accumulating
integer bit weights in R32F. Float-blend support is explicitly queried first.
The actual native depth/stencil can be attached for tests without changing its
contents; scratch reference/grouped stencil is compared independently. Both
native authored formats pass actual native color AND stencil parity, identical
scratch color/depth/stencil,5121 visible marker pixels (cutout0), both eyes,
and all production depth-plane/fade checks. Local stencil-read-fixture files.
Runtime native stencil/frame parity is pending. No gameplay batching enabled.

Ghidra Issue013BatchState at0042EC83 previewed, written, read back and saved: observed stencil marker semantics, scratch color/depth counts and incomplete-native-parity limitation. Existing native cleanup bookmark retained.

2026-10-05 actual native color/stencil gate: installed Release04B2E8ED68795FFFF1EDD14A19DEEA4842C0A27A315A34D1DFC2F0D7055AF5B3 captured6444/6444 indexed draws,4122 groups, no drops/failures/query/other draws. All four actual native colors matched; scratch color/depth/stencil matched. Actual native stencil differed in693378 pixels when scratch stencil started at zero. This was a failed native stencil gate, not production parity.

Preserving the pre-first-draw stencil resolves that difference: ReleaseBC5F2558A959ED7FD9F46565DC4CEACE888BB2EEF89E7510F234B14FF4403643, PID10216, saved city coordinates restored/read back and fresh Sky image checked.6433 observed/inspected/captured draws,5345 eligible,4071 groups, zero drops/failures. Reference6433 -> grouped4071, zero differences in four colors, normalized depth and stencil. Actual native colors AND stencil both compare with zero differences (nativeCompared1/nativeStencilCompared1);1270 ordering barriers incl182 stencil-signature changes;0 query/other draws. Nonempty12069102 color bytes,1121677 covered depth pixels,988507 nonzero stencil pixels. Local initial-stencil-city-result.txt. GPU snapshot reads original S8 with KEEP/mask0/depth-write-off bit tests before the first native indexed draw; eight masked replacement draws reconstruct it only in scratch depth. Authored fixtures seed left7/right128 stencil and pass both shader variants, both eyes, cutout/ring-wrap/depth-plane checks. Debug/Release builds pass with existing dependency PDB warnings.

This establishes frame-local native color/stencil parity and scratch depth equality, not native depth parity or runtime speedup. Diagnostics still collect hundreds of state fields and state blocks per draw and must remain one-shot/off during performance captures. Next gate: read-only native INTZ depth comparison, then a cheap material/geometry packet path preserving native cleanup/resource/query effects. No gameplay draw skipping enabled.

2026-10-05 actual native depth gate passed: Release4A199E517ABB9835861774D57FD59D21DE514D76DF1A169E0CA4FFF14E6698A6, PID14968, same saved city location/readback/fresh image.6584/6584 indexed draws captured,5458 eligible,4155 groups;0 drops/failures/query/other draws. All four native colors, normalized native INTZ depth and native S8 stencil compare with zero differences against original-order scratch; grouped scratch matches all three.1122990 covered depth pixels/1055604 nonzero S8 pixels/12129699 nonzero color bytes.1328 barriers incl202 stencil-signature changes. Read-only native depth GetContainer only for confirmed INTZ format/same size, unbound from depth attachment while sampling; original bindings restored. Both authored fixtures now use INTZ and require nativeDepthCompared1/differences0, in addition to color/stencil/ordered/ring-wrap tests; pass both shader variants. Debug/Release pass. Local native-depth-parity-city-result.txt. Ghidra Issue013BatchState preview/readback/save reflects the actual gate. No production speedup claim.

Next bounded diagnostic correlates each eligible indexed draw with the original CE004373E0 material-pass identity, retaining every native binder/resource call. Complete non-transform packet state is still compared across occurrences of a material+shader+geometry key; missing identities and state splits are reported. A pointer match alone cannot authorize caching, and material cleanup0042C8F0/resource lifetime must be retained.

2026-10-05 native material recipe census, Release8B697E16E215CE84A748F2FB5786875FA234E1D9E83DBAF677D04B0AD80ACE3E/PID23896. All5470 eligible indexed draws have CE004373E0 binder identity (missing0);1801 material+shader+geometry keys,167 keys have multiple complete non-transform states,2105 draws in single-state keys.6610/6610 indexed capture ->4194 groups;0 drops/failures/query/other draws; actual native and grouped scratch color/depth/stencil parity all zero.1331 barriers incl191 stencil signatures. Local material-recipe-city-result.txt. Native binder pointer is a candidate key only; state splits affect3365draws, so blindly memoizing material identity would be wrong. Live module base005E0000 confirmed for preceding PID14968;004373E0 live006173E0 has the verified state loop/device+E4 and VS/PS helper calls, RET10. Static imported Ghidra decompilation remains bad-instruction/nonsensical, do not use it as schema proof.

Followup scratch key refinement uses actual SM3 sampler operands in addition to float-constant operands, ignoring declarations/comments and rejecting relative/out-of-range registers. All shader-used texture/sampler state stays in the key; unsupported shaders/native barrier packets retain every binding. Owned state blocks still restore the full device state. Both authored GPU fixtures now deliberately alternate an UNUSED sampler texture/address between native instances; production dependency parser plus native color/depth/stencil equality and16->1 grouping pass for both shader variants. This removes false compatibility splits; still no live batching or timing claim.

2026-10-05 sampler dependency refinement in game: ReleaseAC370E292FA58D8A4E59E367987E644143EF88070BBB88B9763A777308C64DBF, PID10688, saved city restored/fresh graphics checked.6626/6626 indexed capture,5494 eligible ->3921 groups,0 drops/failures/query/other draws. Complete native colors/depth/stencil and grouped scratch all compare with zero differences,1176420 covered depth pixels/1397751 nonzero S8 pixels/13673200 nonzero color bytes. Native material identities cover all5494 eligible draws;1800 coarse keys,118 split keys,3485 draws in single-state keys.1339 barriers incl207 stencil signatures. Local sampler-mask-city-result.txt. Counts differ slightly with traffic/time; not a matched live performance improvement.

Per user status/payoff request, further compatibility refinement pauses for a bounded same-packet CPU/GPU benchmark before production implementation. New command mesh-benchmark-probe: same grouped order and shared state binding once per group in both timed modes; ordinary per-instance draws versus tested instanced emitter; four warmed ABBA samples each with GPU query drain. Report CPU submit/stateApply/emitter and GPU timestamp spans. Excludes capture/preparation/compilation/readback/clear/initial stencil setup. Existing expensive emitter stream getters/restoration remain in the candidate and are included, not hidden. No gameplay FPS or total saving claim from this experiment alone.

2026-10-05 first batching payoff test (negative): Release50751948EB6BDB1F3CDDFB6D9B8AC06F9918A285B45C3180C1BCA318C35D0F45/PID7556. Saved city/readback/fresh image then no CU during one-frame warm ABBA samples.6558 indexed draws ->3813 groups, actual native and grouped scratch color/depth/stencil parity all0, no drops/failures/query/other draws. Four ordinary emitter CPU samples1.8244/1.7907/1.8174/1.8223ms; grouped3.3658/3.5866/3.6555/3.3802ms. Medians1.81985 vs3.48340ms per captured eye0 GBuffer: current generic emitter is slower by1.66355ms despite~42% fewer draws. Shared StateBlock/scratch binding~35ms dominates diagnostic totals and is NOT native game cost. GPU timestamp spans~35-40ms include those CPU submission gaps, NOT GPU busy time. Cannot claim FPS gain. Local mesh-benchmark-city-result.txt.

State-split categories: PS-float87keys/1498draws,render43/721,int-bool0,texture-sampler0,stream-view-target0; counts overlap.5481 eligible/1789 materialgeometrykeys/87splitkeys/3983 single-state draws. Further categorization/refinement paused per user: test shader submission payoff first. Followup benchmark reuses warm validation of exact immutable prepared packets (skips repeated16-stream getters and finite matrix scan), singleton groups remain native. Gameplay Renderer defaults retain live validation. Capture/preparation cost still excluded, state binding reported separately. Authored control/tangent CPU/GPU query+native parity fixtures pass.

2026-10-05 prepared-emitter payoff: Release3FB59668A0C252CDD0989FCE25B04E3796C0FA72646C15E50A18A8F09D899D83/PID3576. Saved city restored/readback/fresh image, no CU during warm ABBA samples.6631/6631 indexed capture ->3973 groups,5515eligible,0 drops/failures/query/other draws; actual native and grouped scratch color/depth/stencil differences0, restoration1. Ordinary emitter CPU1.9658/1.8948/1.8736/1.8851ms; prepared grouped1.7262/1.8861/1.6699/1.7270ms. Medians1.88995 vs1.72660ms, delta0.16335ms (~8.64%) per captured eye0 GBuffer. This is a small emission-only candidate, not a full-frame saving or90Hz fix. Shared StateBlock/target-binding work36-43ms in both modes dominates the artificial replay and is excluded from the emitter comparison. GPU timestamp spans36-44ms are starvation-inclusive diagnostic intervals, not native GPU processing or proof of shader speedup. StateBlock timings overlap emitter difference variance; four samples only, no sustained gameplay benchmark. No scene draw skipping enabled in gameplay.

Further shader refinements are deprioritized: batching alone shows insufficient payoff for the larger frame gap. Next substantial-payoff gate is reuse of native model/material preparation across eye views, or cheaply prepared material packets replacing repeated native binding callbacks. Establish the avoided native preparation cost separately before building a production cache; preserve eye-dependent matrices/constants, visibility differences, cleanup/resource/query ownership. Existing native binder/model inclusive profiles are candidates, not additive or guaranteed savings. Issue013 remains open.

2026-10-05 live replacement implementation: commands mesh-native-probe / mesh-live-probe schedule one ordinary/replacement eye pair, then retain ordinary continuous VR. Eligible native indexed calls are actually suppressed and their owned packets emitted to the real eye targets, not scratch. First implementation deliberately preserves native order (adjacent identical-state groups only), static triangle buffers/static2D textures, opaque/simple-stencil guards; flush before CE004373E0 material callbacks, CE0042C8F0 cleanup, query/UP/non-indexed/clear/stretch/attachment changes/pass exit. Native callbacks, cleanup and game simulation still execute. Full collector/state capture cost is included in G-buffer CPU span. Previous global scratch draw reduction is not promised by this conservative implementation. Allocation fallback flushes accepted packets before native current draw; an executed failed instanced draw never gets a duplicate native retry. Debug/Release builds pass (existing dependency PDB warnings). Authored control/tangent GPU fixtures exercise actual16-native-draw skip ->1 batch in both eyes, all four colors match (liveMismatches0), cutout and later depth plane checks pass. Runtime deployment/measurement pending; issue stays open.

First real gameplay replacement result (conservative phase), Release1C738EAE56187C09A7AD0C8843B83FD889AA13381FB3CBF6BB4AFBF9D89C4C26/PID6132: native G-buffer5.9345/5.0486ms, live266.0060/199.5414ms.6466/6535draws observed,5420/5473queued and equally many emitted;0 batches,0 failures. Material-boundary flush made every accepted draw a singleton. This is a failed optimization, not an improvement. Local live-mesh-first-result.txt. Normal headset rendering resumed afterward with no command-buffer overflow.

Expanded bounded experiment retains native binding/cleanup but defers static opaque packets across their device-state changes (CE0042C8F0 listing shows only a render-state restore loop, no resource writes). Uses the prior full-frame parity experiment's last-occurrence grouped order rather than adjacent-only groups. Mandatory resource-write barriers: VB/IB/2Dtexture locks, UpdateTexture/UpdateSurface, GenerateMipSubLevels, ColorFill, plus previous query/draw/clear/stretch/attachment/pass barriers. Rejects changing sampled dynamic/RT textures and unsupported resources. Explicit one-eye-pair experiment, not a continuous production default; equal-depth overlap and cross-thread resource mutation remain validation limitations. Final fixtures now upload static default-pool textures from SYSTEMMEM (D3D9Ex rejects MANAGED and direct locks on static default textures); control/tangent16->1 live skip/parity tests pass, Debug/Release pass. Runtime measurement pending.

Expanded linear-lookup real gameplay test: Release59AC3BC2E5CA319A1004FAF0A50DAF4D3F9BEC1D126C250FD1D0B732C0ECAB1B/PID22004, same saved city readback and fresh image. Native before5.2338/5.2899ms per-eye G-buffer; replacement281.1549/174.5262ms; native after5.5251/4.9613ms. Observed6499/6587, queued5429/5498 -> emitted2836/2871, actual instanced batches530/539, flushes92/93,failures0. Total indexed calls including native fallback6499 ->3906 (39.90%),6587 ->3960 (39.88%). Those are DRAW reductions, not CPU/FPS improvement or avoided model callbacks. Actual real targets receive instanced geometry; ordinary continuous VR resumes after this one pair. OpenXR was VISIBLE, notFOCUSED, so no native-headset-FPS inference; only G-buffer CPU spans compared. Local live-grouped-first-result.txt. Native material/buffer preparation remains; collector/state replay swamps emission savings. Hash-indexed matching now replaces quadratic scan but still uses complete state equality. Next capture reports collect/stateCapture/emit/stateApply/instance CPU spans separately. One-shot hook setup/teardown lies outside per-G-buffer timing; these are not complete frame times.

Ghidra Issue013LiveCleanup at0042C8F0 previewed/written/read back/saved, recording verified state-only cleanup and explicit reordering/resource uncertainty. Latest attempted launch had pre-command GTAIV msvcrt.dll AV0xC0000005 at20:32:53; no fresh ASI render log, SteamVR steamtours tier0.dll AV also recorded. Retried successfully after clearing only the verified failed Play/Rockstar tree. No attribution of startup failure to live hooks (commands were not armed).

Final hashed-collector bounded live measurements, ReleaseCA6FB79F3BEDFB6BCE3955653685EC871682F8AF74CC888A27E7033A204F8B01/PID7828, FOCUSED before/after, city restored/read back and fresh image verified, no CU/build during measurements. Native pair CPU G-buffer medians10.2405ms; two warmed replacement pairs356.9839/360.1277ms (median358.5558ms). Actual indexed submissions reduced40.11-41.19% across warmed eyes,~491-533 instanced batches per eye,failures0/overflows0. Per-eye collection median122.1281ms includes91.1775ms state capture; emission50.1873ms includes25.3103ms state apply and0.8648ms instanced emitter. Categories overlap. Cold emitter38.8685ms shader/driver warmup. These are G-buffer CPU spans excluding one-shot hook setup/teardown, not full frame or GPU/FPS data. Details/table in docs/profiling-013-results.md; local live-grouped-hash-result.txt. Full-state collection/replay is decisively negative despite real live draw replacement. Commands return automatically to ordinary continuous VR; no optimization enabled in normal gameplay. Next: replace full StateBlocks with lightweight native binding/geometry records, replay changed state only, and/or avoid repeated native preparation across views. General scene/equal-depth/multithread-resource validity and sustained performance remain unproven. Issue013 not closed.

Post-test fresh Sky image shows the ordinary city scene rendering again; bridge confirms responsive PID7828, ready continuous DXVK-Vulkan stereo with no command-buffer overflows. Live replacement is not left enabled. No commit requested/made. General live graphics acceptance and production performance are not claimed from a few bounded eye pairs.

User-requested large-cost upper-bound test: omit native rendering CPU subtrees before implementing GPU replacement. New development-only commands cpu-omit-test materials/models/off and cpu-omit-status, automatically off after90s. Main eye GBuffer/DrawScene only, render thread; simulation, command ownership, auxiliary rendering and OpenXR lifecycle retained. materials skips verified00444C50 thiscall/RET0C; models skips008DCBF0 thiscall/plainRET. Both deliberately remove descendant draws as well as CPU preparation: incomplete images and reduced GPU work are expected, so gains are an optimistic ceiling rather than shader feasibility or predicted final performance. No collector, state replay or extra timing observers. Run Release matched normal/omitted/normal frame-profile and independent monotonic stereo-submission counter captures, then restore native callbacks. No pixel parity refinement required for this diagnostic per user direction.

2026-10-05 direct omission payoff measured: native before40.48 submissions/sec, omit00444C50 material loop66.46, omit008DCBF0 model command59.73, native restored41.66. Release81F25E359F8A7D6A134E4405E78309D24D7E6B971C7B3BDF998097CE8CC6A8D3/PID11372, saved city position restored/readback and fresh image, FOCUSED controls, no CU/build during captures. CPU render-before-XR median19.6679 ->8.0034ms with material omission, but full submission interval25.2014 ->14.0837ms, still above11.1ms. Removing descendants also removes GPU draws; optimistic ceiling only, not proof all skipped work is GPU-replaceable. Different command classes reach material loop, so models/materials are not globally nested scopes.30s CPU profiles plus separate15s profiler-off layer counter rates. Normal restored, no production gain/issue closure. Details/raw names in docs/profiling-013-results.md.

User requests broader binary-search attribution of remaining costs. Development-only cpu-omit-test gbuffer/scene/lighting/eyes now omits selected main-eye render-list construction and execution while preserving markers, auxiliary phases, simulation and headset submission; off restores normal paths,90s automatic timeout. This is deliberately invalid imagery and not a GPU replacement. Runtime measurement pending.

Whole-main-eye omission completed, Release7F517D2F/PID21916, saved city view: normal41.54 -> all three passes omitted88.95 submissions/sec; DrawScene alone58.86; GBuffer alone67.06; lighting short5s40.77; restored42.35.30s CPU profiles show normal GBuffer~11.24ms/pair, DrawScene~7.58ms/pair, lighting~1.06ms/pair. All-eye render wall~0.10ms, whole interval11.12ms: reaches intended90Hz pacing, not an unavoidable14ms residual floor. Remaining wall spans include waits (first-eye bridge Copy~6ms when little render work, versus~0.53ms nested copy/fence normal); exact waiting substep unmeasured. Native geometry/material AND remaining scene work are substantial targets; lighting no meaningful benefit. No shader/production gain claim from invalid images. First lighting rate crossed automatic timeout and is excluded; only that rate repeated briefly. Tool now checks omission mode/deadline. Normal restored mode0, fresh image/FOCUSED/nooverflow. Detailed table/evidence in profiling report. User requested faster iteration: AGENTS.md now Release-only when Release is run, reuse comparable baselines and prioritize largest-cost payoff tests before parity refinement. Prior callback findings saved/read back in Ghidra.

User requests higher OpenXR Hz target experiment. SteamVR local configuration identifies VIVE Pro MV at90Hz. Added development-only refresh-test120/90 plus refresh-test-status, queues native xrEnumerateDisplayRefreshRatesFB/xrRequestDisplayRefreshRateFB on session callback thread, exposes advertised count/min/max, result and actual rate. Unsupported rate is recorded without disabling VR. No xrWaitFrame bypass or fabricated predicted display period; physical display change only if supported by runtime. Release-only build/runtime request pending.

Correction per user: do not test display refresh changes; headset is intentionally90Hz. Removed undeployed refresh-test commands. Added bounded15s producer-test on/off/status: on session owner thread, pause headset waits/copies/submissions, retain explicitly cached view pose, skip ordinary gameplay limiter, continue native simulation, eye lists and desktop presentation. Count uniquely published CPU-executed main-eye command pairs; GPU completion and headset submissions are NOT verified by this count. Auto-restores on timeout/focus loss/runtime reset, off explicitly resumes normal XR. This is throughput diagnostic, not an asynchronous production bridge or live headset test. User requests faster iteration; Release-only build and short fixed-view rate comparisons planned.

Uncapped producer short tests completed Release5C786C91/PID25076: normal41.02 XR submissions/sec versus41.86 CPU executed eye pairs/sec with XR paused; no meaningful gain demonstrated. Materials omitted64.95 XR versus79.24 CPU pairs; all eye passes omitted101.33 CPU pairs.5s windows, cached pose, GPU completion NOT measured; normal ranges touch, traffic/time not frozen. Both diagnostics restored, no overflow. Removed undeployed refresh-test120 and unfinished async WIP: user wants candid payoff decisions and current normal scene does not support prioritizing async. Sequential OpenXR loops are normal; actual bridge limitations are copies/fence waits, next-frame pose schedule and limited broader validation, not active D3D11 overhead underDXVK. Details and raw names in profiling report. Focus remains native GBuffer/DrawScene preparation/submission. Async change never deployed; current game remains tested5C786C91 build.

Cleanup Release build passes after targeted removal of unfinished async edits; existing dependency-PDB warnings only. Source build CA7557F72D09636798BDE90E1FBDF893B30977943F519D4EB179F508454F531F. Installed game remains tested5C786C91 build, diagnostics off; no redeployment needed for cleanup-only changes. ROADMAP working loop now agrees with Release-only iteration preference. Async/display-refresh request symbols absent from source. No commit or issue closure.
