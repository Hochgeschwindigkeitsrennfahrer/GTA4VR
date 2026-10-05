# Frame profiling (#013)

Use a Release ASI with the development command bridge enabled. Profiling remains inactive until `profile-start`; it collects at most 8192 samples per metric for 30 seconds in memory. It performs no file writes in the measured path. `profile-stop` ends a capture early. A new start clears the prior capture. Samples crossing a capture boundary are discarded.

Once positioned at the desired view, run `tools/capture-frame-profile.ps1 -Label apartment-baseline` or `-Label street-baseline`. Keep the headset tracked and the scene held throughout the capture. Do not request `stereo-test`, change graphics settings or capture eye images during a run. Warm up after loading first. Raw results default to ignored `build/profiles/`.

CPU scope timings are **wall-clock spans**, including driver stalls. `processStub` (originally named `gameUpdate`) times only the original RET stub targeted by FusionFix's process hook; `processCallbacks` times the preceding FusionFix callbacks. Neither measures total native simulation cost. `framePrepare` measures the renderer preparation call and includes nested visibility/list construction; do not sum these overlapping spans. `eye0Build` includes shared auxiliary phases, while `eye1Build` repeats world phases only. `eye*Commands` runs between queued world markers on the render thread; this is command dispatch/driver time, not GPU execution time. `publish` includes desktop mirroring, the nested `d3d9Fence` and eye publication. `xrCopy*` includes swapchain acquisition/wait, D3D11 copy completion and release. `xrWait` includes intentional runtime pacing. `xrEnd` is the runtime submission call.

`submissionInterval` measures spacing between successful projection-layer submissions, including possible reuse of an eye pair. The count above 11.11 ms is not a compositor dropped-frame count. `desktopPresent` times Present/PresentEx or swapchain Present on the actual world-rendering device, excluding duplicate nested calls; zero samples means these entry points were not observed. SteamVR Frame Timing supplies complementary compositor evidence. A screenshot alone is not a percentile distribution.

