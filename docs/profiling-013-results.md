# Performance investigation — issue 013

## Configuration and limits

2026-10-04: GTA IV Complete Edition 1.2.0.59, SteamVR/OpenXR 2.17.10, 90 Hz lighthouse headset, RTX 4070 Ti, system D3D9Ex producer and D3D11 submission device. Continuous stereo and the accepted first-person/collision/visibility fixes remain enabled. Measurements use x86 Release.

The current eye image size is **1920×1080**, confirmed by allocation logs and the user. Earlier 2560×1440 settings must not be assumed for later captures. Other graphics settings have not yet been fully recorded. Scene position is held by the user; traffic, weather/time and head orientation are not deterministic. A fresh desktop capture showed a tilted/inverted headset mirror, so these holds are diagnostics rather than a representative walking benchmark.

Raw JSON and local runtime logs stay outside Git. `build/profiles/` contains the named captures. Submission intervals measure successful application projection-layer submissions, not compositor missed frames or guaranteed unique world frames. D3D9 GPU timestamp spans can include command starvation; they are not GPU utilization measurements. Parent/child timings overlap.

## Limiter verification

Release ASI SHA256 `98BE0512E34C9A9C938C99FCC0488C1D1BDFF44C0D67E0D9D5D0CBC952D3F34F`.

`apartment-limiter-verification`: 2344 intervals in 30 seconds, mean 12.7878 ms, median 11.9988 ms, p95 18.2812 ms. All 2346 observed FusionFix limiter-hook calls used OpenXR pacing bypass; gameplay, cutscene, loading, 60 FPS loading safety and minigame limiters had zero calls. Desktop Present median 0.2328 ms. There is no fixed 60 FPS ceiling in this capture. This does not exclude every external driver/runtime policy.

## Resolution comparison in the street

Same ASI and process (PID 4176), focused stereo before/after. User changed resolution through Graphics and returned to the held street scene. Allocation logs confirm both sizes. No compilation or screenshots ran during the three comparison captures below.

| Capture | Eye size | Interval samples | Mean interval | Median | p95 | Eye 0 / 1 command median |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| `current-copy-wait-repeat` | 1920×1080 | 1823 | 16.44 ms | 16.19 ms | 18.72 ms | 4.90 / 4.59 ms |
| `street-720p-clean-repeat` | 1280×720 | 1783 | 16.81 ms | 16.67 ms | 19.08 ms | 4.86 / 4.60 ms |
| `street-1080p-restored` | 1920×1080 | 1904 | 15.74 ms | 15.21 ms | 18.78 ms | 4.63 / 4.29 ms |

The initial `street-720p-scaling` run overlapped compilation and is excluded from this comparison. Reducing pixel count to 44% did not meaningfully reduce command time or total intervals. A dominant pixel/fill bottleneck is not supported for this held view. The result does not prove a specific CPU bottleneck; native simulation, driver dispatch and synchronization still need attribution.

## Transfer waits

Street repeat: first-eye copy/completion p95 4.29 ms, versus 0.29 ms for eye 1. Swapchain acquisition/wait p95 is below 0.01 ms. Restored 1080p: first-eye copy/completion p95 4.70 ms. Thus the intermittent transfer tail occurs after acquisition, in the D3D11 copy/completion span. It is not yet established whether the time is inside CopyResource, driver Flush or GPU-completion polling.

SteamVR's fresh Frame Timing snapshot showed substantial CPU Late Start and little D3D11 application GPU scene time. Since the world is rendered on a separate D3D9 device, the compositor graph alone cannot establish world-render GPU cost.

## Next attribution

Finer queued phase markers separate G-buffer, lighting and scene drawing per eye. Release `9DC81C6860C14F3DA16099A59E78AA4A47F2963B7B107D34D18126ED06FABC68`, PID 24772, `apartment-phase-breakdown`: 2293 intervals, mean 13.0728 ms, median 12.0620 ms, p95 18.6128 ms. Geometry/G-buffer median is 2.4320 / 2.2722 ms for eyes 0 / 1; scene drawing is 1.2920 / 1.1896 ms; lighting is about 0.07 ms per eye. These are CPU/driver spans.

Transfer spans separate CopyResource, event-query creation, End/Flush and completion polling. First-eye polling p95 is 6.7421 ms, while query creation p95 is 0.0118 ms, End/Flush p95 about 0.06 ms and CopyResource p95 below 0.01 ms. The intermittent delay is completion polling, not query allocation or API-call dispatch. Reusing query allocations alone is unlikely to solve that tail. All FusionFix limiter branches remain bypassed/inactive.

Both Debug and Release builds and bounded profiler tests passed. No performance optimization or native-refresh success is claimed yet.

## GPU-query observer control

Release `0051B8D6F85CC3A46DD037CE74BE8C125B6885D169415E4644B09145E5ECC52C`, PID 21284, same held apartment view. Focus remained FOCUSED; no builds or screenshots during captures.

| Capture | D3D9 timestamps | Mean interval | Median | p95 |
| --- | --- | ---: | ---: | ---: |
| `apartment-query-control-on-1` | enabled | 12.4668 ms | 11.6992 ms | 18.3929 ms |
| `apartment-query-control-off` | disabled | 12.7173 ms | 11.8885 ms | 18.5546 ms |
| `apartment-query-control-on-2` | enabled | 12.0962 ms | 11.4891 ms | 18.1859 ms |

CPU-only mode produced zero GPU samples as expected. Disabling queries did not improve throughput in this control. This does not quantify every profiler cost; per-lookup timing has separate overhead.

## Thread CPU residency and routing

`apartment-thread-cpu`, PID 21284, CPU-only metrics: the coarse CPU window was 31046.08 ms. Thread 30312 used 29718.75 ms CPU (95.72% of one core); queued eye-marker logs identify it as the render thread. Thread 17844 used 26390.62 ms (85.00%); its role is not yet identified. Thread 8284 used 6937.50 ms (22.35%). CPU residency includes spin/poll work, and is not useful-work-only cost or physical-core occupancy. Core migration can spread one busy thread across several core-utilization graphs.

Release `788096083E62E135D28088C68613C7A6119FAAF49D52038467086F4AD78C40F5`, PID 30216, `apartment-routing-baseline`: texture/surface alias lookup totals have medians 0.11 / 0.10 ms per eye, a small part of the frame budget. The gap outside the eye streams is median 4.42 ms / p95 11.57 ms; inter-eye gap median 0.04 ms. The outside gap includes publication, OpenXR transfer/submission, desktop presentation, shared/auxiliary commands and waits for future scene work. It is **not** native simulation time.

The next bounded experiment suppresses implicit D3D11 flushing during completion polling after the existing explicit End/Flush. Completion must still return S_OK before swapchain release; the one-second deadline and failure fallback remain. [Microsoft's flag documentation](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/ne-d3d11-d3d11_async_getdata_flag) warns that polling without flushing pending commands can fail to make progress, which is why the explicit submission barrier is retained. This is a diagnostic comparison, not a default runtime change.

The street routing/thread capture (PID 30216, user-positioned exterior view) measured mean 14.9202 ms / median 14.0344 ms / p95 18.6372 ms submission intervals. Eye command medians were 5.81 / 5.32 ms; routing was only 0.12 / 0.12 ms. The busiest thread used 96.74% of one core's CPU time. This strengthens the case for CPU/driver execution and synchronization attribution rather than a texture-alias cache as the principal optimization.

## Native CPU sampling

Installed xperf rejected a direct sampling start (`0x3ec`); WPR rejected its CPU profile with `0xc5585011`, failure to enable system-performance profiling policy. This process is not running as administrator. No recording was started; `wpr -status` confirmed that none remained. No system policy was changed.

`tools/capture-native-cpu.ps1` prepares a bounded 30-second WPR CPU trace from an elevated PowerShell. It refuses to run without elevation, refuses to join an existing WPR recording, requires focused game stereo, and stores raw trace plus process/module-base metadata only under ignored `build/profiles/native/`. Its syntax and non-elevated rejection were verified; successful elevated recording is pending. The user offered to launch an elevated process and has been given the helper command. CPU stack sampling is needed to distinguish native game, FusionFix and driver hotspots; thread occupancy alone cannot do that.

The initial `gameUpdate` timer targeted a live RET stub, verified and bookmarked in Ghidra. Its replacement `processStub` and `processCallbacks` fields do not measure native simulation; finding a supported simulation boundary remains part of issue 013.

### Elevated street capture, 2026-10-04

The user completed `street-20261004-184231.etl` with WPR elevated. GTA IV CE 1.2.0.59, Release ASI `0A89B250AD02F65855F14B836BF3168C7B252E38770B4B46B930DAC748344074`, PID18212, live image base `0x006D0000`, SteamVR2.17.10 at90Hz, current1920×1080 eye targets. OpenXR was FOCUSED before and after; no command-buffer overflows. The supplied Frame Timing image shows a large red Late Start band, approximately7–9ms in many displayed frames, and small application GPU bands. This screenshot is descriptive evidence, not a percentile measurement.

The ETL contains50.69seconds including stop/rundown, despite the requested30second recording. Analysis below restricts to the first30seconds, GTAIV PID18212, render thread26952 (identified by queued eye-marker logs), and **Sampled Profile** events. Unfiltered butterfly reports also include context-switch stacks and must not be treated as CPU samples. The trace reports zero lost events/buffers. Local matching FusionFix PDB resolves our functions; game/driver functions mostly remain unnamed. The PowerShell metadata module list omitted most32-bit modules under WOW64; use ETL image rundown for module attribution rather than that incomplete list.

Render-thread report contains28000 CPU stack samples. Percentages describe sampled execution, including spinning and driver work; they are not percentages of frame wall time or blocked-wait durations.

| Module containing sampled instruction | Exclusive samples | Render-thread sample share |
| --- | ---: | ---: |
| D3D9.dll | 7991 | 28.54% |
| GTAIV.exe | 7721 | 27.57% |
| 32-bit ntdll.dll | 5559 | 19.85% |
| Windows kernel | 1757 | 6.28% |
| NVIDIA D3D9 user driver | 1334 | 4.76% |
| FusionFix ASI | 1089 | 3.89% |

Shader-constant paths are specific next targets: `SetVertexConstants` has2353 inclusive samples/8.40%, versus27 exclusive samples; `SetPixelConstants` has2345/8.38%, versus35 exclusive. Most cost attributed through these hooks belongs to the underlying D3D9 calls. `OnEndScene` is7.38% inclusive; `PublishEyes`3.01% inclusive. Do not add these to the module shares or other overlapping parent spans. The sampled ntdll addresses `+0x5348E` and `+0x54405` fall after local exports RtlLeaveCriticalSection and RtlEnterCriticalSection, respectively, with D3D9 callers. This supports inspecting device locking/state churn, but does not establish lock contention duration. Removing device thread safety without verifying ownership would be unjustified.

Live native return sites add context:

- Ghidra `0042623A` calls `[edx+0x148]`, returning at `00426240`; that return site has4325 inclusive samples/15.45%. Offset0x148 suggests D3D9 DrawIndexedPrimitive slot82; the live device/vtable identity is not yet verified.
- Ghidra `0043199F` calls the previously verified viewport dependent-matrix routine `00430F00`, returning at `004319A4` (3593 inclusive samples/12.83%). This update can upload global camera constants; the percentage does not imply expensive matrix arithmetic alone. The imported overlapping decompilation at FUN_00431922 is invalid for this live code.

Both findings were previewed as `Issue013CpuSampling` bookmarks, applied, read back and saved in the Ghidra project. No destructive reanalysis or speculative renaming was performed.

### What Late Start tells us here

Valve exposes pose-call time and frame-ready time separately in its [compositor timing documentation](https://github.com/ValveSoftware/openvr/wiki/Compositor_FrameTiming). In our current bridge, D3D9 scene execution and eye publication occur **before** `xrWaitFrame`, followed by view location, D3D11 copies and submission. Thus the screenshot is consistent with substantial work preceding the runtime frame boundary. Small SteamVR application GPU bands do not establish that the separate D3D9 producer is cheap; internal D3D9 timing remains necessary. Late Start alone does not prove a60FPS cap, a GPU bottleneck, or a broken OpenXR runtime.

Next experiments should count and time native shader-constant updates, identify exact duplicate writes while preserving eye/shadow overrides, and compare a scoped reduction in Release in the same held street view. Separately, investigate placing OpenXR frame wait/pose acquisition before the corresponding eye work, with explicit ownership across simulation/build/render threads. Moving the wait merely to make the graph look better is not a performance result; require measured submission intervals, pose age and unchanged images. No native-refresh improvement is claimed from this trace.

## Vulkan prototype comparison

2026-10-04, same game/runtime/headset/GPU, Release `CCAC483C5DE0E7E65AFF7F3570538C244EF70F77C860A7BA9AA5DE8487158704`,1920×1080 eye images. The experimental same-device DXVK v3.1.1 Vulkan bridge uses the existing stereo scene/camera logic. The user confirmed live, correct images in both eyes and head movement. Missing queue locks around OpenXR begin/end frame and acquire/release were corrected after initial device losses. Default DXVK descriptor heap was restored, temporary compatibility config removed and `DXVK_DEBUG` diagnostics disabled for measurements. Explicit bridge quit destroyed runtime objects successfully and GTAIV actually exited; subsequent Vulkan launches reached focused stereo.

The user left the headset stationary and avoided input across Vulkan/system/Vulkan relaunches into the same save. Each capture is30seconds, CPU-only (zero GPU query samples), FOCUSED before/after, no drops/command-buffer overflows, with the ordinary gameplay limiter bypass active. No builds or screenshots ran during the captures. Fresh pre-capture images show approximately the same apartment door/body view; model-reference peak375 and command-buffer peaks about595KB corroborate similar content. The resting headset angle places much of the body in view. This is a controlled backend diagnostic, not a representative standing apartment or street benchmark; small tracked-pose/image differences remain.

| Backend/run | Submission intervals | Mean ms | Median ms | p95 ms | p99 ms |
| --- | ---: | ---: | ---: | ---: | ---: |
| Vulkan A1 (`vulkan-save-control-a1`, PID20256) |2695|11.1232|11.1235|12.1646|12.7820|
| System B (`d3d9-save-control-b`, PID25816) |2073|14.4609|13.6177|18.6599|19.0781|
| Vulkan A2 (`vulkan-save-control-a2`, PID25908) |2697|11.1144|11.1009|12.2109|12.8140|

Vulkan reduces mean submission interval by about23% in this held scene, averaging approximately90 submissions/sec versus69 on the system bridge. The near-identical Vulkan repeat supports a backend effect here. p95 still exceeds11.11ms; this is not proof of zero missed compositor frames or all-scene native90Hz.

Per-eye CPU command medians were2.82/2.52ms (A1),4.21/3.97ms (B),2.87/2.64ms (A2), supporting lower command-processing cost. The Vulkan first-eye copy span median2.71/2.50ms exceeds B's0.22ms, while its p95 4.25/4.09ms is below B's6.60ms. These spans cover different synchronization placement: Vulkan includes DXVK producer completion/queue locking, whereas system publication has an earlier D3D9 fence. Do not label the entire improvement a faster pixel copy or sum overlapping spans.

Earlier uncontrolled live street Vulkan capture (`vulkan-street-initial-diagnostic`, PID24972) averaged14.7629ms with p95 20.8709ms. It cannot be compared directly to earlier system street captures because views/time/traffic differ; it establishes that Vulkan alone has not solved outdoor performance. Next: matched street/vehicle captures, backend GPU/native CPU attribution, reset and runtime-failure tests. The distributed backend remains system D3D9Ex/D3D11; the local test install is left running the opt-in Vulkan prototype. Issue013 remains open.

### DXVK late-start attribution

Release timing instrumentation `66010A826AFDE1351575CAF2502ACFF6406301EFCA9EF2149F6CB115B3F3050A`, PID14212,2560×1440 eye targets. The user resumed live gameplay; this capture is an uncontrolled diagnostic, despite its original `vulkan-late-start-apartment-1440p` filename. A subsequent fresh eye capture shows a street/vehicle view. Do not interpret the filename as proof of apartment/held-view conditions or compare its throughput directly to the saved-view control.

| Wall-clock metric | Median ms | p95 ms |
| --- | ---: | ---: |
| Submission interval (1736 samples) |16.99|20.26|
| Eye0/eye1 commands |6.84 /6.23|8.48 /8.13|
| Eye0 execution through pre-xrWaitFrame |13.18|16.32|
| xrWaitFrame |0.02|0.02|
| xrBeginFrame including queue lock |0.17|0.22|
| xrLocateViews |0.03|0.04|
| Entire enabled runtime EndScene callback |2.32|4.39|
| First/second eye transfer |1.30 /0.30|3.41 /0.73|

The four runtime queue-lock acquisition medians were below0.01ms except release at0.02ms (p95 0.03ms); no substantial contention demonstrated there. Runtime focus stayed FOCUSED and no sample drops occurred. The source-pose prediction timestamp lag median33.32ms/p95 44.43ms is a prediction-time difference, not measured display latency. Major render-thread wall time precedes the runtime frame wait in this diagnostic; a CPU graph near11ms does not establish a complete11ms application frame. Native CPU samples on the DXVK build are still needed to distinguish useful frontend work, driver work and spinning inside the command spans. Moving frame boundaries requires ownership/pose validation and measured gains rather than graph appearance alone.

The user subsequently reported right-eye noise on roads/gray textures and zebra-crossing paint, possibly reflections. Cropped D3D9 producer readbacks show additional grain in the right-eye lit road before Vulkan copying. A fresh held crossing capture confirms the difference on the paint as well. The affected scene/post-processing pass remains unidentified; this retracts any assumption of complete graphical compatibility from the initial both-eye confirmation. A bounded AO isolation diagnostic is prepared, with a separate AO CPU span, but the installed saved FusionFix config has AmbientOcclusion=0, so AO is a weaker lead. Reflection/specular lighting and filtering inputs need isolation. No root cause or quality-preserving fix yet.

### Elevated DXVK street CPU trace

The user completed `dxvk-street-20261004-212510.etl` with the same running Release66010 build, PID14212/render thread25616, CE1.2.0.59 and SteamVR2.17.10. Continuous DXVK-Vulkan stereo remained FOCUSED before/after; no command-buffer overflows. Eye targets were2560×1440 in this run. The held street/vehicle view is not the earlier controlled1080p saved-view comparison. ETL duration50.76seconds includes rundown; analysis restricts to the first30seconds. Zero lost events/buffers.

The render-thread Sampled Profile butterfly report contains28717 samples: native game12972/45.17%, unresolved module10475/36.48%, kernel1832/6.38%, ASI1593/5.55%. The unresolved stack module has a0x780000 span matching the DXVK image size, but the butterfly decoder loses its name. Treat that association as provisional; do not silently label it a resolved DXVK function. The installed ASI's matching PDB was overwritten by a later local build before analysis, so ASI function attribution is unavailable here. Future captures record the installed ASI hash; preserve matching PDBs with deployment backups.

The independent, **all-game-thread** `xperf -a profile -detail -range 0 30000000` report resolves module names through image rundown:

| Module | Sampled weight, seconds | Share of GTAIV sampled CPU weight |
| --- | ---: | ---: |
| GTAIV.exe |23.82|33.81%|
| DXVK (`vulkan.dll`) |17.08|24.24%|
| Windows kernel |10.35|14.69%|
| NVIDIA Vulkan driver (`nvoglv32.dll`) |9.34|13.25%|
| FusionFix ASI |1.94|2.76%|

Total sampled weight70.46seconds across all game threads in the30second window. These weights include sampled spinning and other resident execution; they do not measure blocked waits, GPU execution or individual frame budgets. Do not combine the all-thread shares with render-thread shares or compare their percentages as a matched backend speedup. This supports profiling native/DXVK command production and the driver submission thread rather than assuming OpenXR queue locks dominate. Exact functions, state-update counts and duplicate writes remain to be measured.

### Native rewrite and parallelism candidates

Rechecked live CE viewport update00430F00 in PID14212 (base00980000). Its sampled caller RVA319A4 has4701/28717 inclusive render-thread samples,16.37%; this includes descendants and device publication. The update computes three dependent matrices, then tests whether the viewport is current before shader-state publication. Existing Ghidra Issue006 evidence agrees. The pure matrix multiply004307F0 was rechecked in PID1696/base00980000 and already uses aligned packed SSE. No evidence yet that replacing that math with another implementation or scheduling these tiny calls onto workers recovers meaningful frame time. A parallel implementation would need immutable input batches, separate output storage and publication on the owning thread; shared current viewport/D3D9 mutation cannot simply be run concurrently. New Issue013 bookmark00430F00 previewed/applied/read back/saved.

ReleaseA233445A adds a bounded shader-constant observer to measure source-call counts, requested vectors, repeated candidates and original device-call spans. DXVK v3.1.1 already rejects unchanged constant data before dirtying uploads, so any additional cache must justify avoided native preparation/locking/call overhead and cover state-block/override invalidation. [DXVK implementation](https://github.com/doitsujin/dxvk/blob/v3.1.1/src/d3d9/d3d9_device.cpp). No call suppression or parallel execution is implemented. Deterministic observer/profiler tests and Debug/Release builds pass; focused live capture is pending.

Held street shader-constant capture (`dxvk-street-constant-calls`, ReleaseA233445A, PID1696/render thread27924): user confirmed positioned before capture, FOCUSED before/after, CPU-only/zero GPU queries,1310 eye streams, no sample drops or command-buffer overflows. Eye command medians9.88/9.22ms, pre-xrWaitFrame19.24ms; submission median22.65ms/p95 25.19ms, mean22.90ms. This is instrumentation-on in another street view, not a matched speedup/regression against older captures. Observer overhead remains unquantified.

| Nested original setter wall time per stream | Median ms | p95 ms |
| --- | ---: | ---: |
| Eye0 vertex |0.58|0.64|
| Eye1 vertex |0.57|0.63|
| Eye0 pixel |0.67|0.74|
| Eye1 pixel |0.66|0.72|

Aggregate source-call counts across1310 streams: vertex19,667,284 /19,613,784 and pixel23,370,155 /23,237,469 for eye0/1. Requested vectors167,434,139 /167,360,989 and160,197,500 /160,045,164 respectively. Observer reported zero repeated candidates; do not interpret this as proof that no effective state is redundant, since it lacks full state-block/internal-write coverage and resets at phase boundaries. These instrumented original calls sum to about2.5ms at their individual medians, nested in the roughly19.2ms render span; summing medians is indicative rather than a measured paired distribution. Thus eliminating every measured setter span would still not meet11.11ms. G-buffer medians5.48/5.04ms and draw-scene3.95/3.76ms dominate; eye list building0.75/0.40ms and measured visibility0.02ms are smaller. Visibility does not cover all simulation work. Render thread consumed30.00seconds CPU in31.08seconds of client sampling,96.53% of one logical CPU; migration can hide this in individual core graphs.

The native matrix multiply's full live extent004307F0–004308E8 was established by its RET8 at004308E6. Aggregating exact sampled instruction RVAs in that range yields962/28717 exclusive samples (3.35%) in the earlier DXVK trace. This is all callers on the render thread, not just the viewport update. It is far less than the parent path's16.37% inclusive share and does not support expecting a large gain from a matrix rewrite alone. Native draw call0042623A was rechecked live: six D3D9-style arguments and vtable slot82, returning00426240. Direct device identity still needs object/vtable validation before a draw-level optimization.

### Constant observer overhead control

Release9372B6D8, GTA IV CE1.2.0.59, SteamVR2.17.10/90Hz, DXVK3.1.1, 2560×1440 per eye, PID30048. The user held a street view without input through consecutive observer-on/off/on captures. All were CPU-only, FOCUSED before/after, with no dropped profile samples. Ordinary profiling now leaves the detailed constant observer off; `-Constants` explicitly selects it.

| Capture | Submission intervals | Mean ms | Median ms | p95 ms | Pre-wait median ms |
| --- | ---: | ---: | ---: | ---: | ---: |
| Observer on (B) |1496|20.02|19.82|21.63|16.01|
| Observer off (A2) |1789|16.75|16.61|18.10|12.55|
| Observer on (B2) |1468|20.41|20.17|22.28|16.30|

The observer adds about3.3–3.7ms to the mean interval in this view. Its per-call timing/comparison cost materially distorts the rendering profile. Inactive gameplay already kept the observer off, so this is an instrumentation correction, not a demonstrated gameplay speedup. Earlier A233445A capture results remain diagnostic and must not be treated as an uninstrumented baseline. Traffic/time-of-day variation remains a limitation of a held live scene.

Local files: `dxvk-observer-control-b.json`, `dxvk-observer-control-a2.json`, `dxvk-observer-control-b2.json` under ignored `build/profiles/`. The initial client had a PowerShell case-insensitive naming collision between the `Constants` switch and result variable. The B result was recovered from the completed in-memory epoch before starting A2; it lacks original client thread metadata. A1 was not recovered and is excluded. The result variable is now `constantResults`; A2/B2 saved normally. Off capture has zero constant observer metrics/counters. On captures again reported zero repeated candidates, subject to the same incomplete-state-coverage limits above.

### Right-eye noise investigation

Road/gray/crossing grain is visible in the producer's final D3D9 eye readback before Vulkan transfer. FusionFix AO and ShadowFilter are both0 in this test. Bounded native pixel shader assembly logs show packed normal decoding and screen-position-dependent shadow sampling; neither proves the cause. Raw native shader assembly and screenshots remain outside Git. A new bounded readback captures per-eye G-buffer0/1/2 with explicit supported color encodings, triggered by `stereo-test`; it does not alter draw/state routing. Do not trigger this diagnostic during timing captures.

Release5044DA14 (Debug/Release and profiler lifecycle tests passed) reached focused continuous Vulkan stereo in PID29564 after retrying the launcher's initial error. The initial apartment capture produced all six2560×1440 format21/A8R8G8B8 files successfully, with distinct per-eye geometry visible in albedo/normal/specular-color displays. Matching ASI/PDB and initial images remain under ignored `build/issue013-lighting-input/`. This validates the capture mechanism, not a noise fix. Comparing inputs against final lighting at an affected road surface is pending user positioning.

Held affected view captured at2026-10-04 22:30+02:00, Release5044DA14/PID29564. Both final eye images and six G-buffer inputs have matching fresh timestamps. The view is through a vehicle windshield at a sloped gray road. Right-eye grain is already markedly stronger in G-buffer1 RGB and remains after applying the observed native packed-normal decode to all four raw8-bit channels. Albedo/G-buffer0 and specular-color/G-buffer2 do not show comparable added noise in the same road crop. This narrows the investigation to normal production/input state; it does not establish a particular shader, sampler or state leak. Local copies/crops are ignored under build/issue013-lighting-input/noisy-held. No image assets are committed.

Follow-up actual-device diagnostic Release6A386E9A/PID25480 recorded up to64 pixel-shader/texture tuples per eye,32 shader assemblies and sampler/render/PS constant state. A held second affected view matched64 tuples by shader and sampled texture slots actually declared in that shader. Fourteen matched observations differed in sampler1/2 MIPFILTER: eye0 LINEAR(2), eye1 NONE(0). Used sampler MIN/MAG and sampled render states did not show comparable discrepancies. Unused slots contain eye-specific target aliases and are excluded from tuple matching; shader slot10 is outside this diagnostic's sampled0–7 range. First observations are not a complete per-draw state history. The native shaders sample normal maps on s1 in the implicated variants. The first eye's lighting/transparency work can leave mip filtering disabled before the second eye's geometry, while cached native commands omit restoring it.

ReleaseB6D5A3C3 restores actual first-eye entry mip-filter values before the second-eye G-buffer pass. It reads16 pixel sampler filters at each eye entry and writes only changed values. It preserves the game's selected policy and leaves camera/constants/native cache untouched. Debug/Release passed; clean deployment/FOCUSED continuous stereo reached PID24788 with no command-buffer overflow. Warmed capture matched64 sampled tuples with zero used-sampler differences. Local post-change images/state are under `build/issue013-normal-mip/warmed-after/`. The visible street image appears clean, but it is another scene and is not a matched before/after quality or performance test. User headset confirmation at an affected texture is pending; #013 remains open.

Subsequent user headset confirmation: “yes, it looks like it's fixed now.” Saved final held-view images and native state at22:44+02:00 under ignored `build/issue013-normal-mip/confirmed-after/`. This accepts the right-eye grain correction on tested surfaces; it does not establish full weather/material compatibility or an FPS gain. #013's frame-budget work remains open.

## CPU/GPU cross-check after the noise correction

2026-10-04, ReleaseB6D5A3C3/PID24788/render thread19852, CE1.2.0.59, SteamVR2.17.10/90Hz, DXVK3.1.1, 2560×1440 per eye. User confirmed a held street view without input. Two consecutive30-second captures: ordinary profiling with GPU timestamps, then CPU-only control. Both FOCUSED before/after, no sample drops, constant observer off/zero constant samples, no gameplay/safety limiter calls. Windows GPU Engine utilization sampled at1second intervals alongside both runs; no source/build/image capture changes during measurement.

| Metric | GPU queries on | CPU-only control |
| --- | ---: | ---: |
| Submission interval mean ms |27.13|26.37|
| Submission median / p95 ms |26.83 /29.41|26.09 /28.42|
| Eye0 / eye1 command medians ms |11.17 /10.44|10.92 /10.15|
| Render-before-wait median ms |21.73|21.21|
| OpenXR frame-wall median ms |1.86|1.80|
| Outside-eye gap median ms |5.00|4.85|
| Render-thread CPU, percent of one logical CPU |96.81|97.25|
| Game graphics_1 engine mean utilization |31.93%|32.52%|

The on capture has1104 submission intervals and1105 GPU spans per eye, with eye GPU medians11.50/10.69ms. The control has1136 intervals and zero GPU samples as intended. The roughly0.76ms mean difference is small compared with the missing frame budget, but only two live-scene runs cannot isolate query overhead from traffic/time variation. No new FPS improvement claimed.

Each GPU activity run has32 samples for the game's graphics_1 engine; peak35.40%/33.78%. Game copy engine averages4.37%/4.25%; game3D engine1.24%/1.50%. Summing process samples for each individual engine gives overall graphics_1 means31.93%/32.52%, and overall3D means18.23%/15.69%; competing processes did not show a saturated reported engine. Do not add utilization percentages across different engines: they can execute concurrently or share physical cores. Windows GPU scheduling statistics cover Vulkan as well as DirectX; [Microsoft's engine/counter explanation](https://devblogs.microsoft.com/directx/gpus-in-the-task-manager/). These are engine activity counters, not shader occupancy, per-draw GPU time or a guarantee that no GPU-dependent stall exists.

The combined observations support a CPU-side rendering-command throughput/serialization limit in this view, rather than continuous GPU saturation. GPU timestamp spans can include command starvation and should not be added to CPU spans as another independent cost. Render-thread CPU residency can include spinning; native/DXVK/driver processing and CPU synchronization remain to be separated. The earlier native stack trace supports substantial game and DXVK execution, but is another view/build. Source inspection confirms two sequential world command streams, with shared shadow/reflection/UI phases built only on the first pass; no third full desktop world stream is established. `xrWaitFrame` median0.02ms and later OpenXR work around1.8ms cannot explain the roughly21ms earlier eye execution. Investigate native G-buffer/draw-scene command execution and batching before assuming a Vulkan image-copy or pixel-fill bottleneck.

Ignored evidence: `build/profiles/dxvk-cpu-gpu-crosscheck.json`, `dxvk-cpu-gpu-control.json`, paired `*-activity.json` files, and `dxvk-cpu-gpu-summary.json`. GPU activity collection adds observer work to the host; treat these as diagnostic holds, not a final route benchmark. No new source changes or deployment in this cross-check.

## Sparse draw observer warm-up

Release6F36EA8E/PID27132, CE1.2.0.59, SteamVR2.17.10/90Hz, DXVK3.1.1,2560×1440 per eye. After recovery from repeated startup resets (also seen with preceding B6D5A3), a30-second apartment warm-up recorded2623 streams in each eye/phase, no sample drops or failed draw calls, and zero GPU/constant samples as required by draw mode. All observed original DrawPrimitive/DrawIndexedPrimitive calls were forwarded; approximately1/64 were timed. Mean sampled original device-call wall times were0.22–0.31microseconds. Average calls per stream were1743/1763 G-buffer,44/45 lighting,677/635 draw-scene for eye0/1. This validates counter coverage, not an optimization or a representative street result. UP/bypassing calls and native preparation before the API entry remain outside the draw counter.

Submission mean11.4323ms, median11.1794ms, p95 13.3439ms; eye-command medians3.1099/2.9135ms. No matched observer-overhead control yet; do not compare this apartment view to earlier street holds as a speedup. The headset was not user-confirmed held for this warm-up. Fresh post-capture desktop inspection shows apartment geometry, with character mesh clipping at the resting headset position; this is not a new first-person quality acceptance. Raw evidence stays ignored at build/profiles/dxvk-draw-apartment-warm.json. Debug/Release builds and profiler lifecycle/bounds/epoch tests passed before deployment. Pending: user-held street ordinary/draw/ordinary comparison, then attribution of native command preparation versus driver call spans.

## Held street sparse draw control

2026-10-04, same Release6F36EA8E/PID27132, CE1.2.0.59/SteamVR2.17.10/90Hz/DXVK3.1.1,2560×1440 per eye. User positioned a typical street view; three consecutive30-second CPU-only captures, observer off/on/off, without relaunch/settings/build/image capture. FOCUSED before/after each, no dropped samples or command-buffer overflows; GPU queries, constant observer and AO bypass off. Final profile-status confirms capture inactive and draw selection off.

| Metric | Off A | On B | Off C |
| --- | ---: | ---: | ---: |
| Submission mean ms |12.86|12.90|12.65|
| Submission median ms |12.04|12.11|11.85|
| Submission p95 ms |18.17|18.13|18.18|
| Eye0/eye1 command medians ms |4.05/3.65|4.13/3.73|3.99/3.61|
| Render-before-wait median ms |7.80|7.97|7.70|
| OpenXR frame-wall median ms |3.20|3.13|3.22|

B recorded2324 streams per eye/phase, zero failed draws; timed1.561–1.564% of calls. G-buffer draws per stream1492/1440, lighting226/216, draw-scene571/538. Sampled original device-call means0.282/0.259microseconds G-buffer,0.279/0.274 lighting,0.339/0.329 draw-scene. Maximum sampled span0.0566ms. These are original draw API wall spans with timer overhead, not GPU work or all DXVK work; sparse sampling can miss rare stalls and excludes UP calls. The observed draw APIs are too short to directly explain most of the enclosing7.97ms execution in this hold. Native preparation/state updates and deferred DXVK work remain to be attributed. No calls removed or parallelized.

B mean interval was0.04/0.25ms above the two controls; eye command medians about0.08–0.14ms higher per eye. This control does not show the multi-millisecond observer penalty seen with full constant observation. Live traffic variation prevents a precise overhead estimate or claiming zero overhead. This street view is substantially lighter than the earlier27ms cross-check; do not interpret that difference as an optimization. B first-eye copy median1.93ms/p95 7.82ms and second-eye0.41ms/p95 1.21ms retain a completion tail; xrWait median0.02ms. Transfer completion and command preparation both deserve further attribution. Top thread19056 used94.0% of one logical CPU over the coarse client window; identity requires matching native marker evidence.

Ignored captures: build/profiles/dxvk-street-draw-control-a.json, dxvk-street-draw-observer-b.json, dxvk-street-draw-control-c.json. Next: sample the state/binding-update paths with the same sparse strategy and preserve native API arguments/results; compare against this observer-off control before any suppression or native rewrite.

## Sparse state/binding attribution

2026-10-04, Release2DAD2A21/PID18996, CE1.2.0.59/SteamVR2.17.10/90Hz/DXVK3.1.1,2560×1440 per eye. User confirmed a held street view. Consecutive30-second CPU-only captures, state observer off/on/off; FOCUSED before/after, no metric drops/command-buffer overflows, GPU/draw/detailed constant observers off. A preceding moving/positioning coverage capture validated all20 category/eye pages but is excluded from performance comparisons.

| Metric | Off A | On B | Off C |
| --- | ---: | ---: | ---: |
| Submission mean ms |29.80|30.62|29.76|
| Submission median ms |29.52|30.39|29.49|
| Submission p95 ms |32.24|32.86|32.28|
| Eye0/eye1 command medians ms |12.12/11.32|12.50/11.72|12.07/11.32|
| Render-before-wait median ms |23.56|24.36|23.55|
| OpenXR frame-wall median ms |1.73|1.74|1.73|

The sparse state mode adds approximately0.82–0.86ms total submission time in this hold; do not present its30.62ms as observer-free gameplay. Live scene variation still limits precise attribution. The observer-off controls retained the five new diagnostic detours in this first implementation; removing those from ordinary gameplay is being tested separately. No source/build/image changes during measurement. This view is heavier than the earlier12.9ms draw test, so the change across those different views/builds is not a regression or speedup measurement.

B recorded979 whole-eye streams per category and no failed observed calls. Eye0/eye1 calls per stream: texture7276/7023, render state7754/7446, sampler state790/785, vertex shader2155/2083, pixel shader2215/2140, vertex float constants26454/26393, pixel float constants30063/29887, stream source7515/7248, indices6731/6483, vertex declaration1546/1505. About1/64 original calls timed. Sampled means mostly0.04–0.09microseconds: constants about0.044, texture0.073–0.075, stream/index0.081–0.095. Timer overhead is included and substantial at this scale. Counts times sampled means would be estimates, not measured full-stream durations; rare stalls/unhooked/internal paths are excluded. A count alone does not prove a call is redundant or safe to suppress.

These original state/binding APIs do not explain the full23.6ms enclosing execution; native command preparation, VR wrapper work and deferred DXVK processing remain relevant. Direct VR trampoline writes and routing/shader correction are outside these sampled original-call spans. First-eye copy median0.82ms/p95 about1.4–1.5ms; second eye0.29ms/p95 about0.7ms, so image-transfer/runtime work is comparatively small in this hold. Next: keep observer-only detours absent outside explicit captures, then investigate native execution with the existing CPU sample evidence rather than blindly skipping state calls. Raw captures stay ignored: build/profiles/dxvk-street-state-control-a.json, dxvk-street-state-observer-b.json, dxvk-street-state-control-c.json and dxvk-state-coverage-warm.json.

### Observer-only hook cleanup and native follow-up

Built x86 Debug/ReleaseC131BB36 (SHA256C131BB36E8BEA0B8D2945527FF82CA9876D69FDFECB7C47B8ED6B25AC31C227E) and deployed after bridge quit/verified exit, with matching ASI/PDB and prior ASI/INI saved in ignored build/issue013-state-lifecycle. PID7388 reached FOCUSED continuous Vulkan stereo. Live lifecycle check: explicit state start logged hook installation and published texture/render-state/stream-source counts with zero failed calls; stop/ordinary restart logged removal and reset state pages to zero. Final profile stopped. The five diagnostic-only detours are absent outside active state capture. This removes avoidable diagnostic detours from normal rendering, but no matched FPS gain is claimed; the previous held-view results remain attributed to2DAD2A21. Debug tests cover mode/bounds/epoch publication; runtime verifies activation/removal. New hook code remains behind opt-in stereo allocation, with no new flat-screen acceptance claimed.

Bounded live read-only inspection with the existing inspect-game-code tool in PID7388/base00B00000 established shader parameter dispatch ranges: Ghidra00437170–004372A9 (RET12 at004372A6),004372B0–004373DF (RET12 at004373DC). Each caches a shader pointer before a vtable shader-binding slot (+170/+1AC) and walks two-byte parameter mappings, then dispatches constants/textures based on per-parameter metadata. The first loop's slots+178/+180/+188 are consistent with VS float/int/bool constants. Full device identity/metadata types remain unverified. Static imported bytes are encrypted and not suitable for confident decompilation/flow repair. The old DXVK ETL's exact exclusive instruction-RVA sums give325/28717 (1.132%) and315/28717 (1.097%), about2.23% combined, excluding descendants. A rewrite of these loops alone has limited demonstrated potential. Native hotspotRVA9F969A is inside a memcpy/memmove-style overlap-aware routine using REP MOVSB in this live run, not proof of a large independent game subsystem cost. No native game function patched.

Ghidra project confirmed GTAIV:/GTAIV.exe,32-bit x86/base00400000. Searched existing annotations; previewed/applied/read back Issue013StateDispatch bookmarks at00437170/004372B0 and save_program succeeded. Local live listings remain ignored at build/profiles/native/issue013-hot-*.txt. Next attribution should cover the remaining native render-command execution and deferred DXVK processing, rather than treating sampled original draw/state API spans or these small loops as the entire bottleneck.

### Raw DXVK samples and constant-buffer experiment (2026-10-05)

Extracted ETL time1–2 seconds as raw events, retaining the sampled instruction address rather than treating the unresolved module as one function. PID14212/render thread25616:961 CPU samples,328 within vulkan.dll at base662C0000 (34.13% of this one-second slice). This is a diagnostic subset, not a replacement for the30-second module totals. Hot256-byte regions includeRVA38000 (28),20200 (28),14E00 (26),4B8100 (21),36E00 (17). Counts are samples, not calls or milliseconds.

Extended tools/inspect-game-code.cpp with an optional loaded module filename; it remains a bounded read-only inspector and uses synthetic analysis base00400000 for DLL listings (these are not GTAIV Ghidra addresses). Built the hostx64 tool; actual process code isx86. Rejected zero-byte and unknown-module requests. Initial x86 link to an oldx64 Zydis object failed; corrected host build, no game dependency changed.

Live PID7388 real-device global01FDD464, obtained from the existing FusionFix device-creation signature, points to2E6B79C0/vtable672ED7B0. DXVK base66C50000: slot104=RVA20200 (SetIndices), slot94=36DD0 and109=37F40 (vertex/pixel float constants), slot65=15490 (texture entry), slot82=34810 (indexed draws). Hooks alter entry prologues; inspect unchanged interior ranges/source together. Constant setters show SSE exact-data comparisons;RVA4B8180 contains MOVNTPS writes consistent with DXVK's constant-layout copy. Texture binding changes resource references and hazard/dirty masks. No blanket redundant-state suppression added.

Native004261BE dispatches slot104 via the game wrapper and returns004261C4; the old raw stack pairs DXVK+20253 with GTAIV+261C4. Previewed/applied/read back Ghidra Issue013DxvkBinding bookmark and save_program succeeded. Imported encrypted bytes remain untouched.

Experiment: [DXVKv3.1.1 constant buffers](https://github.com/doitsujin/dxvk/blob/v3.1.1/src/d3d9/d3d9_constant_buffer.cpp) select DMA streaming on discrete GPUs under Auto;False uses one host-visible buffer for CPU writes/GPU reads and omits streaming-copy commands. [Matching option documentation](https://github.com/doitsujin/dxvk/blob/v3.1.1/dxvk.conf) describes its GPU-bound benefit. TestFalse against defaultAuto using unchanged Release ASI, exact config backups, verified effective logs, warmed matching location/view, no instrumentation-heavy observers. It may trade CPU/transfer work for GPU read cost; no improvement is assumed. Config-only experiments do not establish a source-code speedup or all-street90Hz.

The user specifically authorized reading/storing/restoring test position after launch. Added bounded location IPC queued onto the simulation callback, native no-offset teleport/heading and collision request, plus a local capture/restore helper. Parser tests cover finite/bounded coordinates, strict four-value input and unchanged output on rejection. Runtime/build validation pending. It restores coordinates, not traffic/time/weather or the full scene; inspect fresh images and settle streaming before comparisons. Saves remain unchanged.

### Restored street constant-buffer control

All three runs use Release4D9954A3, GTAIV CE1.2.0.59, DXVKv3.1.1/SteamVR2.17.10,90Hz, same stored on-foot position/heading (1332.324097,-534.001953,14.841155,351.429352), continuous FOCUSED stereo,2560×1440 eye targets, CPU-only capture, GPU/full-constant/draw/state observers off, no concurrent builds/screenshots during30sec captures. Native position restoration was checked and streaming settled; screenshots have similar resting-headset low/body-clipping views. Traffic, scene time and minor pose differences remain uncontrolled. The initial pre-bridge street capture is a different view/lifecycle and not the A baseline.

| Metric (ms) | AutoA | FalseB | AutoC |
| --- | ---: | ---: | ---: |
| Mean submission interval |24.4130|24.5824|24.7664|
| Median submission interval |24.2599|24.4227|24.5865|
| p95 submission interval |26.0286|26.2665|26.5667|
| Mean eye0 commands |9.4882|9.4841|9.5536|
| Mean eye1 commands |8.8757|8.9397|9.0628|

No benefit established forFalse; ordinary configuration restored (dxvk.conf originally absent and now removed). Fresh B log confirmsFalse; C log confirms original effective config. No setting promoted. B's dominant thread used96.93% of one logical CPU during client window. All captures remain ignored at build/profiles/dxvk-constant-stream-{auto-a,false-b,auto-c}.json; ASI/PDB/backups/config/logs in build/issue013-constant-stream. Profiling infrastructure records are not a90Hz acceptance.

Next source experiment: per-call constant hooks queried thread identity and atomic observer flags even when no correction/observer was active. Marker-owned TLS selects constant observation and owns ray/cascade binding flags, matching existing render-thread ownership. An early path forwards every call with original arguments/results; correction and selected observer paths remain active. No state values cached/skipped; no native code replaced. Build/runtime/matched comparison pending.


### Constant forwarding runtime result (provisional)

Built x86 Debug/Release5DF3AA6A163B95A4D88BC93C78F72EEF9CE0513D439D0D5193AA08299E81C82C. Profiler lifecycle/percentile/concurrency tests pass. Matched ASI/PDB and prior4D9954A3 preserved at build/issue013-constant-fast. PID7380 restored the same street position/heading and reached FOCUSED continuous DXVK/Vulkan stereo. A short explicit constant capture observed102 streams per eye, nonzero vertex/pixel call/vector counts and zero repeated candidates; stop/ordinary CPU capture selected no observer. The early forwarding path preserves every device call and leaves correction/diagnostic paths active.

Ordinary CPU-only fastD capture: mean submission24.3735ms,median24.2047,p9525.9588; eye command means9.4227/8.8843ms,render-before-wait18.3899ms,OpenXR frame wall1.8596ms. Preceding controlC (same coordinates/view, separate process):24.7664ms mean,24.5865median,p9526.5667; eye means9.5536/9.0628ms,render-before-wait18.6992ms. About0.39ms total/0.31ms eye-execution difference overlaps the0.35ms AutoA→AutoC variation. No proven speedup or90Hz claim. No extra flat-screen launch for this guarded constant-hook iteration; issue closure still requires final shared/fallback validation.

Raw capture remains ignored at build/profiles/dxvk-constant-fast-d.json. Original DXVK config remains restored; profiling stopped, all observer selections off. Current run/build is held unchanged for a user-elevated native CPU recording labelled dxvk-street-forwarding, with exact matching symbols saved. The old trace's ASI symbols did not match; use the new recording to separate the remaining native command/DXVK cost before a larger rewrite. Awaiting recording, not closing#013.

### Forwarding ETL and diagnostic detour removal (2026-10-05)

Analyzed the user-recorded `dxvk-street-forwarding-20261005-003534.etl` with matching Release5DF3 symbols. First30 seconds, PID7380/render TID17544:28,962 sampled-profile hits, no lost events/buffers. Exclusive module shares: GTAIV50.58%, stripped/unknown32.19%, ASI6.40%, kernel5.80%. A separate raw1-second interval has971 render samples;292 map explicitly to `vulkan.dll`, corroborating DXVK attribution without treating every unknown address as DXVK. Thread-specific stack filtering must use `-event '.*Profile.*'`; literal `SampledProfile` produced an empty report. All-event stacks mix context switches and are not CPU sample percentages.

Matching ASI exclusive hits identify `VRProfile::Begin`429 (1.48%), diagnostic `DrawIndexedPrimitive`375 (1.29%), `MapTexture`172 (0.59%). Native004462F8 has818 (2.82%); live entry004462E0 appends a36-byte float record. Raw stacks return to Ghidra008DE0DC, runtime00FDE0DC at modulebase00B00000. Live008DE0D9 dispatches a command through virtual slot+4 before that return. Purpose remains unverified; no native replacement. Previewed/applied/read back `Issue013CpuHotspot` bookmark004462F8 and saved the Ghidra program.

Source change removes both diagnostic draw detours at the eye0 marker after initial/readback diagnostics finish, reinstalling for explicit draw observation or stereo capture. Draw arguments/results remain unchanged. Ordinary texture/surface routing avoids `VRProfile::Begin` via marker-owned TLS when no capture is active; active routing timing remains available. x86 Debug/Release12A58736 and focused profiler tests passed. Runtime PID18272 continuous Vulkan stereo; installed/restored location verified. Explicit3-second draw capture observed136 eye0 G-buffer streams,770,258 indexed calls, zero failures; logs verify install then removal. Matching ASI/PDB and preceding build/INI preserved locally in `build/issue013-draw-fast`.

Predeployment control mean submission20.4227ms; first new capture lost tracking and is diagnostic only. After headset tracking was restored, new mean21.9896ms, eye commands8.4400/7.9252ms, render-before-wait16.4499ms. Resting pose/traffic/lifecycle differ from control: these numbers establish no speedup. Add `-DrawForwarding` / `profile-start-cpu-drawforwarding` for a same-process ordinary/forwarding/ordinary comparison: hooks installed but per-draw counters/timers disabled. Client records exact start command. No90Hz acceptance yet; #013 remains open.

### Same-session forwarding comparison

ReleaseC03D08A1 (x86 Debug/Release passed; lifecycle tests include forwarding isolation/stop/restart) exposes an explicit diagnostic-only forwarding control. PID24752 restored the stored position; fresh window capture shows the tracked exterior, although the resting tilted/low headset still clips the character and is not a representative gameplay pose. No relaunch between the three30-second CPU-only captures; full state/constant/draw observers and GPU queries disabled. Forwarding mode records zero draw observer counts as intended. All runs ended FOCUSED with zero command-buffer overflows.

| ms | Detours off A | Forwarding B | Detours off C |
| --- | ---: | ---: | ---: |
| Mean submission |21.7001|22.1381|22.4910|
| Median submission |21.4046|21.7941|22.3102|
| p95 submission |24.0632|24.5819|24.6489|
| Mean eye0 commands |8.3525|8.5017|8.6739|
| Mean eye1 commands |7.8692|8.0219|8.1968|
| Mean render-before-wait |16.3051|16.6078|16.9546|

Costs rose monotonically: B lies between A/C, so no measurable detour-removal FPS benefit is established. Keep the removal because the diagnostics are unnecessary in ordinary rendering, not because these captures prove a speedup. Traffic/day-night/native workload and pose remain limitations. Raw `draw-detour-{off-a,forward-b,off-c}.json` and exact C03D symbols remain ignored locally. Game left PID24752, observer controls off; requested user-elevated `dxvk-street-clean-hooks` ETL to confirm eliminated exclusive hook costs and reassess remaining native/DXVK work with matching symbols. CPU recording now records profiler state and rejects an active in-game capture to avoid instrumentation contamination. No90Hz or issue-closure claim.

### Clean-hooks native trace (2026-10-05)

Received `dxvk-street-clean-hooks-20261005-010024.etl`, exact ReleaseC03D08A1. Metadata confirms profiler inactive/all observer selections off before/after, CE1.2.0.59, Vulkan/FOCUSED stereo, zero command-buffer overflows. First30sec/PID24752/render TID27896:28,548 sampled-profile hits; no lost events/buffers. Exclusive shares GTAIV46.82%, stripped35.46%, kernel6.06%, ASI5.64%. Indexed-draw diagnostic hook and `VRProfile::Begin` have zero sampled exclusive entries. This verifies removal, not an across-scene FPS gain: workload/pose/model-reference peak differ from the preceding ETL.

Largest remaining ASI exclusive entry is `MapTexture`:758hits/2.66%. It linearly scans the immutable eye-target inventory on every texture bind, including ordinary non-target textures. Native004462F8 remains hot (1075hits/3.77%); exact appended-record purpose still unverified, so no replacement.

Added a64-entry direct-mapped pointer routing cache owned by `Resources`. It stores original/left/right mappings and negative results. Full pointer keys handle collisions; no COM/device calls are suppressed. All special pointers stay alive in that immutable resource set; a newly allocated set gets an empty cache, preventing reset/pointer-reuse stale routes. Access follows existing render-thread routing ownership. Deterministic x86 tests cover each eye, ordinary textures, null/invalid-eye bounds, collisions and replacement ownership. CPU profiler adds explicit original-scan comparison mode `profile-start-cpu-texturelinear` (`-TextureLinear`); ordinary starts and stop/expiry clear it. Active CPU captures retain routing timers, ordinary gameplay bypasses profiler entry.

Debug/Release065F6645 and routing/profiler lifecycle tests pass (existing dependency PDB warnings only); client syntax and diff checks pass. Matching ASI/PDB and prior installed ASI/INI preserved at `build/issue013-texture-cache`. Deployed after bridge quit/verified process exit. Same-session cache/original-scan/cache runtime comparison pending; no speedup or90Hz claim yet.

### Texture-routing runtime result

Release065F6645/PID1916 restores native coordinates and remains FOCUSED/continuous. Fresh image shows the same tilted resting-headset/body-clipping exterior; no headset-quality acceptance claimed. First cache/linear/cache sequence is contaminated: our HTML-tail analysis reader became stuck and consumed a CPU. Its exact command/PID24944 were confirmed; Stop-Process failed, then Process.Kill plus exit verification succeeded. Discard these total-frame comparisons (`texture-cache-a`, `texture-linear-b`, `texture-cache-c`). No large trace processing, screenshots or builds ran during the repeated clean captures.

| ms | Cache clean A | Linear clean B | Cache clean C |
| --- | ---: | ---: | ---: |
| Mean submission |24.7640|24.8641|25.5496|
| Median submission |24.6003|24.7603|25.2223|
| p95 submission |26.8394|27.0851|28.3876|
| Mean eye0 routing |0.2589|0.2928|0.2667|
| Mean eye1 routing |0.2609|0.2949|0.2682|
| Mean eye0 commands |9.7309|9.7161|9.8109|
| Mean eye1 commands |9.2278|9.2115|9.4002|

Routing spans decrease by about0.026–0.034ms per eye (~9–12% of this instrumented span), repeatably on both cache controls. Total frame time does not improve consistently; native/scene/runtime variation dominates, and the benefit is tiny (~0.05–0.07ms per pair in measured routing spans). Retain bounded cache as a local reduction, not a90Hz/FPS claim. All clean runs FOCUSED, no overflows or dropped metric samples, heavy observers/GPU queries off. Diagnostic linear scan ended; ordinary cache remains active, exact symbols preserved. Larger native/DXVK bottlenecks remain. Ghidra clean-trace hotspot bookmark004462F8 previewed/applied/read back/saved; no native rewrite.

### Exact eye-union entity culling experiment (2026-10-05)

The first-image visibility union is a world-axis-aligned box around both eye frusta. This remains useful for conservative streaming, fine-grid traversal and interior camera-volume traversal, but admits spheres outside both eye frusta. Added an explicit `-TightVisibility` CPU-only capture mode: after the native sphere test, reject a recognized world-viewport entity only if its sphere is wholly outside both actual headset frusta (0.01-unit margin). Native nonzero classifications and headset near-plane distance output are preserved. Unknown/auxiliary viewport objects retain their existing behavior. Shadow/caster dependencies of shared world queries still require visual validation; this experiment is not promoted to normal gameplay.

Debug/Release03484B3B9414EED9D15022BBADC60DFA68BCA826E10CD559584A75D43FC80D03 built, focused x86 math and profiler lifecycle/isolation tests pass, capture script parses and diff whitespace check passes (existing dependency-PDB/module warnings). Preserved old installed ASI/INI and matching experimental ASI/PDB in ignored `build/issue013-tight-visibility`. PID17136 reached FOCUSED continuous DXVK-Vulkan stereo, native stored street position readback matched. A fresh image shows the exterior with resting-headset/body clipping; it establishes neither normal headset visuals nor representative player pose.

Same-session broad/tight/broad captures, no analysis/build/screenshot work during each 30-second window:

| Mean ms | Broad A | Exact eye union B | Broad C |
| --- | ---: | ---: | ---: |
| Submission interval | 26.3376 | 19.5246 | 26.4456 |
| Render before XR wait | 20.6824 | 14.5505 | 20.7768 |
| Eye 0 commands | 10.6093 | 7.4573 | 10.6591 |
| Eye 1 commands | 9.9890 | 7.0098 | 10.0333 |
| Eye 0 build | 1.3340 | 0.9224 | 1.3674 |
| Eye 1 build | 0.5789 | 0.3319 | 0.5937 |

Submission p95:28.2063/21.4662/28.3429ms. Captures1139/1536/1134 paired eye samples; all FOCUSED, zero command-buffer overflows. A and C recover similar costs, supporting a local reduction of ~6.8–6.9ms (~26% submission interval, ~30% eye command cost) in this held view. This is workload evidence pending image acceptance, not proof that the visible scene is intact or that all streets improve. Live traffic/time remain uncontrolled. Target11.11ms/90Hz still unmet. Captures are ignored `build/profiles/visibility-broad-a.json`, `visibility-tight-b.json`, `visibility-broad-c.json`.

Added development-only `visibility-test on` / `visibility-test off` commands to allow continuous visual testing without a 30-second timeout or relaunch. They alter no saved configuration and reset on stereo shutdown/device reset; default remains broad visibility. Do not mix a persistent visibility test with matched broad/tight captures. Continuous headset geometry/LOD/interior/shadow acceptance pending.
`tools/summarize-cpu-samples.cpp` is a read-only x64 host ETL reader (not an ASI dependency). Restricted to verified classic kernel PerfInfo provider CE1DBFB4-137E-4DA6-87B0-3F59AA102CBC, opcode46, native64-bit payload, selected thread and bounded first1–60seconds. Restriction leaves clean-hooks first30sec counts unchanged:28,736 sampled events/5,226IPs/no lost events. xperf stack-backed report has28,548 hits; those denominators differ and must not be mixed. Payload sample multiplicity and independent xperf event-count validation remain pending; current output counts events only. No native/backend rewrite follows merely from an IP count.

### Visual acceptance and default promotion (2026-10-05)

User confirms: "Yeah LOD, shadows and everything looks correct" in continuous exact-eye visibility buildC30B65AC. Promote exact union-of-eye sphere rejection to ordinary opted-in VR, retaining conservative streaming/fine-grid/interior traversal and existing native distance/classifications. No broader GTA IV/episode/weather acceptance inferred. Profiling control is now `-BroadVisibility` (`profile-start-cpu-broadvisibility`), temporarily returning to the original broad sphere acceptance; ordinary captures use accepted exact-eye rejection. Development `visibility-test off` forces broad acceptance, `visibility-test on` restores normal exact-eye acceptance. Shutdown/reset restores exact-eye default. These commands alter no saved configuration. Prior experiment descriptions above record the historical defaults at capture time.
Default promotion verified: x86 Debug/Release56A18C86DE55AE6DB8803D601876A862343926AFA56D95433DAB2B5B5475EA74, profiler lifecycle/isolation/concurrency checks and capture-script syntax pass, diff whitespace check clean. Preserved ASI/INI and matching ASI/PDB in ignored build/issue013-visibility-default. PID12360 reached FOCUSED continuous DXVK-Vulkan stereo; native stored street position readback matches. Ordinary CPU-only visibility-default-street capture:1,551 paired eye samples, submission mean19.3377ms/median19.0826/p9521.2949; eye commands7.4309/7.0466ms, render-before-wait14.5611ms, zero dropped metrics/command-buffer overflows. This agrees with the earlier19.5246ms experiment; across-relaunch scene/pose/time are uncontrolled, so no additional speedup is claimed. GTA IV CE1.2.0.59/SteamVR2.17.10/90Hz/DXVK-Vulkan, same graphics configuration. Fresh native CPU trace is needed to attribute the remaining cost after reducing geometry. #013 still open; no90Hz/episode acceptance.

### Native CPU trace after accepted eye culling (2026-10-05)

Received `dxvk-street-eye-culling-20261005-015148.etl`: PID12360/base00B00000, CE1.2.0.59, exact installed56A18C86, FOCUSED before/after, no command-buffer overflows, in-game profiler/observers inactive. First30seconds/renderTID25808:28,639 stack-backed sample hits,28,787 raw sampled events (different denominators), no event loss. Exclusive modules: GTAIV12,000/41.90%, stripped/unknown11,672/40.76%, ASI1,481/5.17%, kernel1,725/6.02%. Unknown samples require live/module corroboration, not automatic DXVK labeling.

Native RVA462F8 is691/28,639 (2.41%), lower share than the earlier clean broad-culling trace1,075/28,548 (3.77%). Workload/pose/time differ; percentages alone are not per-call timings. Texture-routing MapTexture730/2.55% remains the largest ASI entry. Live ASI RVA116940–116A82 shows disabled profiling still initializes/loads a temporary zero stamp; raw IP65FC6994 has629 hits. Added early unprofiled returns in MapTexture/MapSurface; every resource mapping and original device call remains intact. Active routing profiling intentionally continues to create stamps, so ordinary instrumented phase captures do not isolate this fast-path gain. No FPS gain claimed from this change alone.

DXVK3.1.1 index binding/private-reference path RVA2023B still618 raw events and texture path RVA14E7E435. These instructions follow locked private COM reference updates. They establish sampled ownership overhead, not contention or permission to remove atomic ownership. No backend lifetime operations changed.

The second CPU-heavy thread25816 has18,130 stack-backed hits: NVIDIA nvoglv32.dll47.72%, stripped/unknown36.71%, kernel11.10%, ntdll2.85%. An inclusive ntdll thread-entry stack accounts for90.83%; that is not90.83% exclusive ntdll computation. This is existing driver/backend worker activity, not an idle CPU available to receive arbitrary device calls. Respect queue/thread ownership when considering parallel work.

Raw render IP001B3112 has769/28,787 events (2.67%). Extended read-only inspector with bounded `--live address byteCount`: require a GTAIV module,32-bit address, <=64KiB and one committed executable region; no writes/injection/execution. Live bytes identify a SafetyHook-style SSE/general-register restore trampoline. Native entry0131F4F1 (Ghidra00C1F4F1) jumps001B3132, saved MULSS XMM0,[EBP-8] and continuation0131F4F6. Callback resolves65F55C90/ASI RVAA5C90; reads preference-style int and may set low XMM0 to1.0. Matches source/lodlights.ixx WaterMultiplierHook signature; callback symbol not independently resolved. Native function role remains tentative; no hook removed. Previewed/applied/read back/saved Ghidra Issue013WaterHookCost bookmark at00C1F4F1. This does not diagnose water GPU cost or the separate eye-reflection defect.

Debug/Release9AA25E8CF4380631E0DA4D7BBEDEDBB0DFF8DBDEE8776AC43A0EFE9BA88F3C87 and profiler lifecycle/concurrency tests pass (existing dependency-PDB/module warnings). Native/backend rewrite target remains open;90Hz acceptance unmet.

### Binding coverage and ordinary submission measurement (2026-10-05)

Release9AA25E8C/PID6028 restored the stored native street position. Same-session ordinary/state/ordinary means18.6920/20.4744/19.4608ms; p9521.0530/22.0929/21.0907ms, eye execution7.1251+6.7160 /7.9581+7.5563 /7.4900+7.1026ms. State mode1,465 streams per eye, all20 category pages populated, zero failed setters. Observer overhead~1.0–1.8ms plus live scene variation. No speedup inferred from cross-build controls. Ordinary render-before-wait13.9284/14.6761ms versus XR-frame wall1.9684/1.8850ms; first-eye copy completion0.9630/0.8756ms, second0.2510/0.2555ms. Interop completion remains a cost, but is smaller than eye execution here.

Per-eye mean setter counts in this scene: textures7,283/7,318; render state8,815/8,794; vertex constants10,889/10,841; pixel constants14,175/14,148; vertex buffers8,169/8,226; indices7,067/7,129. Sampled API calls roughly0.04–0.085microseconds each (timer overhead included). Extrapolated sampled device-setter sums~3.56/3.44ms per eye are estimates within enclosing commands, not exact disjoint totals. High call counts do not establish duplicate state or justify suppression; state-block/other writes and ownership/lifetime semantics require full coverage.

Added read-only `tools/capture-submission-rate.ps1`: profiler must remain inactive/same epoch; count actual stereo-layer submissions between ready/FOCUSED endpoints. Bounds interval by both named-pipe round trips rather than inventing server timestamps. No CPU/GPU attribution and no assurance of intermediate focus/pose stability. PID6028 ordinary sample1,589 submissions/~30sec estimated52.882Hz, bounded52.806–52.958Hz. This verifies current unprofiled throughput, not a matched fast-path speedup. Script parser passes; output ignored build/profiles/culled-routing-unprofiled.json. Phase/state capture files are culled-state-control-a/observer-b/control-c.json.

Next small forwarding change: recognize the routing thread from its verified eye-marker TLS token during a phase, avoiding repeated GetCurrentThreadId calls in texture/target/depth setters. Other threads/phase gaps keep original identity checks; resource generation invalidates tokens on recreation/shutdown. No graphics call/value/result suppressed. Kernel32 sampleIPs75FE41F0/75FE41F6 account for441/28,787 raw events; this does not prove those calls belong exclusively to routing. Benefit pending validation; larger command/driver binding work remains unresolved.
Routing owner fast path builds x86 Debug/ReleaseE508620961CA9F3E5A88679BD2DFFCCB846DD0631A7663A384AD3B08E9F89FD2. Preserved previous ASI/INI and matching new ASI/PDB in ignored build/issue013-route-owner; bridge quit and actual exit verified before deployment. PID1824 reached ready FOCUSED continuous DXVK-Vulkan stereo, exact native street restore accepted, fresh mirror image shows live exterior/resting-headset body clipping. Unprofiled30sec sample1,575 actual stereo submissions: estimated52.400Hz, RPC bounds52.309–52.491Hz, zero command-buffer overflows. Compared to prior52.882Hz across a relaunch, this does not establish a speedup or regression; live scene/time variations remain uncontrolled. In-game profiler stayed inactive/same epoch. Requested both-eye live-scene check for routing thread ownership; no new90Hz/flat-screen/episode acceptance claimed. Larger remaining target is reducing or batching scene command/state-binding work while respecting device/resource ownership.
User subsequently confirms the routing-owner build: "Yeah everything looks correct." Both-eye live-scene check accepted. Target90Hz remains unmet; keep forwarding changes while investigating larger duplicated command/state-binding costs.

### Backend source review and shared visibility cost (2026-10-05)

Inspected public DXVK v3.1.1 at b1a1c99ab52b687cf950d62c88bc2fa316b41663 (ignored build/dxvk-013). SetStateTexture and SetIndices already skip unchanged bindings; private COM references preserve deferred-resource lifetime. Removing atomic ownership or adding a duplicate pointer cache is not justified by the trace. No custom DXVK build deployed. This source has no multiview/viewMask/ViewIndex path. Native Vulkan multiview needs layered attachments, a view mask and view-indexed shader data; our D3D9 bridge currently exposes independent eye targets and OpenXR arraySize1 swapchains. A switch to Vulkan alone cannot collapse the two native eye command streams. A future prototype must cover per-eye constants, G-buffer/lighting samplers, view-dependent billboards, shared shadows/reflections, and alpha order before replacing the accepted path. See [Vulkan multiview requirements](https://docs.vulkan.org/features/latest/features/proposals/VK_KHR_dynamic_rendering.html), [ViewIndex](https://docs.vulkan.org/refpages/latest/refpages/source/ViewIndex.html) and [OpenXR array layers](https://registry.khronos.org/OpenXR/specs/1.0/man/html/XrSwapchainCreateInfo.html).

A bounded per-eye construction observer in Release4AF5214B/PID21304 saw zero recognized sphere queries during G-buffer construction across120 stereo builds at the restored street location. Thus tightening this hook to individual eyes during command construction has no demonstrated opportunity. Removed that observer; no visibility decision changed.

Analyzed the existing accepted-culling ETL's third CPU-heavy thread1300 with exact56A18C86 symbols:12,812 stack hits/no loss; native GTAIV59.08%, kernel12.84%, NVIDIA kernel driver12.29%, ASI8.62%. SphereCull925 inclusive/282 exclusive hits (7.22%/2.20% of this thread), SphereOutside192 exclusive1.50%. These are thread sample shares, not frame-time savings, and thread1300's full simulation role is not independently established. Earlier parse without matching symbol path remains ignored; corrected report is eye-culling-third-thread.html.

Live CE sphere query00431E40 (PID21304 module00B00000) reads only six planes+300..350 and writes only its optional distance output. Null-output branch00431F09..00431F7C returns0 when ordered dot+radius is negative, otherwise starts2 and sets1 when radius exceeds expanded distance. Result2 means center on/inside all planes; result1 is accepted with center outside a plane. It is not a fully-inside/intersect classification. Corrected the earlier bookmark shorthand through a previewed/applied/read-back/saved Issue013SphereClassification bookmark; static packed code was not reanalyzed.

Testing a guarded direct scalar classification to eliminate the redundant original query, trampoline calls and full viewport copy for recognized world queries. Precise ordered float math and deterministic x86 center/crossing/tangent/outside tests pass. Legacy native calls remain the default and reversible control; unchanged near-distance computation and exact-eye rejection follow either classifier. Bounded native/reference comparisons and matched Release timings pending; no gain or visibility acceptance claimed yet.

Release628B00ECA471E9E13D488BCD4998D345A3B49C5BFA60C2FA669A9D8C5939AEA0 built after Debug; deployed with prior ASI/INI and matching symbols preserved in ignored build/issue013-sphere-classification. PID4508 restored the same native street coordinates, ready FOCUSED DXVK-Vulkan. Legacy comparison120 builds/2,142,425 queries/zero classification mismatches; enabled-path comparison120 builds/2,168,412 queries/zero mismatches. Fast path coverage is corroborated by continued query-count growth with legacyCorrected counters held fixed. Fresh mirror shows the same upward resting-headset/body clipping view; this is not normal headset visual acceptance.

Same-session legacy/scalar/legacy CPU-only30-second captures, observers expired before each timing window and no build/analysis/screenshot work during captures:

| Mean ms | Legacy A | Scalar B | Legacy C |
| --- | ---: | ---: | ---: |
| Submission interval | 19.8253 | 19.8136 | 19.6575 |
| Eye 0 commands | 7.6340 | 7.6495 | 7.6175 |
| Eye 1 commands | 7.2381 | 7.2519 | 7.2046 |
| Render before XR wait | 14.9558 | 14.9848 | 14.9052 |
| Frame outside eyes | 4.9135 | 4.8730 | 4.7963 |
| XR frame wall | 1.8717 | 1.8621 | 1.8497 |

Submission p9521.9395/22.1957/21.5163ms;1,513/1,513/1,526 paired completed-eye samples, FOCUSED endpoints, zero dropped metrics/overflows. Main render thread6324~95.80/96.11/95.74% core; third heavy thread28576~32.12/32.12/32.20%. No demonstrated throughput or thread-CPU benefit. Do not promote scalar classification; restored legacy via test off and verified profiler inactive/all detailed observers off. Files sphere-legacy-a/sphere-scalar-b/sphere-legacy-c.json remain ignored. Keep bounded comparison as development evidence, not an accepted optimization. Native equivalence in these queries does not establish all-scene/episode behavior.

Next: build unmodified public DXVK3.1.1 with x86 MSVC Release symbols to resolve the large stripped render-thread cost before choosing a backend rewrite. Compiler/code-generation differences require validation; no performance comparison with the shipped GNU binary is assumed. Local Meson1.9.1/Ninja1.13.0 installed only in ignored build/dxvk-tooling; pinned Vulkan/SPIR-V/libdisplay-info/dxbc-spirv dependencies fetched, including the nested shader-translator header after its initial missing-header failure. Existing Vulkan SDK glslangValidator supplies shader compilation. No global toolchain install and no custom backend deployed yet.

Symbol build completed: unmodified DXVK3.1.1, MSVC x86 19.51.36260, Release optimization3/debug=false, PE014C; DLL SHA256BCC8A9708EEEA1E54AC80A8A9AD1B50D9C36B9EAA5F490F49476ABC9053377E8 and matching35,442,688-byte d3d9.pdb. Original vulkan.dll/d3d9.cfg/ASI INI, new DLL/PDB and exact628B ASI/PDB preserved in ignored build/issue013-dxvk-symbols. Verified process exit before replacing game-root vulkan.dll; no configuration changed. PID24500 fresh log confirms v3.1.1/x86 msvc19.51.36260, continuous FOCUSED DXVK-Vulkan reached4,500 stereo layers/zero overflows with native street restoration. Fresh image is comparable to prior resting-headset/upward body-clipping view; headset compatibility and new elevated trace requested, not yet accepted. No FPS gain inferred. Capture client now records installed vulkan.dll hash and parses. Reproduction/rollback instructions in frame-profiling.md; build artifacts/source checkout remain ignored.

Received user-elevated dxvk-msvc-street-20261005-025855.etl,1,203,765,248 bytes. Metadata matches PID24500/base00B00000/CE1.2.0.59, ASI628B00EC/backendBCC8A970, profiler inactive before/after, FOCUSED endpoints and zero overflows. User reports recording complete, with no new graphics problem reported; no broader visual acceptance inferred. Matching archived PDBs resolve both ASI and vulkan.dll symbols. Image rundown confirms ASI66140000..66B10000 and backend67030000..673C3000.

First30seconds/renderTID29580 (live eye-marker thread confirmed):28,542 stack-backed sample hits;28,728 raw sampled events/5,277IPs/no loss, separate denominators. Exclusive modules: DXVK12,425/43.53%, native11,437/40.07%, kernel1,745/6.11%, ASI931/3.26%, remaining unknown472/1.65%. Function rows below are exclusive shares of the whole render thread, not additive inclusive parent times or device-call wall durations:

| DXVK function | Exclusive hits | Share |
| --- | ---: | ---: |
| SetStreamSource | 1,370 | 4.80% |
| SetStateTexture | 1,290 | 4.52% |
| SetIndices | 1,046 | 3.66% |
| Atomic fetch_add helper | 754 | 2.64% |
| Vertex float SetShaderConstants | 566 | 1.98% |
| PrepareDraw | 513 | 1.80% |
| Constant-buffer allocation CS chunk push | 512 | 1.79% |
| DrawIndexedPrimitive | 495 | 1.73% |
| Pixel float SetShaderConstants | 492 | 1.72% |
| SetRenderState | 478 | 1.67% |
| Index-binding CS chunk push | 431 | 1.51% |

Native RVA462F8 remains675/2.36%, role still unverified. DrawIndexedPrimitive9.25% inclusive includes PrepareDraw/constant/binding work; it is not9.25% exclusive raster API time. Ownership atomics sampled here do not establish contention or justify deleting references. Shader/binding/frontend command work remains the larger target. Ignored reports dxvk-msvc-render.html/.json, dxvk-msvc-render-ips.txt and dxvk-msvc-images.txt retain exact evidence; old GNU traces cannot use this PDB.

Preparing a compiler-only experiment: same pinned source/configuration with MSVC /GL compilation and /LTCG linking. Meson b_lto=true alone did not emit those flags for this compiler backend; that preliminary artifact is excluded. Explicit flags are verified in the D3D9 compile command and link arguments, and linker reports code generation. x86 DLL4A8F369E87687E7C31A5734655A0511266EAAA4D025C47686AA8A58155B0BAB0/matching39,153,664-byte PDB preserved in ignored build/issue013-dxvk-lto-symbols. No rendering/ownership source edits. Baseline non-LTO capture completed and current native location saved; LTO deployed after verified exit, runtime/comparison pending. No optimization gain claimed yet.

## Compiler-only DXVK comparison

2026-10-05, CE1.2.0.59/SteamVR2.17.10/90Hz, ASI628B00EC unchanged, per-eye2560x1440. Restored the same native on-foot position from ignored dxvk-msvc-held-location.json after each relaunch; no save writes. CPU-only30-second captures, observers/scalar test off, FOCUSED stereo endpoints and no metric drops/command overflows. MSVC baseline BCC8A970 -> explicit /GL+/LTCG4A8F369E -> baseline BCC8A970. Both source builds are x86 Release; new LTO build and matching PDB archived locally. Fresh captures show the comparable upward/resting-headset street view (body clipping), which is not a normal headset-quality acceptance route. The repeat control needed window activation before capture; desktop focus is another across-relaunch limitation.

| Mean milliseconds | Baseline A | LTO B | Baseline C |
| --- | ---: | ---: | ---: |
| Eye0 commands | 7.9776 | 7.7879 | 7.7770 |
| Eye1 commands | 7.5026 | 7.3860 | 7.4094 |
| Render before XR wait | 15.5648 | 15.2559 | 15.2743 |
| XR frame wall | 1.9330 | 1.8657 | 3.1596 |
| Submission interval | 21.4600 | 20.5125 | 22.0486 |

Submission p9523.8376/22.5992/24.4638ms, completed-eye pairs1397/1461/1360. No reliable command-execution improvement: B and C are effectively equal, while XR wall varies independently. Lower native build times in B also cannot be attributed directly to a backend compiler change. Do not promote LTO from these results. Restored baseline after verified game exit, then restored original shipped GNU DXVK265888C31CA78DFFA290C39CB7E50BFB02762590E41927906E46FB32F01497FA for a final source-build cross-check. Compiler builds change code generation; the symbol build is an analysis tool, not a proven optimization. Captures remain ignored under build/profiles/dxvk-msvc-no-lto-a.json, dxvk-msvc-lto-b.json and dxvk-msvc-no-lto-c.json.

DXVK source already skips unchanged stream buffers, offsets/strides and index bindings; blindly caching calls outside it is not supported by these samples. The atomic helper's butterfly has incomplete caller attribution, so its754 exclusive hits do not identify a single safely removable reference or lock. Preserve resource ownership and multithread protection. Next larger experiment should measure actual draw/binding transitions and native command batching, with a controlled reversible change; no90Hz acceptance.

## Default-off deferred index-binding prototype

2026-10-05: public-source patch patches/dxvk/013-defer-index-bindings.patch adds d3d9.experimentalDeferIndexBindings=false to pinned DXVK3.1.1. A changed non-null SetIndices retains API-state ownership/locking and sets the existing dirty flag; PrepareDraw consumes it for indexed draws. Reviewed dynamic system-memory UploadPerDrawData, UP draw binding/reset, EndScene null cleanup and state-block Apply forwarding. Intended saving: changed bindings overwritten before a draw, not unchanged bindings (already skipped upstream). No resource-reference suppression, multithread-lock removal or ASI rendering change. Patch reverse-apply check passed after correcting PowerShell's initial CRLF serialization; .gitattributes preserves LF for patch portability.

x86 MSVC Release DLL FD0668B57D17E3DDFCB3191832FC05AA82AFE850B4311C0A7EFABD96703EFB9D / PE014C / matching36,417,536-byte PDB archived in ignored build/issue013-dxvk-deferred-index. Debug compilation pending. Prototype is not deployed, not accepted and has no measured saving. Original GNU265888C3 backend remains installed in PID2508. Steam launch succeeded after two direct startup attempts exited; no exception trace captured, no fault cause inferred. Native held street restoration passed, continuous ready stereo and zero overflows. OpenXR remains VISIBLE rather than FOCUSED, so the matched capture is gated pending the user's headset wake/dashboard closure. No screen capture or heavy analysis during measurement windows; user explicitly noted Computer Use overhead.

Both x86 MSVC Debug and Release builds now pass (235-target Debug build, existing upstream warnings). Debug PE014C verified; manifest records source commit, patch/DLL/PDB hashes and pending runtime/performance status. git diff --check passes apart from line-ending notices. No game/backend/config deployment; wait for focused headset before comparing the original GNU control, then test the patched backend off/on/off.

## Post-restart deferred-index comparison (not promoted)

2026-10-05: user reports the repeated preceding failures came from a Windows issue and restarted the machine. Those interrupted runs are excluded from performance acceptance; they do not prove a fault in the index patch or a repaired OpenXR lifecycle bug. The same FD0668B5 prototype ran continuously both enabled and disabled after the restart. Debug and Release are x86; no ASI source/build change in this comparison.

CE1.2.0.59, SteamVR2.17.10, lighthouse90Hz, RTX4070Ti/NVIDIA610.88, ASI628B00EC. Per-eye1920x1080, not the earlier2560x1440 captures. Native held street coordinates and heading restored/read back after each launch. Fresh images outside the capture windows show comparable upward/resting-headset views with body clipping; this is not comprehensive headset visual acceptance. No computer use, compilation or large analysis during these CPU-only30-second measurements. Detailed observers and scalar classification off; FOCUSED endpoints, zero dropped metrics and zero command-buffer overflows in all four captures. Position restoration does not reproduce traffic, weather, time or headset orientation.

| Capture | Eye0 commands mean ms | Eye1 commands | Render before XR wait | XR frame wall | Submission interval | Submission p95 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| reboot-gnu-street-a | 7.444 | 7.1005 | 14.629 | 1.659 | 19.5721 | 21.1676 |
| reboot-index-off-a | 7.9118 | 7.5265 | 15.5256 | 1.7079 | 20.797 | 23.0435 |
| reboot-index-on-b | 8.1283 | 7.7252 | 15.9413 | 1.7119 | 21.2833 | 23.5731 |
| reboot-index-off-c | 7.9897 | 7.5785 | 15.6543 | 1.6822 | 20.868 | 22.9655 |

Off A / on B / off C use the identical FD0668B5 DLL, with the option confirmed in fresh backend logs. Enabled B does not improve either eye command time or throughput. Reject promotion; keep the patch only as a reproducible negative experiment. Original GNU265888C3 was restored after verified process exit; the exact temporary one-line dxvk.conf was removed (absent before testing), ASI/INI unchanged. Earlier source-build compiler differences are not an optimization gain. The GNU control averaged19.5721ms (~51.09 submissions/sec), still far from11.11ms/90Hz. Two early launch exits during the comparison recovered on retry; no startup root cause established.


## Device-lock ordering candidate

2026-10-05: inspected pinned public DXVK ScopedDeviceLock. Enabled protection obtains ownership via acquire compare-exchange and releases via default sequentially consistent store; the sibling Spinlock already uses release unlock. Prepared independent default-off d3d9.experimentalReleaseDeviceLock patch, preserving contested waiting, recursion, COM references and Vulkan queue synchronization. Only the D3D9 constructor opts into the alternate store; all other scoped-lock constructors retain defaults. This is a hypothesis about repeated API overhead, not measured contention or safe removal of a lock.

Actual-header four-thread test caught the pinned guard move constructor reading its own uninitialized m_lock rather than other.m_lock. Original test reached concurrent publication completion, then explicit move faulted withC0000005. Separate013-device-guard-move.patch fixes ownership transfer; both default/enabled lock modes now pass400,000 protected updates each, nested guard ownership, move release and disabled paths. This is not evidence for the earlier game crash causes. Controls and candidate must include this identical correction. Initial test harness compilation needed NOMINMAX; isolated sandbox test stalled, outside-sandbox test supplied the native failure evidence. x86 /O2 probe of the actual header confirms release unlock emits mov, default unlock emits memory xchg. Remaining acquire compare-exchange is unchanged.

Debug rebuild with both patches passes; Release rebuild underway. No deployment or frame-time gain yet. No change to ASI or the original working GNU backend currently running. Patch/test reproduction in patches/dxvk/README.md; source/binaries/assembly/logs remain ignored.

Release509BC6FC/Debug6870371B now built, PE014C verified. Exact DLL/PDB/patch hashes archived in ignored build/issue013-release-device-lock; actual-header test passed outside restricted sandbox. Test move failure left a CSRSS-owned error dialog on desktop; computer-use app approval timed out trying to dismiss it. Old failed test processes were closed. Game remained FOCUSED/ready/continuous; no further UI activity in timing windows. Keep this environmental limitation in the comparison; it is not a GTA IV assertion. Default-off candidate deployed only after bridge quit and verified exit, original GNU backend preserved. First startup restore attempted too early during loading and timed out; later callback completed and the explicit restore/readback passed. No startup hang inferred.

## Device-lock off/on/off result (not promoted)

Same509BC6FC Release DLL in all three runs, including identical guard-move correction; default-off control versus release store enabled. CE1.2.0.59/SteamVR2.17.10/90Hz, ASI628B00EC unchanged,1920x1080 per eye. Native coordinates/heading restored/read back,10-second settling after fresh captures,30-second CPU-only windows without computer use/build/analysis. FOCUSED endpoints, no metric drops or command overflows. The enabled launch needed stale PlayGTAIV/Launcher cleanup after no game process appeared, then succeeded on retry. Repeat control launched successfully. The old test error dialog remained on desktop in all three fresh images; low/resting headset/body clipping and native idle pose differ slightly across launches. Traffic/time/weather/headset pose are not restored; no broad headset compatibility acceptance.

| Capture | Eye0 commands mean ms | Eye1 commands | Render before XR wait | XR frame wall | Submission interval | Submission p95 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| lock-release-off-a | 8.1681 | 7.7385 | 15.9942 | 2.5375 | 22.5038 | 25.1948 |
| lock-release-on-b | 7.9775 | 7.4669 | 15.5316 | 2.4656 | 22.3369 | 24.2479 |
| lock-release-off-c | 7.9235 | 7.5002 | 15.5118 | 2.5011 | 21.5588 | 23.6374 |

Enabled eye-command sum15.4444ms matches repeat-control15.4237ms, rather than establishing the apparent0.4622ms saving versus first-control15.9066ms. Submission is slower than repeat control. Do not promote the release-order option. Most active thread remains95.66/95.93/96.00% of one core across runs; no contention or safely removable locking established. Original GNU265888C3 restored after verified exit; exact test-owned dxvk.conf removed, ASI/INI unchanged. The guard-move correction remains a separately reproduced source defect, with no demonstrated game-crash link. Both public patches and actual-header test retained for reproducibility, default off. #013 remains open; request representative head-height street view before larger rendering experiments.


Final rollback verification: original GNU265888C3 backend, no dxvk.conf, unchanged ASI/INI. PID10276 reached2,342 continuous stereo layers after loading (versus2 at the initial status), FOCUSED/ready/zero overflows. Profiler inactive and all detailed observer selections off. Fresh street mirror shows continued rendering with the same low-headset/body-clipped view and old CSRSS test-error dialog. Display refresh90Hz remains distinct from live status submission48.2Hz; this endpoint is not a30-second benchmark. Waiting for a representative head-height headset view; no extra performance claim or #013 closure.


## Head-height view, control/draw observer/control

2026-10-05 user raised headset to head height; fresh mirror shows forward/downward outdoor park/court geometry, still some headless body clipping. Original GNU265888C3/ASI628B00EC, CE1.2.0.59/SteamVR2.17.10/lighthouse90Hz, RTX4070Ti/NVIDIA610.88,1920x1080 per eye. Same-session PID10276, native module base007E0000, position snapshot ignored head-height-street-location.json; no relaunch or backend/config change. First headheight-control-a capture ended VISIBLE, excluded from comparisons; draw capture was correctly gated before starting. After user restored focus, CPU-only control / draw observer / CPU-only control all validate FOCUSED endpoints, no metric drops/overflows. No computer use/build/analysis during30-second windows. Native CPU deltas: busiest thread28776~95.89/95.97/95.93% core; no new stack trace establishes its exact function distribution in this view.

| Capture | Eye0 commands mean ms | Eye1 commands | Render before XR wait | XR frame wall | Submission interval | Submission p95 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| headheight-focused-control-a | 6.4066 | 5.9691 | 12.4576 | 1.7798 | 17.5314 | 19.2612 |
| headheight-focused-draws-b | 6.4693 | 6.0579 | 12.61 | 1.7973 | 17.6726 | 19.523 |
| headheight-focused-control-c | 6.4272 | 5.9691 | 12.4798 | 1.7932 | 17.5648 | 19.7084 |

Control throughput~57.04/56.93 submissions/sec. Draw observer adds~0.11-0.14ms submission interval here; do not count its results as ordinary performance. G-buffer mean4.6994+4.3339ms in A and4.7180+4.3506ms in C (~9.05ms total), about73% of combined eye command spans. Observer:5358.99/5430.78 indexed G-buffer draws per eye, ~1.567M/1.563M submitted primitives; DrawScene1522.64/1564.82 draws, lighting77.22/69.21. Counts include actual submitted geometry across those stages, not unique visible objects or proof of redundant work. Sparse original draw API spans~0.222-0.302microseconds include timing overhead and exclude native command preparation/VR writes/deferred backend work. No failed draw calls. Larger target is native G-buffer draw/state command generation and execution; no batch suppression or visibility reduction implemented.

Live rechecked sampled native RVA462F8: entry004462E0 -> runtime008262E0, conditional nine-field0x24-byte record append/count increment, returns00446360; no established G-buffer role/caller/type. Static import remains packed/bad-data. Previewed/applied/read back Issue013NineFieldAppend bookmark and saved GTAIV.exe project. No destructive reanalysis or confident symbol invented. Read-only listings remain ignored.


## Fresh native CPU trace for the head-height view

Received user-elevated headheight-gbuffer-20261005-094059.etl (1,167,065,088 bytes).
Metadata: CE1.2.0.59/PID10276/base007E0000, ASI628B00EC, original GNU DXVK265888C3,
FOCUSED endpoints, profiler inactive/all detailed observers off, zero command
buffer overflows. Analyze first30seconds/renderTID28776 identified in the preceding
same-process controls. Raw decoder:28,687 sampled events/5,319IPs/no lost events;
stack report:28,538 stack-backed hits. These are separate denominators.

ETL image rundown establishes GTAIV007E0000..023C7000, ASI66B20000..674F0000,
vulkan.dll677C0000..67F40000. Exact raw instruction attribution:

| Module | Raw samples | Share of render thread |
| --- | ---: | ---: |
| GTAIV.exe | 13,184 | 45.9581% |
| DXVK vulkan.dll | 10,498 | 36.5950% |
| Kernel ntoskrnl.exe | 1,629 | 5.6785% |
| FusionFix ASI | 1,192 | 4.1552% |
| Unmapped executable addresses | 507 | 1.7674% |

Remaining samples are other runtime/driver modules. Stack decoder's38.41%
unknown-module bucket must not be called entirely DXVK. New standard-library
Python report mapper verifies all28,687 samples sum, maps through the exact ETL
rundown, retains unknowns, and refuses corrupted totals or a missing target PID.
Both refusal paths verified against local altered report fixtures; actual
attribution matches the independent address-range totals. Reproduction documented
in frame-profiling.md. Reports/fixtures remain ignored.

Native nine-field append004462E0..00446361 (runtime008262E0..00826361) aggregates
1,196 raw samples/4.1691%; native matrix multiply004307F0..004308E9 aggregates
824/2.8724%. The single append interior RVA462F8 has768 stack hits/2.69%, which is
not its whole-function cost. Shared count/output-pointer writes in the append
are established; semantic record type/callers/phase and independent-job ownership
are not. Existing Ghidra bookmark previewed, applied, read back and saved with the
fresh evidence and explicit uncertainty. No game function rewritten, renamed
confidently or destructively reanalyzed.

Bounded read-only GNU backend listings show hot IP677D4E7E directly after locked
resource-reference increment and677E023B after locked decrement. Consistent with
binding/reference transitions in public source; exact stripped function names are
not independently established for these PCs. No proof of contention, unnecessary
references, or safe lock/reference removal. Resource lifetime protection remains
intact. Reports headheight-dxvk-hot-14e.txt and headheight-dxvk-hot-202.txt are
local inspection evidence, not disassembly payloads for distribution.

The trace's rolling submittedHz endpoints43.86/47.4 differ from the earlier
57Hz control captures; recording overhead and scene/input/time variation cannot
be isolated from those endpoints. Later ordinary30-second capture with profiler
inactive, no computer use/build/analysis, FOCUSED endpoints:1,597 submissions,
estimated53.125Hz (RPC bounds53.027..53.224). This is another time window, not a
new optimization or matched speed change. Do not convert whole-thread sample
shares into exact G-buffer milliseconds; the trace is not phase-filtered.

Conclusion for the next implementation: target aggregate per-eye draw/state
command work, with a bounded observer of actual binding changes between draws
before attempting batching. Counts alone do not prove redundant calls, and
unverified native shared buffers must not be parallelized. The append/matrix
routines individually cannot explain the full remaining frame-budget gap.
Keep accepted visibility and simulation ownership while measuring any candidate.
Game/backend/config remained unchanged; #013 stays open with no90Hz claim.

## Native scene-trigger investigation (2026-10-05)

The user reports steady90Hz in most places, with hitches concentrated in certain views/locations. The earlier~42% native module share identifies where CPU samples land; it does not explain those triggers or prove every outdoor scene is slow. Resolution-insensitive CPU cost is consistent with per-object/command/state work, but does not prove that work is redundant. A genuinely fast outdoor view has been requested for a matched comparison; no new timing or speedup is claimed here.

Continued CE1.2.0.59 live-code inspection in PID10276/base007E0000, existing Release628B00EC ASI/original GNU265888C3 backend:

- Resolved the previously tentative004462E0 nine-field writer as **immediate vertex emission**, using begin00446280 ->00423700 and end00437950 ->004237A0. The begin helper sets stream0, locks a dynamic vertex buffer with NOOVERWRITE0x1000, switches to DISCARD0x2000 when its1.5MiB ring rolls over, and reserves count*stride bytes. End unlocks the vertex buffer and calls device DrawPrimitive at virtual+144. One verified command callback008DD680 begins type5/count4 and emits four36-byte vertices. Exact position/normal/color/UV field names still need declaration verification.225 direct-call candidates are now discoverable with `inspect-game-code --calls`; candidates must be checked for executable instruction boundaries. This establishes concrete geometry construction work, not a safe batching change or its dominant producer.
- Resolved xperf HTML symbol links that were lost by the earlier text-only parser.759 of768 interior append stack hits link back to command dispatcher return008DE0DC. Its3432 inclusive hits include descendants; only3 are exclusive at that return. Stack unwinding can omit intermediate callers, so this does not identify which command generates the expensive vertex stream.
- Rechecked viewport00431940 ->00430F00: three dependent matrix multiplies, then shader publication if current viewport. Its linked descendants include native constant caches0042C480/0042C6A0 and memcpy00DF9670. Pixel-float cache0042C480 compares count*16 bytes, returns on equality, otherwise copies and calls SetPixelShaderConstantF at+1B4. Vertex-float cache0042C6A0 calls+178. Vtable mapping checked against installed Windows SDK d3d9.h. Therefore matrix branch attribution includes constant comparison/copy/upload; changing the matrix arithmetic alone cannot recover that whole percentage.
- Added read-only `tools/read-native-render-commands.ps1` to group a copied **consumed command prefix** by execution callback and model ID. It validates CE/signatures, capacity, header sizes, buffer pointer, slot and cursor around the copy; rejects a switched/reused prefix. Reader advancement in the same slot is allowed. No process suspension, field writes, debugger, injection or raw buffer export. This is not a complete-frame/per-eye counter: ABA reuse remains possible, commands can mutate payloads while executing, and counts must not be interpreted as duplicate objects or a timing result. Keep it outside performance captures.
- Actual diagnostic prefix580304bytes/5541commands contains3850 model callbacks008DCBF0. A later smaller prefix contains3333 such callbacks and449 callbacks008DD0B0. Dominant model IDs include19492 (334 callbacks),3722 (227),788 (154),761 (115); instance/phase/scene-feature attribution remains pending. Live008DCBF0 resolves the model ushort at command+30 through analysis01295CD8, normalizes a quaternion, builds a transform and dispatches drawable+20. Thus native per-instance rendering is concretely identified; repeated model IDs alone are normal for many instances and passes.

Ghidra bookmarks Issue013ImmediateVertices004462E0, Issue013ConstantCache0042C480 and Issue013ModelCommand008DCBF0 were previewed, applied, read back and saved. The packed static import was left intact. Old tentative notes are historical; this section resolves the writer's role without claiming its dominant scene trigger.

## Fast native trace and corona producer,2026-10-05

Analyzed native-fast-headheight-20261005-105321 against headheight-gbuffer-20261005-094059: same CE1.2.0.59/PID10276/renderTID28776/base007E0000, ASI628B00EC and GNU DXVK265888C3. Both first30-second raw windows have zero lost events. Fast has22865 samples, GTAIV6938/30.34%, DXVK7992/34.95%; slow28687, GTAIV13184/45.96%, DXVK10498/36.60%. Native immediate writer004462E0..00446361 has48fast/1196slow; pure matrix multiply004307F0..004308E9 has489/824. Raw instruction samples are not milliseconds or percentages of complete frame time.

Found a metadata alignment error: the old before/after frame counters bracket module hashing/enumeration and WPR start/stop processing, extending beyond the requested30seconds. For example the fast trace's5789-frame delta cannot be divided by30. Endpoint rates/separate matched phase captures remain contextual evidence, not an aligned per-frame denominator. Updated capture-native-cpu.ps1 with recording-start/end status-query QPC/UTC bounds, WPR boundaries, clock frequency and an explicit warning. PowerShell parser validation passed; its revised elevated recording path still needs a live run. No historical frame-rate claim is derived from that delta here.

Added opt-in five-second vertex-caller-probe, installed/removed on queued render markers. Fixed512-row histogram, foreign threads ignored, counts by six eye/stage streams plus outside; no hook remains afterward. x86 Debug and Release built successfully. Deployed ReleaseDC3D54E0 with installed ASI/INI preserved under ignored build/issue013-vertex-probe-backup; GNU backend/INI unchanged. Relaunch PID2916/renderTID19220/base007E0000, SteamVR2.17.10/90Hz,1920x1080, focused continuous Vulkan stereo. Restored both saved on-foot locations, completed readbacks, settled15seconds, inspected fresh native window images outside probes.

The dominant immediate producer is **corona billboard rendering**, native00C1F170..00C1F993, not water. Verified entry/body/loop and six direct calls to004462E0, plus FusionFix source/coronas.ixx's matching relocated arrays,49152 limit and3*2^14 phase indexing.00C1EE40 filters source64-byte records with native sphere query00431E40 and builds compact32-byte phase records.00C1F170 expands each accepted quad into six36-byte vertices, up to0xAAA quads per native batch. The radius/color/basis reads and two-triangle emission agree with corona geometry. Do not infer this role for unrelated immediate callers.

Completed caller probes, no dropped entries:

| Quantity | Fast street | Slow courtyard |
|---|---:|---:|
| Eye/stage stream starts, each |450|231|
| Calls at each of six corona sites, per eye DrawScene |273144|1471238|
| Corona quads per eye DrawScene stream |607.0|6369.0|

Same emitter also runs outside marked eye streams. Counts are entry attempts and diagnostic overhead prevents timing claims from these probes. Time/weather/traffic were not restored; both fresh probe images are night scenes, while older phase/ETL windows may differ. An initial slow probe's host log reader tried to materialize the304MB cumulative log, exhausted memory and failed to save its result: excluded that run, repeated with a bounded tail. All subsequent log reads are bounded. No game crash or render overflow was observed in these probes.

Older matched ETL raw samples in corona draw00C1F170..00C1F993 are10fast/621slow, excluding child calls. Prep00C1EE40..00C1F020 has0samples in these render-thread windows; it may run elsewhere and absence does not prove zero cost. Writer's1196samples cannot all be assigned to coronas without aligned caller samples. Native shader/material binding and dependent-matrix publication remain the larger G-buffer investigation; do not equate its inclusive subtree with pure math or corona emission.

Persisted Issue013FastSlowCPU004462E0 and Issue013CoronaEmission00C1F170 via preview/apply/readback/save in GTAIV.exe; static packed code left intact.

### Exact-byte vertex copy experiment: no gain

Added development-only vertex-emission-test on/off. Unique native signature plus enabled/output/count layout validation; queued render-thread hook change, off by default and on shutdown/reset. Nine cdecl words copied as36raw bytes; disabled/null-output branches still increment the count, no vertex/draw removal. x86 tests exercise NaN/infinity/signed-zero/color bits, consecutive unaligned36-byte records, guards and count-only branches; passed. Debug and Release builds passed, with existing deprecation/missing upstream PDB warnings.

ReleaseASI5E56347C22C6B63C4D0B40E6937D1BEC346C6430458410D0CB9528BF28698DB5, PID24384, same backend/INI. Interim installed ASI/INI backed up under ignored build/issue013-vertex-emission-backup. Restored courtyard/readback, settled and fresh image before baseline. Confirmed active detour via live004462E0 and local on log; fresh scene remained live. No Computer Use, build or ETL work inside timing windows. Off/on/off CPU-only30-second captures all validComparison=true, FOCUSED endpoints, zero drops/overflows:

| Mean CPU wall time,ms | Off A | On | Off B |
|---|---:|---:|---:|
| Submission interval |21.1569|21.4293|21.3571|
| G-buffer, eye pair |9.4683|9.5264|9.7085|
| DrawScene, eye pair |4.7860|5.0653|4.8748|
| Render before XR wait |15.1489|15.4927|15.4894|

Scene animation/body visibility and traffic varied; this is no demonstrated gain, not a precise regression estimate. Restored original emitter and verified native entry bytes. Experiment stays off. Camera/culling/shadows/quality were not changed. #013 remains open; this does not meet the90Hz target in the dense courtyard.

Inspection utility Release build and live caller discovery passed. Native snapshot first rejected two changing copies; revised same-buffer reader-advance condition then decoded two bounded prefixes with valid command lengths. PowerShell syntax and Git diff checked. No ASI/backend/config deployment or gameplay change in this investigation. Raw snapshots, disassemblies and linked-stack analysis remain ignored under build/profiles/native/. Next acceptance evidence is fast/slow **outdoor** frame-cost and model/constant counts, then a measured change aimed at the excess work. #013 remains open.

## Fast/slow outdoor differential (2026-10-05)

Same process10276/CE1.2.0.59/base007E0000, unchanged628B00EC ASI and265888C3 GNU DXVK,1920x1080 eyes,SteamVR90Hz. Stored user-selected fast position in ignored native-fast-outdoor-location.json; restored slow head-height-street-location.json and returned to fast with successful native readbacks. Allowed15s streaming settle and inspected fresh game images before comparative windows. Computer Use, native readers, builds and analysis stayed outside timing captures. Initial in-car and low/blocked on-foot captures are **not** the head-height comparison; user raised headset and fresh images verified clear street/courtyard views.

Thirty-second draw-observer captures, fast/slow/fast:

| Metric | Fast A | Slow courtyard | Fast repeat |
| --- | ---: | ---: | ---: |
| Mean submission ms |11.1147|20.9652|11.2313|
| Submission p95 ms |11.7631|22.9260|12.0471|
| Combined eye command ms |4.3641|15.2218|5.0398|
| Combined G-buffer command ms |2.2866|9.6749|2.6796|
| Combined DrawScene command ms |1.7150|4.7424|1.8631|
| G-buffer calls/eye0 stream |1065.9|5524.1|1431.2|
| G-buffer calls/eye1 stream |1021.7|5560.8|1360.9|
| DrawScene calls/eye0 stream |373.6|1667.5|426.8|
| DrawScene calls/eye1 stream |339.3|1686.4|392.9|

All captures valid by client gate, FOCUSED endpoints, zero failed draws/dropped samples/command overflows. Scene changed naturally: time of day, traffic, exact headset orientation and weather were not frozen; fast repeat counts drift upward~30-40%. Nonetheless restoring fast recovers~90Hz and~5ms eye command work, while slow generates~4-5times more G-buffer submissions and~15ms eye work. This establishes scene-dependent submission/preparation load, not a global60Hz cap or optimization gain. These include draw-observer overhead; do not compare their intervals directly to the constant-observer captures.

Separate constant observers: vertex calls per eye stream~3388/3373 fast versus10756/10697 slow; pixel~4237/4210 versus13989/13948. Vertex vectors~20857 versus100773, pixel~18376 versus94422. Timed constant forwarding spans sum~0.58ms fast/~1.93ms slow, excluding upstream native comparison/matrix work and including instrumentation. Consecutive repeatedCandidates=0 in both scenes: this observer provides no justification to drop repeated native constant writes blindly. Slow constant-observer capture24.2255ms versus20.9652ms draw-observer capture is not a proven scene regression.

The render thread still approaches one core even in the fast view: fast A94.2% (21.58s user/7.67s kernel), repeat92.9% (23.91/4.95), slow96.4% (29.23/.77). Therefore total thread CPU alone does not distinguish useful scene work from pacing/runtime/kernel activity. Fresh fast native ETL is needed to compare exclusive native work **per frame**, not just percentages of CPU samples.

Native model attribution: resource-pointer table analysis01295CD8, live model data+3C hash independently matched to JOAAT of installed IDE names. Repeated sampled IDs resolve to CIPProj1Blok_01 (19492), LondonP_MD_ingame_2 (3722), CJ_PROC_PLANT_28 (761), CJ_PROC_Stone2 (788), BM_NYlamp1a (1910). Other hot IDs include CJ_BARRIER_10 (1838), CJ_PROC_PLANT_5 (766), C_Fern_MD_INGAME_2 (3698), CJ_FENCE_4_2 (1752). No asset bytes copied into tracked source.

Read-only longest validated diagnostic prefix848240bytes/9383commands:

| Model | Native callbacks | Distinct translation bit patterns |
| --- | ---: | ---: |
| CJ_BARRIER_10 |531|133|
| CJ_PROC_PLANT_28 |481|161|
| CJ_PROC_Stone2 |343|182|
| CJ_PROC_PLANT_5 |303|98|
| C_Fern_MD_INGAME_2 |286|78|
| LondonP_MD_ingame_2 |271|43|
| CIPProj1Blok_01 |270|40|

The prefix contains multiple passes/eyes; translations are a diagnostic approximation to instances, not entity identity, and rotations/mesh/subset/flags can differ. Byte+35 variants include0 and3 for plants/trees, but their meaning remains unverified. Counts show many actual placements, not evidence that hundreds of identical commands can simply be deleted. Candidate optimization is compatible repeated-object batching/instancing or excess per-instance state preparation, with shader/mesh/pass compatibility verified first. No vegetation/LOD quality reduction performed.

Native reader now issues its before/buffer/after RPM calls adjacently in a managed helper, avoiding PowerShell dispatch gaps; validates all existing bounds/slot/cursor checks. Compile error from referencing an in-memory Add-Type assembly was corrected by a local read-only P/Invoke declaration. Final helper compiled and eight live bounded snapshots decoded with zero rejected copies; retained only longest diagnostic prefix. Runtime snapshots remain non-atomic/ABA-limited and are excluded from frame timing windows. Script syntax passes. Ghidra Issue013SceneWorkload bookmark008DCBF0 previewed/applied/read back/saved. Game returned to saved fast position; no ASI/backend/INI deployment and no performance improvement claimed. #013 remains open.

- 2026-10-05 submission-counter audit after native-slow-corona-path trace: `openxrvr.ixx` increments `stereoLayers` once after a successful `xrEndFrame` containing one projection layer with two eye views and completed eye transfers; this is an eye-pair count, not a per-eye count. `submittedHz` is a rolling approximately five-second counter rate, while `VRProfile::Submission` measures QPC intervals at the same success point. Revised WPR metadata records 30758 -> 32034 successful layers (1276 pairs), with RPC-bounded elapsed interval 30.0025827–30.106136 seconds, giving 42.3834–42.5297 submissions/sec for this particular recording. Built-frame delta is also 1276; runtime endpoints FOCUSED, no command-buffer overflows. Earlier ~47/~57 values belong to other captures, not this exact trace. This does not count compositor-generated/reprojected display updates or prove distinct pixels each submission. User reports perceived headset smoothness above 60 FPS; retain that observation and distinguish display/head-motion updates from newly rendered scene submissions. Native ETL attribution remains pending; no optimization or closure inferred from the graph.

## Courtyard trace confirmation and packaged grass-fix experiment, 2026-10-05

The new native-slow-corona-path-20261005-113448 ETL belongs to PID24384/render TID14092, verified against the earlier queued phase markers, capture thread CPU and ETL thread rundown. The currently running game's thread must not be substituted after a relaunch. An initial attempt with later TID9188 returned zero samples and is excluded. First30-second raw window:28309 events,5297 IPs,zero lost events. GTAIV45.0528%, GNU DXVK36.5997%, ASI4.3237%; native corona drawer C1F170..C1F993595 exclusive events/2.1018%, immediate vertex writer4462E0..4463611133/4.0023%, matrix4307F0..4308E9 673/2.3773%. These are instruction-event fractions, not milliseconds/frame; the first30-second ETL window is not exactly the RPC-bounded recordingStart/end window. Matching-build stack report28047 hits uses a separate denominator and stripped DXVK symbol rows are not automatically game code. Previewed/applied/read back/saved Issue013CoronaTraceConfirmation at C1F170 in Ghidra.

User reports minimum graphics settings barely alter timing (~0.5ms GPU), while view/detail distance below4 and turning toward ocean recover budget. This is evidence to investigate visibility-dependent command work; it does not establish a single vegetation bug, TV60Hz cap or base-game-only cause. Fresh five-second submission audit earlier recorded299 pairs/~59.23Hz in a different window. User graph application intervals vary toward11ms with orientation; no fixed60 cap established.

User requested independent installation of GTA IV Grass & Procedural Props Fix v1.2 and supplied the ZIP in workspace root. Public source commit bbbb93eeaa7364e27d8f2dc8e451c94f223a3bfc inspected and built in ignored build storage with documented x86 TinyCC0.9.27 command/hardening helper; output577536 bytes SHA2566C032A33C3922EEBAD93E9473AA7143DFEF480F16E70A727A7BEB5DB21529BDF exactly matches supplied packaged ASI. No mod source integrated into FusionFix. Installed only package ASI+INI after bridge quit/verified exit of PID2876; FusionFix ASI5E56347C unchanged. Package defaults preserved: auto capacities,distance1,plant/object density1,class mask3,DebugLog1,HangWatchdog1,45sec,HangMessageBox0,FullHangDump0. README recommends Detail Distance100, but game settings intentionally left at user's current values for causal comparison; not a test of that changed-settings preset.

Baseline player root captured in ignored grass-fix-comparison-location.json:1333.773804,-533.229858,14.839653,heading334.461823. Launch args preserved from PID2876: -hz60 -width1920 -height1080 -nomrestrict -norestrictions -noprecache -novblank -percentvidmem100 -availablevidmem8192. No commandline.txt/args.txt exists; presence of args does not prove their effect or earlier-run identity. Pre-install CPU-only profile grass-fix-package-off-a completed with computer use/development analysis idle. New PID30092 remains FOCUSED continuous Vulkan stereo; mod log PATCH_APPLIED, recognized CE executable SHA25608759A5516F9837920EA504436236BBAB89D0826A8E4D04FF106345177B5345D, no startup guarded-interval mismatch. Native location restore completed with exact returned coordinates. Mod-on comparison pending; no speedup claimed. Preserve ZIP/raw logs/builds outside Git.

### Packaged grass-fix comparison outcome

Thirty-second CPU-only captures: off-a (PID2876), on-a (PID30092), off-b (PID19960), same FusionFix5E56347C and original GNU DXVK265888C3;90Hz SteamVR, supplied separate mod ASI6C032A33 when enabled. No computer-use/compilation/ETL analysis during measured windows. Each client gate valid, endpoint FOCUSED, zero command-buffer overflows/metric drops. Mean ms:

| Metric | Off A | Packaged mod on | Off B |
| --- | ---: | ---: | ---: |
| Submission interval |17.8683|20.6321|21.0497|
| Eye0 commands |6.1952|7.9007|7.7017|
| Eye1 commands |5.9274|7.4997|7.2076|

No demonstrated performance improvement or90Hz recovery. OffA fresh image was daylight/dusk, while post-relaunch onA/offB were night; headset/camera alignment and idle-body obstruction also differ, so offA-versus-onA is not a causal regression. OnA roughly matches repeated night offB; small interval difference is not a reliable benefit. Location restore/readback passed; offB settled recapture drifted~0.039 game units (within existing0.1 gate). Traffic/time/weather not restored. Visuals were live with procedural props on; headset visual acceptance remains user-side, no broad compatibility claim. Detail/view/other game options were left as supplied by user rather than setting mod README's recommended DetailDistance100. The mod changes pool capacity/density semantics, so neither binary compatibility nor approximate timing establishes identical vegetation content. Normal bridge quit of offB took longer than3seconds; no deployment occurred until later verified GTAIV exit. Restoring packaged mod after control for user testing; no FusionFix integration, no backend changes. To disable later: clean quit, move only installed ProceduralFixes.asi to ignored backup storage; retain matchingINI and package. User ZIP and extracted package/diagnostics stay out of Git.

Final state: packaged grass mod restored/enabled; PID23736 FOCUSED continuous Vulkan stereo, mod log PATCH_APPLIED, courtyard restore completed and fresh live scene inspected. Game and mod files remain outside Git. No second ASI copy installed.

## 2026-10-05: native function flame graphs and complete model callbacks

User requests retaining packaged Grass & Procedural Props Fix v1.2; it remains
installed independently with unchanged defaults. No source integration. Native
profiling should proceed from concrete hot functions, not broad module totals.

Added offline sampled-stack exporter and zoomable flame graph generator.
Two-pass ETW clock selection verifies the identical per-thread sampled-IP
sequence and matches the earlier first30s raw totals exactly:

- Slow `native-slow-corona-path-20261005-113448.etl`, PID24384/TID14092:
  28309 Profile events,28271 matched stacks,38 missing,lost0.
- Fast `native-fast-headheight-20261005-105321.etl`, PID10276/TID28776:
  22865 Profile events,22754 matched stacks,111 missing,lost0.

Local artifacts `build/profiles/native/slow-cpu-flame.html` and
`fast-cpu-flame.html` show exact unmapped/native addresses and only verified
function-range labels. Click to zoom; search/highlight; exclusive/inclusive
ranking tables. JavaScript syntax validated with Node. Missing or partial
x86/WOW64 unwinds leave fragmented roots: a synthetic PDB with function names
would improve labels but cannot automatically recover the original FPO/unwind
information or source-level types. No nearest-symbol guesses were used.

| Verified analysis range | Slow inclusive/exclusive samples | Fast inclusive/exclusive samples |
| --- | ---: | ---: |
|00426180..00426259 indexed draw/buffer bindings|6227/449|4236/247|
|0042C480..0042C531 pixel float constant cache/upload|1182/400|716/287|
|0042C6A0..0042C751 vertex float constant cache/upload|1348/400|778/225|
|00444C50..00444F3D geometry/material loop|738/737|400/400|
|004462E0..00446360 immediate vertex append|1134/1133|48/48|
|004307F0..004308E8 matrix multiply|673/673|489/489|
|00C1F170..00C1F992 corona billboard draw|595/595|10/10|

Inclusive and exclusive counts have different meanings. Inclusive includes
children/DXVK and is not additive across functions; counts are not measured
milliseconds/frame or achievable savings. Different captures/time/scene state
remain a limitation. The immediate writer and corona difference is concrete,
but the earlier writer replacement failed to improve time. Indexed draw's
largest child branch is DXVK RVA348DF (2574 slow/1551 fast), while index binding
RVA20253 is368/193. Native dispatch return008DE0DC is3102/736 root samples;
004319A4 viewport-publication return1446/536. These are caller anchors,
not proof that the return instruction itself consumes that inclusive cost.

Read-only live disassembly PID30452/base007E0000 confirms indexed helper's
SetIndices slot+1A0,SetStreamSource+190,DrawIndexedPrimitive+148. Return00426240
is from DrawIndexedPrimitive, not the following function's query-poll loop.
The geometry loop00444C50 iterates ushort geometry/material entries and calls
0042EBE0 after selection. Shader float caches compare/copy changed constants
then upload; packed static Ghidra import is not valid live code at these ranges.

GTAIV.exe has an RSDS CodeView entry (GUIDcc7e4bba-a264-4d80-96b6-91740c9efabd,
age1), so a synthetic symbols PDB is potentially compatible with `wandel/pdbgen`.
Its documented export covers global functions/types, not recovered original
source symbols; no PDB generated yet. This is optional labeling work and does
not block locating or disassembling hot addresses.
Sources: https://github.com/wandel/pdbgen and Microsoft WPA flame graph docs
https://learn.microsoft.com/en-us/windows-hardware/test/wpt/graphs .

Default-off five-second model callback observer008DCBF0 added and x86 Debug/
Release built (current Release SHA256
5DC6AE19CC8E9FE2BF7173451DEE815C175C54889F8E0462940C0AD42CDDB3CA).
Initial512-row courtyard capture dropped305752 entries and was excluded.
Expanded fixed4096 rows: same PID30452/backend/mod, courtyard233 streams per
phase,748 models,dropped0; fast451 streams,147 models,dropped0. Equal six-phase
stream counts. A first fast trial overlapping compilation was excluded and
repeated with builds finished. Fresh scene images and native restore/readback
verified, streaming settled; no graphics/settings changes. No ordinary frame
performance claim from this diagnostic.

Courtyard per eye G-buffer callbacks2682.41 vs fast379.26 (~7.07x); summed
inclusive callback wall time6.7863ms vs1.7491ms for the two G-buffer streams.
DrawScene callbacks330 vs92 per eye, summed wall0.6712ms vs0.1874ms. QPC timing
includes diagnostic overhead, routing and descendants. Dominant courtyard
models include284 CJ_BARRIER_10 callbacks/eye,152 CJ_PROC_PLANT_5,186
CJ_PROC_Stone2,178 CJ_PROC_PLANT_28,148 C_Fern_MD_INGAME_2. Largest individual
model is only0.4034ms combined eyes, so it is not a sole explanation. IDs are
mapped by model-info+3C hash against installed IDE names. These counts include
early returns, not D3D draw counts or distinct objects. The observer detaches
before logging; ordinary rendering has no active observer hook afterward.

Next investigation: follow indexed draw/model/material and viewport-constant
call chains, distinguishing extra visible geometry from redundant state or
command work. Preserve accepted culling/LOD correctness and the grass mod;
do not infer speedup from sampled counts or remove objects blindly. #013 open.

Ghidra `Issue013SampledIndexedDraw`00426180 and
`Issue013GeometryMaterialLoop`00444C50 annotations were previewed/applied/read
back and saved. No synthetic PDB or static reanalysis performed. Current native
observer has detached; live status PID30452 remains FOCUSED with zero command
buffer overflows. Grass mod remains installed and graphics configs unchanged.

## 2026-10-05: sharing work between eyes versus parallel callbacks

Source `BuildFrame` explicitly invokes the original builder twice, replacing
phase viewports per eye. Eye1 repeats only G-buffer, lighting and DrawScene;
shared shadow atlases/reflections/weather/UI are not rebuilt a second time.
This is the current integration strategy, not a requirement to recompute all
scene preparation for stereo. Main-world callback execution is currently
repeated, including some eye-independent work. Assets are already-resident
meshes/materials, not recreated by the measured model callback.

Read-only live PID30452/base007E0000 confirms model callback008DCBF0 resolves a
resident drawable; normalizes command quaternion+10 in place; calls setters
00AC6EF0/00AC6FB0/00AC7050; constructs a local transform via006708D0 and copies
translation from command+20; invokes drawable virtual+20 or+2C. Verified
006708D0..006709AA is a quaternion-to-matrix helper without camera access. It is
reusable instance preparation in principle. However its isolated slow-trace
exclusive cost is only20/28309 samples (~0.071%); this cache alone is not a
credible explanation or full solution for the remaining frame budget.

00AC6EF0 and00AC6FB0 compare/write process-wide scalar state and call004374E0
on change. Combined with the earlier global device/constant caches and mutable
command quaternion, the callback is not an independent thread-safe job. Simply
calling it concurrently from native dispatcher008DE0D9 would race shared render
state and command order. D3D9 API locking would not protect all RAGE state or
make ordered commands execute in parallel. Do not patch this into concurrent
native calls without extracting ownership first.

Potential design: immutable per-frame scene/instance snapshot -> share mesh,
world-transform and camera-independent material preparation -> generate bounded
per-eye render packets using separate state -> submit ordered native/API commands.
Pure preparation may then be moved to workers; CPU command/state handling must
remain serialized unless replaced with separately owned command contexts.
Measure the larger drawable/material descendants before selecting a cache or
worker boundary, including material pass/variant/instance/transform/frame keys;
model ID alone is not a valid cache key. This is investigation, not an implemented
optimization or concurrency guarantee.

GPU offload is possible for suitable geometry/culling/instance preparation,
but existing rasterization and shader vertex transforms already run on GPU.
CPU traversal/state/API calls cannot be moved by relocating their callsite.
Vulkan multiview could issue one command stream for both views, while retaining
eye-specific projection, rasterization and view-dependent shading. Current
DXVK backend still accepts D3D9 commands; it does not automatically convert the
native stereo renderer to multiview. An implementation needs compatible shader,
layered target, constant and per-pass routing changes, plus validation of shadows,
reflections/transparency/culling. It is a larger option, not ruled out.

Sources: Microsoft D3D9 threading differences
https://learn.microsoft.com/en-us/windows/win32/direct3d11/overviews-direct3d-11-render-multi-thread-differences
and Khronos Vulkan multiview
https://docs.vulkan.org/tutorial/latest/OpenXR_Vulkan_Spatial_Computing/08_Slang_Spatial_Shaders/02_native_multiview.html .
Ghidra reusable-transform annotation previewed/applied/read back/saved. No game
state, binary, renderer behavior or settings changed during this analysis.

## 2026-10-05: larger material workload and GPU instancing feasibility

User explicitly prioritizes material frame-time savings and GPU offload over
the0.071% quaternion helper. No transform-cache optimization pursued. Added
native material-binder observation to the existing five-second default-off
model probe. Verified live004373E0..0043745C thiscall/four arguments/RET10;
unique entry match, fixed six-phase counters, foreign threads ignored, original
function forwarded, hook detached before logs/reset. Metadata records declared
state/VS/PS entry counts, including entries that native helpers may skip.

x86 Debug then Release pass; regenerated vs2026 solution. Release SHA256
151550BC833146BBD75F76BE9E832DDA97E39C6C5C0F7A4E58C3F9650163E958.
Installed ASI/INI backed up in ignored issue013-material-probe-backup; prior
PID30452 bridge exit verified before deploy. PID30888 CE1.2.0.59/base007E0000,
SteamVR2.17.10/90Hz,1920x1080 eyes,GNU DXVK265888C3,grass mod1.2 unchanged.
Courtyard and fast native restore/readbacks/15s settle/fresh scene images pass.
Images show resting-headset body obstruction; time/traffic/headset pose are
not frozen. No claim of exact visual-match benchmark or optimization gain.

| Binder diagnostic | Courtyard | Fast |
| --- | ---: | ---: |
| Equal starts in each of6 phases |244|450|
| Dropped model rows |0|0|
| G-buffer binds/eye0 |5331.14|1398.57|
| G-buffer binds/eye1 |5240.66|1338.34|
| Both-eye G-buffer binder wall ms |2.38798|0.77810|
| All6-phase binder wall ms per pair |3.37548|1.18097|

Courtyard G-buffer metadata/eye0:9003 render-state entries,9052 vertex and19727
pixel parameters per stream; eye1~8757/8935/19619. These are not actual GPU
updates. Inclusive wall costs include downstream API calls and QPC/hook
overhead and are nested within the earlier model/eye timings, not additive
savings. The material path is a worthwhile target measured in milliseconds;
no claim that all3.38ms can be removed. Native00437170 and004372B0 already cache
current shader handles; DXVK already skips unchanged vertex/index buffers.
Previous constant observer reported zero consecutive repeated payload candidates.
Therefore blindly deleting all second-eye bindings is not valid. Stronger
candidate: compatible static-instance batching that avoids repeated material
setup/API submission, with per-instance shader data replacing world constants;
validate mesh/subset/material/pass/alpha/wind/shadow/eye dependencies first.

GPU candidate: native corona00C1F170 constructs camera-facing quads using six
36-byte immediate vertex records per sprite. Raw sample cost2.10% in corona
itself plus4.00% in the shared append helper (not all append cost attributable
to coronas). Earlier slow caller probe6369 quads/eye vs607 fast; native batching
already combines up to2730 sprites per draw. The opportunity is CPU expansion,
copy/upload volume and preparation, not reducing thousands of corona draw calls.
Instanced vertex shader expansion is preferable to adding compute + CPU readback.
It can use D3D9 Shader Model3 / SetStreamSourceFreq through current DXVK without
raw Vulkan buffer interop. Compute/indirect batching remains a possible larger
backend change when workload/data ownership warrant it.

Added standalone x86 `tools/bridge-probe/billboard-instancing.cpp` with authored
HLSL. Debug/Release builds pass without warnings. On installed GNU DXVK265888C3
and RTX4070Ti,128x128 synthetic output:16 instances,shared instance buffer,two
eye matrices,CPU-expanded vsGPU-instanced mismatch pixels0/0,litPixels8192.
Logs moved into ignored native/billboard-instancing-dxvk.log/result.txt; game
renderer untouched by this standalone run. The final signedness-only source
cleanup rebuilt successfully; the passed run precedes that nonsemantic change.
Readback is correctness-only, no intended production CPU roundtrip. No GPU speedup
or native corona compatibility established. Main game remains live FOCUSED,
zero overflows and probes detached. No repeated flat-screen launch needed for
these default-off diagnostics/standalone shaders.

Integration gates: capture/verify native vertex declaration and shader inputs
(including extra per-vertex data), retain native visibility/occlusion/fade/atlas
UV/blend ordering and exact per-eye camera basis, validate known lights in both
eyes/night/day/pause/reset/fallback, compare ordinary/prototype/ordinary Release
captures with observers off. Initial geometry test covers only synthetic opaque
quads and does not establish any of those game gates. Preserve native fallback.
Ghidra material-binder annotation previewed/applied/read back/saved.
Reference for D3D9 instancing:
https://learn.microsoft.com/en-us/windows/win32/direct3d9/efficiently-drawing-multiple-instances-of-geometry .

## 2026-10-05: native billboard shader contract and preserved-shader GPU proof

Added a default-off extension to `model-command-probe`: mid hooks bracket the
native batch draw at analysis VA00C1F940/00C1F945; the D3D draw detour takes one
getter-only snapshot per marked phase. No buffer lock/readback, device setter or
native rendering replacement. Disassembly/logging occurs after detaching the
native hooks; diagnostic D3D draw hooks are removed at the next left-eye marker.
Snapshots are not benchmarks or complete shader-variant coverage.

Regenerated the vs2026 project; x86 Debug/Release pass. Initial Debug C1060 heap
failures persisted after limiting `/MP` via the environment. Preserved Debug
objects locally, disabled compiler parallelism in the ignored generated project
and used the x64 compiler host; Debug also used `/Zi` instead of `/ZI`. This was a
local build workaround, no checked-in build policy change. Release completed after
the user interruption. Logs:issue013-corona-shader-debug/release.log. Installed
plugin/INI backed up before deploying Release SHA256
4B35C700B9A794D18E956A22CC0BAF2E5EA9535D369523A06C1B011C3EA8B8DA.
CE1.2.0.59/PID3520/base00BF0000/renderTID8268; SteamVR2.17.10/90Hz,
1920x1080 eyes, GNU DXVK265888C3 and separate grass mod unchanged. Runtime
callsite0140F940 corresponds to Ghidra00C1F940; do not reuse prior007E0000 base.

Courtyard restore completed with readback,15s streaming settle and fresh native
window image. Nighttime view/body obstruction and an overlapping timing window
limit visual inspection. Capture233 equal six-phase streams,dropped0:

| Native draw snapshot | Eye0 DrawScene / phase2 | Eye1 DrawScene / phase5 |
| --- | ---: | ---: |
| Getter snapshot valid | yes | yes |
| D3D primitive type |4 / triangle list|4 / triangle list|
| Primitive count |5460|5460|
| Vertex stride |36 bytes|36 bytes|
| Captured VS / PS |3340 /332 bytes|3340 /332 bytes|

Other phases had no corona draw capture, explicitly logged. Actual declaration:
POSITION0 FLOAT3+0, NORMAL0 FLOAT3+12, COLOR0 D3DCOLOR+24,TEXCOORD0 FLOAT2+28.
The NORMAL field is the light center, not an ordinary surface normal. The VS
uses center/camera distance, a special near-light branch and multiple depth
samples to hide/fade occluded lights, then transforms the quad and applies fog.
The pixel shader samples the atlas, multiplies color and writes depth. The
captured states disable depth testing/writes and use additive ONE/ONE blending;
the shader's own occlusion still matters. Eye projection/position constants differ.
Native shader dumps and extracted assembly/constants remain ignored local inputs.

Added authored `VRBillboardShader::AddInstanceExpansion` and standalone
`instance-shader-prefix.cpp`. It preserves the original runtime assembly body,
substitutes computed position for v0 reads, and introduces instance half axes.
Unsupported input registers/occupied r30/r31 or declaration ordering are rejected.
SM3's one-distinct-input-register-per-instruction restriction is honored. No game
shader is embedded in the repository or replaced in the installed game.

Installed x86 DXVK/RTX4070Ti accepts both reconstructed original VS3248 bytes and
prefixed VS3360 bytes (reassembly omits original comment metadata). The standalone
runner uses captured native VS/PS and both eyes' float constants,16 synthetic
lights, white atlas and controlled floating depth texture,128x128 target:

| Native shader pixel case | Mismatched pixels | Lit pixels |
| --- | ---: | ---: |
| Eye0 visible |0|144|
| Eye0 occluded |0|0|
| Eye1 visible |0|144|
| Eye1 occluded |0|0|

This proves GPU corner expansion can retain the captured native shader's visible
and occluded paths under those inputs. It does not cover the near-light branch,
every fog mode, real atlas UV/color-alpha encoding, native distance shifts, all
shader variants or ordered native geometry. Decimal float dump NaNs preserve
their classification, not payload bits. Readback is test-only. No speedup claim.
Local results:instance-shader-equivalence-dxvk-result.txt/log. Main game remains
FOCUSED/live with zero command-buffer overflows, native diagnostic hooks detached.
Latest rolling51Hz is a health readback, not an optimization comparison.

Next integration boundary: produce compact native records instead of six36-byte
vertices, retain visibility/fade/atlas and eye-dependent distance shifts, preserve
native shader/state caches and blend order, reset/release instance resources and
restore stream frequencies. Only then compare ordinary/prototype/ordinary Release
captures with diagnostics off. Larger static-instance/material batching remains
the other target; #013 stays open. Ghidra GPU billboard contract bookmark was
previewed, applied, read back and saved.

## 2026-10-05: compact GPU billboard integration and first matched captures

The extended native observer captured194 equal streams in PID24368, zero dropped
rows. First six native36-byte vertices match the compact32-byte record formula
exactly in both eyes: center float3, radius, brightness, distance push, packed RGB,
flags. Native right/negated-up axes and distance shift reproduce position/center,
UV, alpha atlas coordinate and brightness. Local check-corona-record.py reports
zero error. These local vertex/shader inputs stay out of Git.

Compact prefix preserves the runtime native shader body, builds shifted center
and corners on the GPU and reconstructs native inputs. The standalone captured
shader/colored2x2 atlas/depth/distance-shift test passes both eyes: visible168/144
lit pixels respectively, occluded0, all cases zero mismatches. No production
readback. Unsupported scratch/input/declaration variants are rejected.

Added default-off bridge-controlled GPU replacement. Unique native batch boundary
C1F334 -> C1F945 bypasses CPU six-vertex expansion only in normal flags0,0 and main
eye DrawScene streams. Native visibility mask selection/order retained; compact
records uploaded to a dynamic instance VB, shared four-corner VB plus six indices.
32 bytes/light replaces216 bytes of expanded vertices; this is a structural
upload reduction, not a frame-speedup multiplier. Original depth/fog/atlas shader,
textures, constants and blend states retained. D3D bindings and stream frequencies
restored; missing signatures/resources or unsupported shaders retain native path.
Reset/shutdown disable and release resources; broader lifecycle tests pending.
Rejected shader variants cached so rejection does not reassemble every batch.

x86 Debug/Release pass (existing dependency-PDB warnings; local serial/x64-host
Debug Zi workaround retained). Installed ASI/INI backed up, verified bridge exit,
Release206104DCCF7C6BB81D1076029B71C48567DB39E3709CD8375E3FB22CD753E641
installed. CE1.2.0.59/PID25644/base00BF0000/renderTID23400; SteamVR2.17.10/90Hz,
GNU DXVK265888C3, 1920x1080 eyes, separate grass1.2 mod/config/launch flags unchanged.
Courtyard restore completed/read back, streaming settled. Fresh images show live
scene before/with GPU path; nighttime and character-body obstruction limit light
inspection. Not a normal-view90Hz acceptance check. Runtime path actually executed:
initial4444 batches/12047522lights/0fallbacks; timing activation9076 batches/
24669456lights/0fallbacks, including warmup and control-command delays.

No Computer Use/builds/offline analysis during these same-process30-second CPU-only
captures; GPU queries/native observers off, headset held, FOCUSED throughout,
zero metric drops/command overflows. Captures ordinary-a / active / ordinary-b:

| Mean metric (ms) | Ordinary A | GPU | Ordinary B |
| --- | ---: | ---: | ---: |
| DrawScene eye pair |5.2360|4.8218|5.3612|
| GBuffer eye pair |11.0796|11.1622|11.2127|
| Render before xrWaitFrame |17.1382|16.8374|17.4278|
| Submission interval |23.5235|23.2936|23.9637|
| Submission p95 |25.5419|26.1546|25.8862|

Local files gpu-billboard-ordinary-a/active/ordinary-b.json/CSV remain ignored.
There is a candidate0.4-0.5ms reduction in the targeted scene phase, but no tail
improvement or sustained90Hz recovery. Traffic/time/idle animation remain live;
precise causality and general gain need more representative graphics validation.
The experiment is off after testing and has no distributed enable setting.
Near lights, fog/variants, both-eye manual inspection and reset/fallback coverage
remain open. Ghidra Issue013GpuBillboardRuntime bookmark previewed/applied/read
back/saved. The packed import remains intact; binder decompilation is unreliable,
so subsequent batching analysis uses verified live disassembly.

Next larger target: bounded first-eye GBuffer grouping by actual mesh/subset/pass
and shader/resource bindings. Repeated keys must still be checked for texture,
constant, animation, extra-stream and ordering differences before instancing.
The default-off diagnostic changes no draw decisions and is excluded from timings.

### First native mesh-binding census

Default-off model observer now takes getter snapshots for only the first eye0
GBuffer stream. Key includes model context (model+1,0unknown), native pass pointer,
VS/PS/declaration/VB/IB, stream0offset/stride and all indexed draw arguments.
Fixed8192 rows, no vertex/index readback or draw changes. Counts after native
hooks detach; repeated keys are candidate compatibility, not duplicate geometry.
Debug/Release8E9DB5A86C10C6758FC91EEA3D970C6AA5EE5F926CB5DDFA0575263F9999936A
pass after retrying outside restricted process boundary (initial DebugC1902 PDB
service mismatch; retry succeeds). Backed up206104DC+INI, clean exit/deploy and
courtyard restore/readback; PID23676/renderTID24896, same runtime/backend/mod.
Five-second203 equal six-phase streams, native table drops0. First GBuffer:
5146 indexed draws,1988 distinct keys,3158 repeated-key draws, getter failures0,
key-table drops0. Local mesh-bindings-courtyard.txt/summary.json remain ignored.

| Model ID / hash | Matching-key draws | Primitive counts |
| --- | ---: | --- |
|788 / F5EBF068|178|12|
|1838 / E02F3A89|142 per group|36,188,20|
|761 /22E5469D|81 per group|332,234|
|3698 /5E50EDD3|72 per group|232,232 (different passes)|

These are distinct instance draws sharing a recorded binding key. About61% of
recorded calls are repeats of a key, not61% proven savings. Tex/sampler/render
states, float/int/bool constants, extra streams, transforms, wind/animation and
ordering are not part of this key. Some model0 rows are draws outside observed
native model callbacks and must not be confidently assigned to a model.
Diagnostic overhead is excluded from benchmarks. No mesh batching installed.
Next step compares float constants and textures within one repeated known-model
group, and retains its runtime VS locally to inspect per-instance dependencies.

### Repeated mesh shader dependencies

Two bounded read-only state runs completed. Release0849130A/PID25068/render20528:
183equal streams,5480indexed draws/2097keys/3383repeats, no failures/drops.
Selected model788/key789 after32nd match,147state samples,texture mask0036;
VS captured. Refined lane/PS capture Release2BC8AB1A6212AC62BD4BA26B52BE4EB2130E19FF18DAE20ABB9CEC23F0661F0A
passes x86 Debug/Release. Backup/verified clean quit/deploy/restore/readback passed;
PID9256/base00BF0000/render2804.348equal streams; first-stream1879draws/611keys/
1268repeats, zero failures/drops. Lower first-stream population despite later
larger model-reference peaks is a warmup/streaming limit: not a performance gain
or directly comparable workload. Selected model1838/key1839 group135draws,
104state samples after first31,failures0,textureChanged0016 (slots1,2,4).
Local mesh-state-courtyard.txt and mesh-lanes-courtyard.txt/VS/PS assembly ignored.

Actual selected VS: position and normal transform read c0..c3, clip/projection
reads c8..c11; UV c208/209 and vertex shading c39.z/c40.z/c45. Shader defines c4
itself, so API c4 changes do not alter this shader. Active changed lanes in this
sample: c0.xy,c1.xy,c3.xyz,c8/c9/c11.xyzw. c2/c10 remained unchanged here; that is
not a guarantee for other orientations. c39/40/45/208/209 unchanged. Many changing
high API registers are not read by this shader and must not expand the instance
payload just because they differ.

Actual PS uses only s0, which remained identical (changed slots1/2/4 are unused).
Defines c0/c1/c2 itself; their API changes are irrelevant. Active non-definition
reads c39.x (fade), c52.x, c207.x,c209.xy,c223.z stayed unchanged in this sample.
Native shader writes four GBuffer outputs and depth, with texkill/alpha dithering;
retain the original shader and clipping, not an opaque-color substitute.

Concrete candidate: one per-instance world matrix and clip matrix (c0..3 and
c8..11), native shader/atlas/material retained, group only proven-compatible mesh,
subset and states. Later move clip composition to GPU/shared-eye matrices if
matrix conventions are verified. Need verify sampler/render state, all vertex
streams/frequencies and int/bool/relative constants; ensure instance visibility,
fade/animation, texture0 and command order remain correct. Original native model
callbacks still own shared RAGE state; do not parallelize them blindly. A draw-only
prototype might reduce driver draw submission but would still leave preceding
native binding work, so measure that separately before claiming the larger goal.
No mesh-instancing replacement yet. GPU billboard experiment enabled in PID9256
for pending user both-eye light check; still default/reset/shutdown off. Latest
live session FOCUSED,continuous,command overflows0; rolling Hz not a new benchmark.

### Rigid native shader instancing proof

Added authored VRRigidInstanceShader::AddInstanceMatrices and standalone
rigid-instance-prefix.cpp. Runtime native VS body retained; c0..3/c8..11 operands
become eight instance float4 attributes, moved to unused r16..23 before original
instructions. Unsupported input layout, occupied scratch registers, matrix defs,
relative constants and oversized/non-SM3 assembly rejected. No native shader or
asset embedded. This is a test/helper, not a game mesh batcher.

Standalone x86 Debug/Release builds pass /W4 without warnings. On installed
GNU DXVK265888C3/RTX4070Ti, original VS648bytes -> instance VS840bytes accepted.
Captured native VS and actual four-output/depth/texkill PS, authored geometry,
colored2x2 atlas,16 independently rotated/transformed instances, two clip/eye
matrices. Reference draws16 times with original matrix constants; prototype draws
once with128-byte instance records. All four128x128 GBuffer color targets match
exactly for both eyes: visible5121lit/0mismatches per target, cutout0lit/0mismatches.
Final relative-address guard and strict cutout-empty check rebuilt/retested pass.
Readback is test-only. Depth output was retained in the shader but no depth surface
was bound: this proves four color targets and cutout parity, not depth-buffer
parity, occlusion-query behavior, real native geometry or game ordering/reset.
Local rigid-instance-equivalence-result.txt/dxvk.log and source shader inputs ignored.
No concurrent performance capture/Computer Use; no mesh-instancing speedup claim.

User checked live GPU billboard mode after the shader captures: street/distant
lights in both eyes look unchanged during movement, no new graphics issue reported.
This accepts that manual route only; near-light variants/reset/fallback still open.
It remains opt-in/default-off, currently live for development. #013 stays open.

Rigid proof additionally extended with D24S8 depth, depth-write/LESS enabled,
instance clip depths0.2/0.4/0.6/0.8 and a later magenta plane at0.5. Debug/Release
rebuild and retest pass. Both eyes/all4targets0pixel mismatches; visible16384lit,
cutout0. Plane covers13829pixels and2555foreground pixels remain, so the result
exercises shader-written depth rather than two identical all-background images.
Depth observed indirectly through color/occlusion, not raw depth bits. Reference
16instance draws plusplane vs1instanced draw plusplane. Local rigid-instance-depth-
result.txt/dxvk.log ignored. This strengthens the earlier no-depth-surface proof;
native game ordering/query/resource/lifecycle and performance gates remain open.

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

## Live replacement result (2026-10-05)

A real, opt-in replacement now skips compatible native indexed submissions and emits instanced geometry into the actual eye G-buffers. `mesh-live-probe` runs one eye pair, then ordinary continuous rendering resumes. This is not scratch replay. CE1.2.0.59, SteamVR2.17.10/90Hz, DXVK-Vulkan,1920x1080 per eye; ReleaseCA6FB79F3BEDFB6BCE3955653685EC871682F8AF74CC888A27E7033A204F8B01/PID7828. Saved city location restored/read back, streaming settled and fresh image inspected; no CU/build/fixture work during captures, FOCUSED before and after.

Four native eye-pair G-buffer CPU totals:10.9049,10.2059,10.2751,9.9535ms (median10.2405ms). Three replacement pairs:467.8093ms cold,356.9839 and360.1277ms warmed. The two warmed pairs' median is358.5558ms. These are CPU spans for the G-buffer callbacks, not whole-frame timings or GPU busy time; one-shot hook setup/teardown is outside them.

Warmed actual total indexed calls, including untouched native fallbacks:

| Eye capture | Native candidates | Queued | Emitted for queue | Instanced batches | Actual total calls | Reduction |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| pair2 left |6462|5393|2766|533|3835|40.65%|
| pair2 right |6520|5448|2833|532|3905|40.11%|
| pair3 left |6448|5394|2738|502|3792|41.19%|
| pair3 right |6508|5449|2814|491|3873|40.49%|

No reported submission/restoration failures or command-buffer overflows. This establishes real draw replacement, not performance acceptance or general scene parity. Native material/model callbacks still run. All native-before/after pairs are ordinary rendering; neither the scratch benchmark nor the live draw reduction is a model-callback or FPS percentage.

Warmed per-eye median CPU costs: collection122.1281ms, including full state-block capture91.1775ms; emission50.1873ms, including state-block apply25.3103ms and instanced emitter0.8648ms. The subcategories overlap their parent spans and must not be summed. Initial shader adaptation/driver warmup cost38.8685ms in the first left-eye emitter, versus~0.83-0.90ms warmed. Hash lookup retained complete equality but did not fix state-capture/replay overhead.

Conclusion: the shader genuinely replaces live draws and substantially lowers call count, but this full-state packet collector is decisively slower. Keep it bounded/off in ordinary gameplay. The next implementation must collect lightweight native binding/geometry records and replay only required/changed state, preserving resource/query effects; then measure total live cost again. No production frame-rate improvement demonstrated. Local raw evidence:live-grouped-hash-result.txt, authored control/tangent Release and Debug fixtures (both eyes/allMRT/cutout/depth-plane/native-skip checks) pass. Equal-depth reordering in other scenes, cross-thread resource mutations, sustained gameplay and flat-screen lifecycle acceptance remain open. Issue013 stays open.

## Direct CPU-work omission upper bound (2026-10-05)

Per user request, tested deliberately incomplete live rendering before more shader/packet refinement. CE1.2.0.59, SteamVR2.17.10/90Hz, DXVK-Vulkan,1920x1080 per eye; x86 Release81F25E359F8A7D6A134E4405E78309D24D7E6B971C7B3BDF998097CE8CC6A8D3, PID11372. Saved city-across-bridge position restored/read back, fresh image inspected and streaming settled. Parked headset leaves some character/car foreground; same held scene within this run. No CU/build work during measurements; FOCUSED endpoints and no command-buffer overflow.

| Condition | Successful stereo layers/sec, profiler off | Median CPU render-before-XR-wait ms | Median submission interval ms, profiler on |
| --- | ---: | ---: | ---: |
| Native before |40.48|19.6679|25.2014|
| Omit material loop00444C50 |66.46|8.0034|14.0837|
| Omit model command008DCBF0 |59.73|11.3640|16.9443|
| Native restored |41.66|19.4152|24.9468|

Four30s CPU-only phase captures and four independent15s counter windows. Material mode active before/after rate capture, model mode likewise (953ms remaining at final readback); explicitly restored mode0 afterward. The two targets are not globally nested: other command classes also reach the material loop. Both skip CPU preparation AND descendant draws, so this is an optimistic ceiling, not a shader replacement or production performance claim. No collection/state replay. Material omission lowers the CPU rendering span by about11.5ms and improves submissions about60%, but whole-frame14.1ms still exceeds11.1ms. Its remaining DrawScene medians2.1090/2.0173ms, frameOutsideEyes5.9501ms, xrFrameWall1.9648ms (p956.4358), xrCopy0 .8896ms (p955.4207). Parent spans overlap; do not add them or label frameOutsideEyes pure simulation. Material render-before-wait p958.6517/p9910.0949ms. Normal control medians/rates are close; no90Hz result.

Raw local evidence cpu-omit-{native-before,materials,models,native-after}.json and corresponding -rate.json. Normal rendering restored; issue013 remains open. Next requested attribution: omit entire main-eye GBuffer, DrawScene, lighting, then all three, retaining simulation/auxiliary rendering/OpenXR. This removes construction plus execution and deliberately invalidates images; it tests remaining phase cost rather than pixel correctness.

## Whole main-eye pass omission (2026-10-05)

Implemented requested coarse attribution before further shader work: modes3/4/5/6 omit main-eye GBuffer/DrawScene/lighting/all-three list construction plus execution. Boundary markers, auxiliary passes, command lifetime, simulation and OpenXR remain; default mode0. No GPU replacement, images deliberately invalid. Debug and Release compile successfully (existing dependency-PDB warnings only); no further Debug builds required for Release-only iterations per updated AGENTS.md. Release7F517D2FB5272BFCF1B626CFEAFEC39971DCD0312A96045CD229D6C114F7E43B/PID21916, same CE/SteamVR/DXVK/settings and saved city location. Normal quit/deploy backup/readback/settling/fresh image completed; no CU/build during measurements.

| Omission | Successful stereo layers/sec, profiler off | Median eye-render CPU wall span ms | Median whole submission interval ms, profiler on |
| --- | ---: | ---: | ---: |
| Normal before |41.54|not recaptured|not recaptured|
| All three main-eye passes |88.95|0.1014|11.1245|
| DrawScene only |58.86|11.7817|16.9950|
| GBuffer only |67.06|8.8988|15.1787|
| Lighting only |40.77 (short5s window)|18.8166|24.3437|
| Normal restored |42.35|20.1662|25.8159|

30s CPU profiles; independent15s rates except lighting short5s. Active modes and positive deadline checked before/after accepted omission windows. Lighting's first15s rate crossed the automatic90s timeout and is excluded (phase-omit-lighting-rate.json); repeated only that invalid rate with a short5s window, active5 at both ends. Its30s phase profile completed while active5. Rate helper now rejects changing/expiring omission modes and stores endpoint mode evidence. All profiles FOCUSED endpoints/validComparisontrue; command-buffer overflows0. Time/weather/traffic/headset orientation not frozen; comparisons establish large effects, not precise additive savings. Normal before/after close.

Normal restored GBuffer medians5.6766/5.5663ms, DrawScene3.8547/3.7239ms, lighting0.5372/0.5217ms, frameOutsideEyes5.6221ms. Large CPU wall costs occur in BOTH GBuffer and DrawScene. Removing just one still leaves the other. Material-loop removal's rate66.46 from preceding run is close to full GBuffer removal67.06; do not treat the small difference across runs as significant or exact scope containment. Lighting removal has no demonstrated meaningful rate gain.

All-eye removal approaches the intended90Hz runtime cadence:11.1ms interval is a paced submission period, NOT11.1ms CPU processing. Remaining frameOutsideEyes median10.9880 includes synchronization/runtime waits; xrFrameWall7.0060, first eye Copy6.1204 (nested copy/fence6.0395), xrWait only0.0162ms. The bridge's Copy includes DXVK transition/flush/resource completion, queue submission and Vulkan fence wait; exact6ms substep is not separately timed here. Therefore do not describe all residual wall time as simulation or CPU work, nor claim measured xrWaitFrame itself consumes the6ms. It does not establish an unavoidable14ms floor: omission reaches~89 actual layers/sec. Retained shared/auxiliary work alone does not prevent target cadence in this view. Further production optimization must lower preparation/submission in both expensive passes and avoid introducing packet replay overhead. Omission removes native side effects/draws and cannot prove every skipped operation is shader-replaceable.

Normal rendering restored mode0 and fresh city image inspected after tests, responsive/FOCUSED continuous stereo. Local evidence phase-omit-{eyes,scene,gbuffer,lighting,native-after}.json, corresponding valid rate files, phase-omit-native-before-rate.json and phase-omit-lighting-short-rate.json. Issue013 remains open. Prior native callback omission findings persisted/read back/saved as Ghidra Issue013CpuOmission at00444C50/008DCBF0; no new native address interpretation needed for phase omission.

## Uncapped producer result and pipeline decision (2026-10-05)

User clarified90Hz hardware is fixed and requested render-as-fast-as-possible, not a display overclock. Undeployed refresh-rate request removed. Added development-only producer-test on/off/status: pauses XR transfer/submission for at most15s, retains explicitly cached pose, bypasses ordinary gameplay limiter, continues native simulation/eye-command execution/desktop presentation. CPU completed-pair counter increments only for uniquely published Vulkan pairs; GPU completion is not verified and this is NOT headset FPS. Shutdown/reset/focus loss clear the mode. tools/capture-producer-rate.ps1 measures short bounded RPC deltas, rejects state changes, restores XR in finally.

Release5C786C91BBFB49CC82659FC13D4CEC624288CEDFC3633DB730DA7B9CDF3AFABA/PID25076, CE1.2.0.59/SteamVR2.17.10/DXVK-Vulkan/90Hz,1920x1080 per eye. Saved city restored/read back, streaming settled, fresh image. Five-second windows, profiler off, no CU/build during captures, focused endpoints/zero overflows:

| Native work | Normal XR submissions/sec | Uncapped CPU eye-command pairs/sec |
| --- | ---: | ---: |
| All normal rendering |41.02|41.86|
| Material loop omitted |64.95|79.24|
| All main-eye passes omitted |prior comparable88.95, not recaptured|101.33|

Normal bounded ranges40.49-41.56 versus41.55-42.18: no demonstrated meaningful current-scene gain from removing the entire synchronous XR path. Material omission78.89-79.60 uncapped versus64.38-65.53 XR suggests submission/synchronization can matter AFTER reducing native rendering cost. Different views/traffic/time and short windows limit fine percentages. All-eye omission100.86-101.81 command pairs/sec exceeds90, but says nothing about valid production GPU/headset throughput. All tests restored producerActive=false/cpu-omit mode0; ready continuous stereo/nooverflow. Local producer-{native-xr,native-uncapped,materials-xr,materials-uncapped,eyes-uncapped}-rate.json. No new baseline suite or manual graphics test requested.

An asynchronous triple-buffer submission implementation was started at user direction, then canceled after the user emphasized avoiding negligible work and requested candid assessment. Unfinished edits removed; no async build was deployed, no async production gain claimed. Current pipeline remains synchronous. This is deliberate reprioritization: normal producer test does not justify async as the main performance fix. The term prototype was too broad: a sequential wait/begin/render/end loop is ordinary OpenXR. Our actual limitations are game-owned targets plus copying/CPU completion waits, poses currently sampled after rendering for the next game frame, and incomplete broader latency/lifecycle/compatibility validation. D3D11 remains a conditional fallback and is not the active DXVK transfer path; inactive code is not measured overhead. Follow native GBuffer/DrawScene costs before more shader parity/architecture work. OpenXR reference: https://github.com/KhronosGroup/OpenXR-Guide/blob/main/chapters/frame_submission.md .