`eye0GPU` and `eye1GPU` use a bounded ring of D3D9 timestamp/frequency/disjoint queries around queued world markers. Results are read on later frames without flushing or waiting. Unsupported queries, disjoint intervals and a full ring produce missing samples rather than stalling rendering. GPU intervals exclude shared auxiliary passes outside the world markers; CPU command timing overlaps them and must not be added. The first capture initializes the query ring; use a second warmed capture for comparison. Query events and result types follow [Microsoft's D3D9 query documentation](https://learn.microsoft.com/en-us/windows/win32/direct3d9/queries).

Record commit/ASI hash, executable/runtime/headset/GPU versions, per-eye and desktop resolution, refresh, graphics settings, location, view direction, weather/time and warm-up. Compare an apartment hold, an exterior hold at the same doorway/subway view, then apartment-to-street walking and a short drive. Separate streaming spikes from steady-state cost. Repeat each relevant run before and after a single optimization and verify unchanged visibility/collision and one game update per stereo pair.

The submitted OpenXR device is D3D11, while scene rendering happens on a separate D3D9 device. Treat SteamVR's application GPU bands cautiously: internal D3D9 timestamps are needed to establish the producer's world-render cost. Disable old development probes that write periodic files (for example `ProbeFirstPersonAnchor`) before collecting a performance baseline, after backing up the installed INI. Keep gameplay and graphics settings identical between comparison captures.

Native boundary correction: CE1.2.0.59's source hook signature matches the call at Ghidra `005C2A41`, targeting `004016A0`. In PID28652 (live image base `006D0000`), bounded read of `006D16A0` returned `C3 CC CC...`: the original target is a return stub. Thus initial `gameUpdate` samples are not simulation timings. The static imported decompiler output at that target is obfuscated/overlapping; use the runtime bytes and callback coverage rather than treating it as a simulation function. Real simulation attribution remains open.
`processStub` replaces the misleading initial `gameUpdate` label; `processCallbacks` measures the preceding FusionFix callbacks, neither is total native simulation time. `xrImageWait*` measures acquire/wait and `xrCopyFence*` measures the copy through completion. Both are nested within `xrCopy*`. Limiter metrics measure actual calls to gameplay, cutscene, loading, 60 FPS loading safety and minigame limiters. `runtimePacingBypass` records zero-valued samples solely to count limiter-hook invocations with active OpenXR pacing; its duration is not useful work.
`eye*GBuffer`, `eye*Lighting` and `eye*DrawScene` are nested CPU/driver spans at the existing queued phase markers; they include routing and rebind work, not GPU busy time. `xrCopyIssue*` times CopyResource, `xrQueryCreate*` times event-query allocation, `xrFlush*` times End/Flush, and `xrPoll*` times completion polling. These spans sit within the existing copy/fence parent. Do not run compilation, screenshot capture or settings changes during a baseline capture; repeat any contaminated run.
`profile-start-cpu` (capture client `-CpuOnly`) records the same CPU metrics while skipping new D3D9 timestamp-query insertion/readback. Any already begun interval is still ended correctly. `profile-status.gpuQueries` records the mode. Compare ordinary, CPU-only and ordinary captures in one held view to estimate query-related observer overhead; zero GPU samples in CPU-only mode are intentional. Ordinary `profile-start` restores GPU queries.
The capture client also records threadCpuWindowMs and per-thread user/kernel CPU deltas over roughly31seconds, with oneCorePercent normalized to one core for the measured wall interval. Only threads present in both snapshots are included. These are coarse CPU residency measurements, including spinning/polling; they are not per-frame percentiles or proof of a particular thread role. Match IDs against native marker evidence. Snapshot duration differs from the30second sample window and is recorded explicitly.
`eye0Routing`/`eye1Routing` aggregate texture/surface alias lookup durations across an eye stream, including measurement overhead. Thread-owned totals publish once per eye without per-call locking. `frameOutsideEyes` spans the end of eye1 through the next eye0 start; it includes transfer, presentation, auxiliary work and waiting, not just simulation. `interEyeGap` spans eye0 end through eye1 start. These gaps overlap other parent metrics and must not be added twice.

DXVK late-start attribution: `renderBeforeXRWait` measures eye0's first queued render marker through the corresponding pre-`xrWaitFrame` boundary on the same render thread. It includes both eye command streams and publication, not earlier simulation/list building or all of SteamVR's Late Start. Its boundary is consumed once and rejects stale capture epochs; zero samples may indicate different callback threads rather than zero work. `xrBegin` includes `xrBeginFrame` and its queue lock; `xrLocateViews` measures view location. `xrQueueBegin`, `xrQueueAcquire`, `xrQueueRelease`, and `xrQueueEnd` measure DXVK queue-lock acquisition only (including draining DXVK submissions); acquire/release have two samples per stereo pair. These are nested in parent spans. `xrFrameWall` includes the entire enabled EndScene runtime callback, including pacing, publication, synchronization and waits; it is wall time, not active CPU work. `posePredictionLag` is the nonnegative difference between submitted source-pose prediction time and the new frame prediction time in milliseconds, not measured motion-to-photon latency. Instrumentation does not move the frame wait or change pacing.

`ambientOcclusion` measures FusionFix's AO pass when enabled in the game. `profile-start-cpu-noao` or capture-client `-NoAO` bypasses that pass for at most30seconds to isolate a visual/performance effect. Capture status records `aoBypassSelected`; stop, expiry or any ordinary capture restores the normal setting without writing it. Do not combine with `-NoFlushPoll` or treat a quality-changing capture as an equivalent-quality speedup. Stereo readbacks during effect isolation contaminate timings; retain those captures as image diagnostics only.
`profile-start-cpu-noflush` (client `-NoFlushPoll`, which implies CPU-only GPU-query mode) temporarily uses D3D11_ASYNC_GETDATA_DONOTFLUSH after the existing explicit End/Flush. It retains successful completion before release, timeout and fallback. The override applies only while a capture is active; stop/expiry restores ordinary polling. Capture status records nonflushingPoll as the selected experiment, even after capture completion. Compare with CPU-only ordinary polling in the same view.
For native CPU stack sampling, use tools/capture-native-cpu.ps1 -Label street from an elevated PowerShell while holding the tracked street view. It records for30seconds and stops automatically; it refuses an existing WPR recording. Raw ETL and process/module metadata remain in ignored build/profiles/native. Analyze only the GTAIV process for the issue; do not commit raw machine-wide tracing data. This session could not enable the WPR CPU profile without elevation.

Analyze the resulting trace locally with Windows Performance Toolkit's `xperf.exe`. Set `_NT_SYMBOL_PATH` to the matching Release PDB directory and `_NT_SYMCACHE_PATH` to an ignored workspace directory. Resolve the render-thread ID from queued marker logs. For example, the verified street trace used PID18212/thread26952:

Preserve the installed ASI and its matching PDB together before another build overwrites `bin/`. The native capture helper records the installed ASI SHA256; a current workspace PDB may belong to a newer, undeployed build and must not be used as if it matches. If the stack decoder loses a module name, compare ETL image rundown and the independent `-a profile -detail` module report. The latter aggregates all game threads; its percentages cannot substitute for render-thread attribution.

```powershell
$env:_NT_SYMBOL_PATH = (Resolve-Path bin).Path
$env:_NT_SYMCACHE_PATH = Join-Path (Get-Location) 'build/profiles/native/symcache'
& 'C:\Program Files (x86)\Windows Kits\10\Windows Performance Toolkit\xperf.exe' `
    -i build/profiles/native/street-20261004-184231.etl -symbols `
    -o build/profiles/native/street-render-cpu-30s.html `
    -a stack -butterfly 10 -pid 18212 -tid 26952 -event 'Sampled Profile' -range 0 30000000
```

The event name includes a space. An unfiltered stack report includes context-switch events and is not a CPU-time distribution. WPR stop/rundown can extend the ETL beyond the requested duration; inspect `-a tracestats -timespan actual` and lost events, and bound the analysis window. Exclusive samples count instructions in a function/module; inclusive samples include descendants and overlap other totals. CPU sampling does not measure blocked-wait duration. PowerShell's module enumeration may omit x86 modules under WOW64; ETL image rundown supplies them. Keep these reports and caches out of Git.

## Shader constant call observer

`profile-constants` returns per-eye vertex/pixel source-call counts, requested float4 vectors, observed streams and exact-byte repeated-value candidates for the current capture. The capture client saves it separately as `constants`, with matching epoch validation. `eye0VertexConstants`, `eye1VertexConstants`, `eye0PixelConstants` and `eye1PixelConstants` aggregate wall time inside the original device setter calls per eye stream; they overlap eye command timings. The observer's comparison work is outside those setter spans but remains measurement overhead in the complete frame. Internal VR override writes through unhooked trampolines are excluded.

Observation runs only during bounded profiling and on the routed eye/render thread. Shadows are reset at phase boundaries and capture changes; known VR register overrides invalidate VS209/PS14. State-block Apply and other unobserved device writes are not fully tracked. Therefore repeated candidates are not proof that calls can be skipped, and no calls are suppressed. Counts are integers, not milliseconds. To test a cache later, establish state-block/reset/other-thread invalidation and preserve failures and eye overrides first.

Detailed constant observation is now explicit: `profile-start-cpu-constants` or client `-Constants` enables it, with GPU queries off. Ordinary `-CpuOnly` captures leave it disabled; stop/expiry or another start disables it. Status records `constantObserverSelected`. Compare ordinary/observer/ordinary captures in one held view to measure its overhead before interpreting fine timings. The earlier A233445A build observed constants during all captures; do not compare its instrumented frame times directly with a new ordinary capture as an optimization gain.

Lighting pixel shader discovery records at most16 shaders per eye-resource generation, each bounded to24KB of assembly in the existing local stereo log. Warm discovery before timing; no shader dump or raw game shader goes into Git. It is diagnostic evidence, not a graphical repair.

`stereo-test` also reads back the already-rendered per-eye G-buffer0/1/2 clones to local `plugins/GTAIV.VR.gbufferN.eyeE.bmp` files. It supports A8R8G8B8/X8R8G8B8 and converts A2R10G10B10 to8-bit display channels; other formats are explicitly logged as unsupported. These display files do not preserve exact10-bit values. The capture issues no draws or routing changes, but synchronous readback stalls the producer. Never trigger it during a timing capture. Preserve uniquely named local copies with the final eye images when comparing a held view; do not commit game imagery.

The same capture now logs G-buffer draw diagnostics: at most64 distinct pixel-shader/first-eight-texture tuples per eye, at most32 pixel shader assemblies (24KB each), actual render/sampler states and PS float constants. These are first observations of each tuple, not a complete history of constant/state changes. The indexed draw hook uses the actual IDirect3DDevice9 slot82; it forwards the original arguments/result, and reads device state only while the render thread's capture marker is active. Diagnostics can stall that pair and must be warmed/finished before profiling. No draws or state writes are suppressed. Local raw shader logs stay outside Git.

## Sparse draw-call attribution

Client `-Draws` selects `profile-start-cpu-draws`: CPU-only, constant observer off. Ordinary starts/stop/expiry disable it. `profile-status.drawObserverSelected` records selection. `profile-draws <eye 0-1> <stage 1-3>` returns one bounded response for G-buffer, lighting or draw-scene, and the client saves all six results as `draws`.

The render thread counts DrawPrimitive and DrawIndexedPrimitive calls, requested primitives, indexed vertex-range sizes and failed calls. It times approximately1/64 original device calls, rotating the selected positions across frames/phases. Counts are aggregated once per phase; no per-draw lock or device-state getter is used. DrawPrimitiveUP/DrawIndexedPrimitiveUP and calls bypassing the hooked methods are not covered. Requested geometry is not unique visible geometry. Sparse sampled mean/max refer only to observed original device-call wall time (including driver waits); they exclude the native preparation before that call, and are neither complete draw time nor GPU time. Never sum them with parent phase time. A rare slow call can be missed.

Compare ordinary CPU-only / draw mode / ordinary CPU-only in the same held view before treating their frame times as normal gameplay or making a speedup claim. No rendering calls are suppressed or moved to another thread.

## Sparse state/binding attribution

Client `-States` selects `profile-start-cpu-states` for a30-second CPU-only capture, with the draw/constant observers and diagnostic overrides off. It counts ten categories: texture, render state, sampler state, vertex shader, pixel shader, vertex float constants, pixel float constants, stream source, indices and vertex declaration. `profile-states <eye 0-1> <kind 0-9>` pages one category; the capture client saves all20 results in `states`. Ordinary starts and stop/expiry disable observation.

Counts are thread-owned between the eye's G-buffer start and draw-scene end markers; at most1/64 of each category's original device calls receive timers, with positions rotated by frame/eye/category. Totals publish once per eye. Texture routing, VR shader/constant correction and diagnostic shader discovery occur outside the measured original call, and direct calls to hook trampolines are excluded. Routed sampler writes inside the marked stream can be included. This is neither total native command preparation nor a full device history: state blocks, integer/bool constants, UP calls, texture-stage/stream-frequency/FVF changes and unhooked paths are excluded. A call count alone does not prove redundancy; no calls are suppressed and no device work moves to another thread. Sparse samples can miss rare stalls. Compare ordinary CPU-only / state observer / ordinary CPU-only in a held view to quantify observer cost before attributing performance.

The five additional state-only detours (render state, sampler state, stream source, indices, declaration) are now installed only for an explicit state capture at the owning render thread's first eye marker. They are removed at the next eye0 start after stop/expiry/ordinary restart, and during reset/shutdown. Installation/removal can disturb that transition; exclude those frames or warm up before comparisons. Existing VR texture/shader/float-constant hooks remain necessary for rendering. A failed optional hook setup disables this observer until reset and logs the failure; it does not block ordinary stereo.

## Diagnostic draw forwarding control

`-DrawForwarding` selects `profile-start-cpu-drawforwarding`: CPU-only phase timing with the two draw detours installed, but draw/state/constant observation disabled. Use ordinary / forwarding / ordinary captures in one held view to isolate detour overhead without a relaunch. Stop/expiry/ordinary restart clears the control. Initial stereo and explicit `stereo-test` diagnostics also temporarily install these hooks. Ordinary rendering removes them at eye0 start once those diagnostics complete. Hook changes can disturb transition frames. The capture JSON records `startCommand`; do not interpret zero draw counters in forwarding mode as no geometry being rendered. Ordinary routing bypasses the per-call profiler entry when the eye marker has no active capture stamp.

Native CPU capture refuses to start while the in-game profiler is active and records its before/after state in local metadata. This keeps ordinary native samples separate from diagnostic observer overhead. Matching ASI/PDB files must be preserved before rebuilding. Native ETL sampling requires a Windows Administrator session; sandbox approval alone does not grant that privilege.

## Texture-route cache comparison

`-TextureLinear` / `profile-start-cpu-texturelinear` selects the original linear texture routing scan for a bounded CPU-only capture. Draw forwarding, GPU queries and state/draw/constant observation remain disabled. Ordinary capture, stop or expiry clears it. Compare cache / linear scan / cache in one held view. The resource-owned pointer cache stores only immutable routing mappings, including unchanged ordinary textures; actual device calls and their results remain intact. Resource replacement creates an empty cache; no pointer survives from a prior resource set. Active captures still include per-route timing overhead, so their timings are not completely uninstrumented gameplay.

## Eye visibility comparison

Exact-eye entity rejection is normal opted-in VR, visually accepted on2026-10-05. `-BroadVisibility` / `profile-start-cpu-broadvisibility` selects the original broad sphere acceptance for a bounded CPU-only control; stop/expiry/ordinary capture restores normal behavior. Streaming/grid/interior traversal remain conservative in both modes. Historical `-TightVisibility` captures in the results document predate promotion.

For continuous diagnostics, `visibility-test off` forces broad entity acceptance and `visibility-test on` restores normal exact-eye acceptance. Reset/shutdown restores the normal default. Use `visibility-test on` before matched ordinary/broad/ordinary captures. Neither command changes saved configuration. All-scene performance and episode compatibility remain unverified.

## Ordinary submission rate

`tools/capture-submission-rate.ps1 -Label held-street` measures actual stereo-layer submission counts for30seconds without enabling the in-game profiler. It records RPC timing bounds, ready/FOCUSED endpoint status and profiler epoch. Any detected session/profiler change invalidates the measurement. Focus/pose/traffic between endpoints are not monitored. Use a held tracked view and matched controls; the result has no CPU/GPU phase attribution. Results go to ignored build/profiles.
Texture/target/depth routing uses a marker-verified thread token only within executing eye phases. Resource generation invalidates tokens on recreation/shutdown; other threads and phase gaps keep original identity checks. These fast paths forward all device calls. Active routing phase timing uses stamps; ordinary inactive rendering skips stamp initialization. Do not infer unprofiled performance from active routing-span costs alone.

## DXVK with matching symbols

The shipped GNU DXVK3.1.1 DLL is stripped. An unmodified MSVC build provides PDBs for the D3D9 frontend and backend functions; it changes compiler/code generation, so validate it as a separate profiling baseline. Old GNU trace addresses cannot be resolved with the MSVC PDB. Do not treat a across-build FPS difference as an optimization result.

Prepare ignored local sources/tools, then run the build commands in an **x86 Visual Studio developer PowerShell** with glslangValidator on PATH:

```powershell
git clone --branch v3.1.1 --depth 1 --recursive https://github.com/doitsujin/dxvk.git build/dxvk-013
python -m pip install --target build/dxvk-tooling meson==1.9.1 ninja==1.13.0
$env:PYTHONPATH = Join-Path $PWD 'build/dxvk-tooling'
$env:PATH = (Join-Path $PWD 'build/dxvk-tooling/bin') + ';' + $env:PATH
python -m mesonbuild.mesonmain setup build/dxvk-013-msvc build/dxvk-013 --buildtype=release -Ddebug=false -Denable_dxgi=false -Denable_d3d8=false -Denable_d3d10=false -Denable_d3d11=false
python -m mesonbuild.mesonmain compile -C build/dxvk-013-msvc -j 4
```

Expected source commit is b1a1c99ab52b687cf950d62c88bc2fa316b41663. Existing partial clones need their pinned submodules initialized, including dxbc-spirv's nested SPIR-V headers. This workspace used MSVC19.51.36260 and the existing Vulkan SDK1.3.231.1 glslangValidator. Meson debug=false disables shader-debug generation; the project's MSVC /Z7 and /DEBUG:FULL still produce the native PDB. Verify PE machine014C, Release optimization and the fresh DXVK build log; do not infer bitness from the folder name.

Preserve DLL/PDB hashes and build options together. For this game, deployment replaces the backed-up game-root `vulkan.dll` with the built `src/d3d9/d3d9.dll`, after bridge quit and verified process exit. Preserve d3d9.cfg/ASI INI and the preceding backend, and retain `d3d9.pdb` with its original name in the symbol path. Leave API1, Vulkan bridge settings and gameplay settings unchanged for the comparison. Confirm continuous stereo, fresh graphics and headset behavior before using a new elevated native trace. Roll back the backed-up DLL only after the game exits.

The native capture metadata records installed ASI and vulkan.dll hashes. A file hash identifies the installed file; fresh DXVK logs and ETL image rundown identify the actual loaded backend. Keep all DLL/PDB/ETL/build output out of Git.

## Measurement isolation

Keep Computer Use screenshots and UI inspection outside performance capture windows. Do not run compilation, ETL analysis, large file copies or other development workloads concurrently with a capture. Observe the scene, allow it to settle, start the bounded bridge capture, then leave the machine alone until it finishes. Record any unavoidable concurrent work and exclude contaminated captures from comparisons. A resident helper's cost is not quantified merely by avoiding screenshots; preserve the same background conditions across controls. Keep OpenXR focus/tracking, backend, settings and view comparable, and report any differences rather than attributing them to an optimization.

The capture client writes validComparison and comparisonProblems, then exits with an error after preserving diagnostic data if focus/session/backend/readiness changes, render overflows increase or samples are dropped. This checks endpoints; it cannot prove uninterrupted focus between them. Exclude invalid captures from matched comparisons; old files without the field require manual endpoint validation.


## Address-based native CPU attribution

Native recording metadata `before`/`after` includes module enumeration and WPR setup/stop processing. These counters do **not** bound the requested30-second sample window. Older recordings must not use their counter delta divided by30 as submission rate or for precise per-frame sample normalization. The capture script now records `recordingStart`/`recordingEnd` status-query QPC/UTC bounds after WPR starts and before it stops, plus WPR boundaries and QPC frequency. Match the analyzed ETL interval to those bounds before normalizing; the first30seconds of an ETL can include setup/status-query time. Recording-start/end query latency also bounds counter alignment uncertainty. Endpoint `submittedHz` and separate phase captures can provide context but cannot replace an aligned frame count.

A stripped backend can appear under `***unknown***` in xperf stack tables. Use
image rundown from the same recording to attribute raw instruction samples:

```powershell
xperf -i build/profiles/native/<capture>.etl -o build/profiles/native/<capture>-images.txt -a process -image
build/summarize-cpu-samples.exe build/profiles/native/<capture>.etl <render-thread-id> 30 > build/profiles/native/<capture>-ips.txt
python tools/summarize-native-cpu.py build/profiles/native/<capture>-ips.txt build/profiles/native/<capture>-images.txt <game-pid>
```

Use the verified render thread and process from that recording. The Python tool
checks sample totals, duplicate IPs, lost events, process image coverage and
ambiguous module matches. Kernel ranges come from PID0 rundown. It reports
unmapped addresses explicitly, including executable allocations absent from
module rundown; do not automatically label them as DXVK. Optional
`--range LABEL BEGIN END_EXCLUSIVE` uses relocated runtime addresses, not Ghidra
addresses. Ranges overlap module totals and are not additive savings.

Keep both reports from the same ETL and an interval without DLL unload/reload or
address reuse. The rundown mapper does not perform time-dependent remapping.
Raw sampled-event totals differ from stack-backed hit totals; preserve each
report's denominator. Samples attribute resident CPU instructions, not blocked
wall time, exact stage-specific costs, or guaranteed frame-time savings.

## Sampled call stacks and flame graphs

`tools/export-cpu-stacks.cpp` reads the existing ETL offline. Compile it with the
x64 MSVC host toolchain and `advapi32.lib`; the traced game remains x86. It pairs
kernel Profile events with StackWalk records by original raw QPC timestamp and
thread. A second ETW pass supplies calibrated FILETIME window selection; the
ordered sampled-IP sequence must match between passes. It reports matched,
missing and duplicate stack timestamps and lost events. Kernel/user fragments
are combined; partial or failed native unwinds still produce truncated stacks.

```powershell
build/export-cpu-stacks.exe <trace.etl> <render-thread-id> 30 > build/profiles/native/<label>-folded.txt
python tools/cpu-flamegraph.py build/profiles/native/<label>-folded.txt <same-trace-images.txt> <game-pid> build/profiles/native/<label>-flame.html --ranges <verified-ranges.json>
```

The JSON range list contains `begin`, `end` (exclusive), and `name`, using
Ghidra VAs at base00400000. Validate ranges against live assembly. Other game
frames retain exact analysis addresses; other modules retain exact RVAs. Do not
assign the nearest Ghidra symbol to an address outside its verified body.
Click frames to zoom; search highlights names/addresses. Tables distinguish
inclusive from exclusive samples. Inclusive counts overlap across ancestors;
flame width is sampled CPU work, not frame time or waiting time. Fragmented roots
must not be interpreted as the game's complete call graph. Keep generated
reports/symbol maps local. For latency from waiting/scheduling, also inspect
CPU Usage (Precise)/context switches rather than extrapolating sampled CPU.
