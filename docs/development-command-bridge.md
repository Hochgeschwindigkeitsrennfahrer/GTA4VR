# Local game command bridge

`profile-start`, `profile-start-cpu`, `profile-start-cpu-noflush`, `profile-start-cpu-noao`, `profile-stop`, `profile-status` and `profile-metric <name>` control/read a bounded 30-second in-memory timing capture. The CPU-only start skips D3D9 GPU timestamp queries to measure observer overhead. The no-flush start temporarily changes D3D11 completion polling after explicit Flush while retaining the completion barrier and timeout. The no-AO start temporarily bypasses FusionFix ambient occlusion for effect isolation; stop, expiry or an ordinary start restores the saved behavior without writing settings. It is a quality-changing diagnostic, not an equivalent-quality performance comparison. See [profiling procedure and metric definitions](frame-profiling.md) and `tools/capture-frame-profile.ps1` for apartment/street comparisons. Profiling is inactive until explicitly started through the opt-in development bridge; these commands do not write saves or inject gameplay input.

`aim-status` reports the opt-in `ExperimentalHeadsetAim` installation, fresh headset pitch/yaw, native weapon-basis override and camera-update counts, and lock-on suppression. Counters establish callback coverage, not shot accuracy. The current prototype targets on-foot `CCamAimWeapon`; sniper/FPS scope cameras remain unverified. The aim basis uses a fresh focused headset sample (250ms bound), local-player ownership, first-person state and pause/vehicle/action gates. Lock-on suppression preserves the prior native bit and restores it on the next simulation callback when this path becomes inactive. No game-input or arbitrary-execution command is added.

Set `[DEVELOPMENT] EnableCommandBridge=1` in the installed FusionFix INI for development tests. The distributed default is `0`. A running game exposes `\\.\pipe\GTA4VR.Dev.<process-id>`. The pipe rejects remote clients and grants access only to the current Windows user and local System. No TCP port, arbitrary script, memory-write command, or game-input command is exposed.

## Restore a profiling location

The user authorized position restoration for repeatable testing. `location-capture` queues a native position/heading read; `location-restore <x> <y> <z> <heading>` queues a bounded native teleport. Both run on the simulation callback, not the EndScene IPC callback. `location-status` reports the operation ID, pending/completed state, validity, error and native position readback. Acceptance only means queued; inspect completed readback before measuring. Only unpaused free roam with a living, grounded player on foot is supported; missions, active cutscenes and vehicles are rejected. Coordinates must be finite, XY within ±8192, Z within −1024…4096 and heading within ±360 degrees. This is a fixed operation, with no arbitrary native calls or memory writes exposed.

```powershell
.\tools\game-location.ps1 Capture -Path build/profiles/street-location.json
# After relaunch has loaded free roam:
.\tools\game-location.ps1 Restore -Path build/profiles/street-location.json
```

The helper checks the completed operation ID and native XYZ readback (within0.1 game units), and uses invariant decimal formatting. Restoration requests native collision streaming, uses `SET_CHAR_COORDINATES_NO_OFFSET` plus `SET_CHAR_HEADING`, and recenters VR. Let streaming settle and inspect a fresh image before a comparison. Restored position is not a restored world snapshot: traffic, time/weather, tasks, camera/tracking alignment and resource residency can differ. The file remains in ignored build output; player saves are not changed. Use captures from the same game episode/build only.

From the repository's PowerShell prompt:

```powershell
.\tools\game-command.ps1 status
.\tools\game-command.ps1 quit
.\tools\game-command.ps1 stereo-test
.\tools\game-command.ps1 lod-probe
.\tools\game-command.ps1 recenter
.\tools\game-command.ps1 input-status
.\tools\game-command.ps1 camera-probe
.\tools\game-command.ps1 movement-status
.\tools\game-command.ps1 aim-status
# Specify the process if discovery is ambiguous:
.\tools\game-command.ps1 status -GameProcessId 12345
```

`status` reports protocol version, game PID, and OpenXR enabled/running/failure/session/frame state. `quit` releases OpenXR on the EndScene callback thread and posts `WM_CLOSE` to the game-owned window, requesting its usual close path. The response acknowledges command acceptance; confirm actual exit from the process list. Neither command saves the game or modifies player saves.

Issue 006 adds stereo readiness, built-frame/budget, continuous mode and submitted-layer counts to `status`. Set `[VR] ExperimentalStereoFrames=0` for continuous development rendering; this is the user's preferred mode for headset checks. Values 1–600 retain the bounded diagnostic mode and hold the last pair afterward. `stereo-test` resets the counter and requests a fresh capture on the next game-process callback when resources and valid OpenXR views are ready. It does not repeat simulation, launch another process, save or inject input. Check status/logs and actual images; command acceptance alone is not rendering evidence.

For #014 visibility investigation, `[DEVELOPMENT] ProbeFirstPersonAnchor=1` also installs the native entity/LOD observer. `lod-probe` arms or restarts it on the game-process callback, without restarting the game or stereo frame counter. It records at most 480 nearby entity decisions from enclosing CE AEBDF0, including model classes that bypass AEBFF0 (radius 150 game units, up to 48 distinct entities every 120 simulation frames) in `GTAIV.VR.stereo.log`; original LOD, rejection and occlusion outputs are preserved. It also records up to 32 native request-frustum rejections whose bounds intersect either actual eye frustum, preserving the request decision. The observer starts disarmed. Acceptance only means the request was queued; verify fresh records. The same request also arms up to 480 direct outdoor phase-rejection records through AE77F0/AE8680; these log the candidate/rejection masks and distinguish entity rejection from occlusion. AEBDF0 is used by interior traversal and does not cover the outdoor batching path by itself. Each entity record includes real root distance and loaded drawable pointer; some native branches do not write their distance output. Result codes need native branch context, and absence from the log can mean an entity never reached this decision function. This command does not reposition the player or modify saves.

`openxr.displayRefreshHz` is the current hardware rate returned by the optional OpenXR display-refresh extension (zero if unavailable). `openxr.submittedHz` is a five-second projection-layer submission average; it does not count compositor-generated frames or prove the headset displayed every submitted frame. `[VR] MatchHeadsetRefresh=1` skips the ordinary gameplay limiter while OpenXR requests rendered eye images, letting `xrWaitFrame` supply timing. Loading/cutscene/minigame safety limits still apply, and ordinary gameplay caps resume when VR stops or fails. This pacing works without the optional refresh-rate query.

If a restricted command sandbox denies pipe access, run the same client in the normal Windows user context through the approved tool path. Keep the pipe ACL restricted; do not broaden it to work around the sandbox.

## Protocol and ownership

Issue 014 adds `recenter`: it requests a new headset tracking origin atomically; the world list builder applies it on the next valid eye sample. It does not move the character or write a save. Command acceptance alone does not prove that a valid pose has arrived or that recentering is visually correct.

`camera-probe` restarts the bounded read-only anchor/motion observations only when `[DEVELOPMENT] ProbeFirstPersonAnchor=1` was enabled at launch. The world list builder consumes the atomic request, resets diagnostic counters and starts a fresh `FusionFix.vr-camera.log` on its first sample. Preserve the previous log before requesting another capture. `FirstPersonProbeSamples` is clamped to 1–240; observation stops after that sample count. It does not enable an uninstalled hook, move the character, change input or write saves.

`movement-status` reports the opt-in `[VR] ExperimentalHeadRelativeMovement=1` hook installation, fresh focused first-person heading, query/override counts and last original/applied angles in radians. It changes the existing on-foot keyboard/gamepad direction reference, not input magnitude or physics position. It requires `ExperimentalFirstPerson=1`; invalid/stale poses, lost focus and fallback camera states use the original angle. A nearly vertical gaze retains the last horizontal heading. Counters establish hook coverage, not correct walking direction; verify forward/back/strafe in the headset. Vive Wand input remains #015 and room-scale body translation remains separate unfinished #014 work.

Its `roomScale` object reports `[VR] ExperimentalRoomScale=1` configuration and signature-checked direct position/query/synchronization availability. `motorCalls` now counts direct-displacement updates (the legacy field name remains for protocol compatibility); the ordinary walking motor is unchanged. `pending` is zero: no catch-up queue is used. `consumed` is the diagnostic total of fresh physical displacement requests and `achieved` is the diagnostic total of applied root displacement. Neither total controls the camera. `body` is the current root XYZ and `view` is the generated eye midpoint XYZ: their horizontal difference is the fresh head movement not yet processed by the next simulation update. The processed component stays centered at the resolved root; no accumulated attribution estimate is involved. Fresh horizontal tracking deltas are resolved by a conservative standing-torso cover of native swept spheres, then applied through the low-level position setter and physics matrix/broadphase synchronization. Gaps over 100 ms, owner changes and tracking jumps over 0.3 units rebase the next request. The camera uses the current resolved root and eye pose, removing the absolute processed tracking translation from its base. Entity writes run on the pre-game-process simulation callback; camera preparation only publishes tracking and renders the newest unprocessed delta. No estimated movement share, room velocity injection or body acceleration servo remains. The cover does not yet implement full composite collision, wall sliding, penetration recovery or crouched bounds. Ordinary physics owns floor/support and vertical body travel. `groundZ` and `floorEyeHeight` report the native ground and STAGE eye-height samples (zero if unavailable). Opt-in physical crouching uses configured PlayerHeight with65%/75% thresholds and a native-cancellation latch. See [collision analysis](room-scale-collision.md).

`input-status` reports the optional Vive Wand action observation (`[VR] ExperimentalControllerInput=1`): per-hand action activity, valid aim pose, trackpad coordinates and click. Stale samples older than 250 ms become inactive. This command makes no game input changes; controller locomotion belongs to #015.

The server uses one overlapped message-mode pipe and bounded buffers, polled without blocking from EndScene. Commands are UTF-8 messages of at most 128 bytes. The response is one JSON message; the client accepts up to 1024 bytes to accommodate the movement diagnostics and rejects larger responses. The client reads the response, sends the three-byte message `ack`, and disconnects. The server executes the quit side effect only after the acknowledgement; a stalled connection is canceled after five seconds. Unknown commands return an error. A paused game can respond as long as it continues rendering; startup/loading or a frozen render thread can make the client time out.

The bridge and OpenXR do not create a control worker thread. Explicit OpenXR shutdown occurs after the current zero-layer frame has completed, before the normal window-close request. DLL detach only closes IPC/log handles; it does not call OpenXR or unload its loader under the Windows loader lock.

Track live test evidence in [issue 009](../issues/closed/009-local-development-command-bridge.md) and [issue 005](../issues/closed/005-openxr-initialization.md). Compiled binaries, installed configuration, and runtime logs remain outside Git.

For outdoor LOD attribution, the same request also arms a maximum480 AE7990 batch records (up to128 distinct roots within150 units per sampled frame). The observer captures entries before native filtering, then logs whether they were cleared, the resulting distance, loaded drawable, LOD parent, cutoff inputs and alpha. Original calls/outputs remain intact. A cleared entry explains why later rejection probes miss it; it does not identify the cause without the native branch context. tools/read-visibility-candidates.ps1 -GameProcessId <pid> -X <x> -Y <y> -Z <z> reads the surviving native candidate array without game writes or suspension. This external snapshot is non-atomic and can omit entities whose root is outside its chosen radius.
`-FineGrid` walks bounded nearby native cell lists even when the transient candidate array is empty. Add `-Queues -QueuePhase <0..31>` to read final native queue membership (obtain the current main phase index from the live stereo trace). Records include draw distance, parent/child LOD masks and type flags. Reads occur while the game runs, so absence in one non-atomic snapshot is not definitive rejection evidence. Entity +3C stamps are shared by multiple engine traversals; a changed stamp alone does not prove render traversal.

Camera-state diagnostics in `movement-status` include `firstPerson`, `vehicle`, `ducking`, `airborne`, `animated`, `ragdoll`, `vehicleExiting` and `physicalCrouchInhibited`. Animated/ragdoll selection comes from a fresh simulation-owned player snapshot; it suspends room-scale body writes and ordinary walking-heading overrides. The inhibition flag stays set after native duck cancellation until floor-relative headset height reaches75% of configured player height. These flags prove path selection, not a correct headset image or action-transition acceptance. During vehicles/actions, `roomScale.body` describes the chosen camera anchor rather than a measured physics-root position; do not treat body/view separation there as a room-scale alignment measurement.

`profile-constants` reads the bounded shader-constant observer's capture epoch and per-eye vertex/pixel counts. It changes no rendering behavior. See the profiling procedure for repeated-value candidate limits and the nested device-call timings.
`profile-start-cpu-constants` selects CPU-only capture with the detailed constant observer; ordinary starts disable it. `profile-status.constantObserverSelected` identifies the selected mode. Rendering behavior is unchanged.

`profile-start-cpu-draws` selects the sparse draw observer with GPU queries and detailed constant observation off. `profile-status.drawObserverSelected` records selection. `profile-draws <eye 0-1> <stage 1-3>` pages per-phase counts and sampled original device-call wall time within the existing1024-byte client limit. Invalid eye/stage/trailing input is rejected. See `docs/frame-profiling.md` for sampling and coverage limits; no draw/state mutation is exposed.

Sparse state profiling (#013): `profile-start-cpu-states` starts a30-second CPU-only observer. `profile-states <eye 0-1> <kind 0-9>` returns bounded per-category count/sample totals (texture, render state, sampler state, vertex/pixel shader, vertex/pixel float constants, stream source, indices, vertex declaration). `profile-stop` or an ordinary start disables observation; no game settings are written. The capture client exposes `-States` and validates completed epochs for all20 pages. See docs/frame-profiling.md for coverage and limits.
`visibility-test off` forces broad world-entity sphere acceptance for diagnostics; `visibility-test on` restores normal exact-eye acceptance (visually accepted2026-10-05). Enabling requires ready stereo. Reset/shutdown restores the normal default. Neither command changes saved configuration or streaming/grid/interior traversal. Use the normal path before matched ordinary/broad/ordinary performance captures.

`sphere-classification-probe` compares the native world-AABB sphere classification with the instruction-derived scalar implementation for 120 completed stereo builds. It logs query/mismatch counts to the local stereo log; it changes no decisions. `sphere-classification-test on/off` selects the experimental replacement or legacy native calls/copy at the next scene setup. The replacement preserves the accepted eye-union rejection and near-distance output; unknown viewports and invalid inputs retain native calls. Default and reset/shutdown are off. Run the comparison first and exclude its extra work from timing windows. These commands do not modify settings or saves.

`vertex-caller-probe` requests a five-second native immediate-vertex caller histogram. The next left-eye G-buffer marker installs a uniquely matched mid hook on the render thread; a later marker removes it when the deadline expires, and shutdown also removes it. Ordinary rendering has no vertex-probe hook. Counts go to the local `GTAIV.VR.stereo.log`, with module base, thread, six completed-phase stream starts and dropped entries. Each row records the actual return address and counts in eye0 G-buffer/lighting/DrawScene, eye1 G-buffer/lighting/DrawScene, and outside marked streams. The table holds at most512 caller addresses; foreign threads are ignored. Counts include entry attempts, including the native disabled/null-output path. Validate a return address's preceding instruction before assigning a producer. The probe changes no vertices or settings, but its hook adds diagnostic overhead: exclude it from performance captures. Command acceptance is not completed-probe evidence; check the armed/completion records and ensure no entries were dropped.

`vertex-emission-test on/off` selects an experimental replacement for the native nine-word immediate vertex writer at the next left-eye G-buffer marker. It preserves the enabled/null-output count-only branches,36-byte stride, packed color and float bits; it changes no draw ordering or settings. The unique signature and contiguous state layout must match. Default and shutdown/reset are off; verify the local log or live entry detour before measuring. Disable it before `vertex-caller-probe`. The initial off/on/off Release comparison showed **no gain**, so it remains a diagnostic control, not a recommended optimization.

`model-command-probe` requests a five-second observer of CE's native model callback008DCBF0. It attaches at the next left-eye G-buffer execution marker, counts callback entries and inclusive QPC wall ticks by model ID/hash for each of the six eye/pass streams, and detaches at the next left-eye marker after its deadline (or shutdown). A fixed4096-row table bounds storage; foreign threads and unmarked/auxiliary passes are ignored. No command/model/visibility changes are made. Counts include early-return callbacks, not actual D3D draws; timing includes native descendants, routing and detour overhead and is not GPU cost. Keep other native probes and the frame profiler off during collection; exclude these runs from ordinary performance comparisons. Check armed/completion records, zero dropped rows and equal nonzero stream counts before comparing per-stream means; shutdown may interrupt a partial stream. Logs are local and stay out of Git.

The bounded `model-command-probe` also attaches a native material-pass binder
observer at004373E0 when its unique signature matches. It forwards the verified
thiscall/four-stack-argument ABI and counts per-phase entries, inclusive QPC
wall ticks and declared render-state/vertex/pixel parameter entries. These are
metadata traversal counts, not actual API updates (some are skipped/cached).
The material hook detaches before the model hook is removed and before logging.
Its inclusive time is nested in model/eye time; do not add them. An unavailable
signature logs that the binder observer did not attach. Ordinary rendering has
no binder probe hook; keep this diagnostic excluded from frame benchmarks.

The same bounded probe brackets the verified native corona batch draw at
CE `00C1F940`/`00C1F945` when its unique signature matches. It captures the first
actual `DrawPrimitive` in each marked phase using D3D9 getters: vertex declaration,
stream stride/offset, vertex/pixel shader disassembly, float constants and depth,
blend and cull states. It does not lock/read vertex buffers or alter device state.
Capture validity and absent phases are logged explicitly; one snapshot does not
establish coverage of every shader variant. Disassembly and logging happen after
the native hooks detach. All shader dumps remain local and must stay out of Git.
This diagnostic temporarily keeps draw hooks installed; ordinary rendering
removes them again at the next left-eye marker unless another diagnostic needs them.

The same probe also records the first native compact corona record and its six
expanded vertices in each phase, together with the native camera basis and mode
flags. These are bounded CPU reads, not GPU readback. Treat geometry dumps as
local game data and keep them out of Git.

`billboard-instancing-test on/off` selects the #013 experimental GPU expansion
of native corona records. Default/reset/shutdown are off; no INI is changed.
It only replaces verified normal-mode main-eye batches. Unsupported shaders,
signatures, mode flags or allocations retain the native path. It preserves the
native selected-record order and original shader body, uploads compact32-byte
instances, and restores D3D9 bindings and stream frequencies after drawing.
Disable other native probes first. Command acceptance means requested, not
executed: check the adaptation log and the `off batches/lights/fallbacks` counters
in `GTAIV.VR.stereo.log`. Exclude shader compilation/warmup from captures. This
is not an accepted default optimization; near-light variants, reset and broader
graphics compatibility remain pending. Keep ordinary/off as the control.

`rigid-instancing-test on/off` selects the #013 mesh compatibility stage. It
replaces eligible main-eye G-buffer indexed triangle draws with **one instance
per original draw**, retaining native order and material/pixel-shader state.
It does not batch meshes or skip model callbacks. Only the verified rigid
four-input and tangent declarations are accepted; other layouts, extra streams,
existing instancing and unsupported shader variants use the original draw.
World/clip matrices become instance attributes; original vertex shader,
declaration, stream1 and stream frequencies are restored after each replacement.
Default/reset/shutdown are off, and no configuration file is changed. Disable
native observers before enabling it. Acceptance is only a queued request; read
the adaptation records and `off draws/fallbacks/failures/restorationFailed`
counters in the stereo log. This mode deliberately reads device state on each
draw and can be slower. Exclude it from performance comparisons; reset and
broader moving-headset/material compatibility remain pending.

`mesh-packet-probe` requests a single eye0 G-buffer diagnostic capture and an
isolated comparison using native mesh buffers. Native gameplay draws still run.
Capture is bounded to2048 inspected draws,1024 state groups,128 instances per
group,64 shader/declaration variants and1500ms of sampling. Only supported rigid
declarations with static vertex/index buffers and ordinary stream frequencies
are considered. State grouping compares actual shader-read float constants
(excluding per-instance VS matrices and inline `def` values), all int/bool
registers, texture/sampler bindings, render states, streams, clip planes,
viewport/scissor and targets. It owns state-block references until completion.
The largest repeated group is replayed separately and instanced into temporary
four-target/depth surfaces, then original targets and complete state are restored.
Read `mesh-packet-probe ... valid/restored/differingBytes/nonzeroReferenceBytes`
in the stereo log; accepted means queued only. Zero differences with empty output
does not validate visible geometry. This excludes other scene geometry, active
query behavior and mutable texture contents; it is NOT permission to reorder
the scene. Disable other native/replacement experiments first. Expect a brief
diagnostic stall; never use this capture for performance numbers. Reset/shutdown
cancel it, and no geometry or native shader assets are written to Git.

`mesh-order-probe` extends the bounds to8192 inspected draws,4096 groups,
128 instances/group,128 shader variants and4000ms, and compares the captured
eligible subset in original draw order against a replay that emits each group
at its last occurrence. Instance order within each group is retained. It rejects
packets spanning different target/depth sets and logs reference/grouped draw
counts and differing color bytes across all four scratch targets. It also
compares normalized D24 depth using a scratch-only INTZ texture sampled into
R32F; creation failure invalidates the comparison. The live depth format is
untouched. Coverage counts distinguish observed, inspected and captured draws.
Stencil is read through eight EQUAL bit tests, with KEEP operations, zero stencil
write mask and depth writes disabled, accumulating bit weights in R32F after
checking float-blend support. Scratch stencil is compared separately. Skipped
draws and query/resource side effects are not covered; even nonempty identical
color/depth/stencil does not justify live scene reordering. This command is
scratch-only and should be excluded from all timing captures.

`mesh-replay-probe` raises only the group limit to5120 and attempts to retain every indexed
draw, including unsupported/dynamic geometry as ordinary native draws and
ordering barriers. Blended, non-depth-writing and unsupported depth-test draws
prevent grouping across their position. Stencil is restricted to disabled or
ALWAYS/KEEP/KEEP/(KEEP or REPLACE), without two-sided operations; any stencil
signature change (including reference and masks) is also an ordering barrier.
Other stencil operations/tests are ordinary native draws. Query Issue and both UP draw observers
must install; any observed query event cancels the comparison. Non-indexed/UP
draws are counted but not replayed and invalidate complete native comparison.
When every observed indexed draw is captured without failures/drops, it also
compares the original-order scratch colors and stencil against the actual native
G-buffer/depth-stencil. Stencil uses the same read-only bit tests. A GPU snapshot of the stencil
present before the first native indexed draw initializes both scratch replays;
the native stencil is never cleared or written. When the native depth surface
is an INTZ texture, its normalized depth is sampled read-only into R32F and
compared separately (`nativeDepthCompared`/`nativeDepthDifferences`). Other depth
formats leave that comparison unverified. No live format change or depth writes. Read `nativeStencilCompared` and
`nativeStencilDifferences` separately as well. A one-shot CE material binder
observer reports `mesh-recipe-probe`: material+shader+geometry key counts,
missing material identities, and keys with more than one complete observed
non-transform state. These are cache candidates, not permission to skip native
binding or cleanup. The observer is detached when the probe finishes.
Read `nativeCompared` separately from `valid`: a valid partial scratch result
is not native parity. State blocks retain references, not immutable copies of
mutable buffer/texture contents. Even frame-local native color/stencil equality
and a successful native depth comparison do not validate resource side effects,
other eye views or production performance. Normal gameplay draws remain intact.

`constant-batch-test on/off` gates an experimental native vertex-parameter binder
coalescer. It copies float writes, merges adjacent/overlapping ranges, flushes
before texture/resource callbacks, draws and binder return, and checks the first
64 nonempty scopes by readback. Default/reset/shutdown are off. The measured
off/on/off comparison was slower with it enabled, so leave it off for normal
development and performance measurements. Reduced call counts alone are not
evidence of a speedup.

The model probe's mesh-binding observer records only the first eye0 G-buffer
stream, grouping actual indexed draws by model context, material-pass pointer,
VS/PS/declaration/VB/IB, stream0 offset/stride and every draw argument. Getter
failures and table overflow are counted. Repeated keys are batching candidates;
they do not establish matching textures, other streams, constants, animation,
alpha behavior or safe reordering. No draws are changed. Exclude its getter and
hash overhead from benchmarks; the five-second probe detaches before logging.
The first eight known-model groups reaching32 matching draws also get bounded state
comparison within that same frame: float VS/PS registers (changed lane masks),
texture slots0-15 and their first VS/PS disassembly. Only those groups are sampled;
the first31 instances are omitted from the variance comparison. Other constants
may contain stale values unused by this shader: inspect actual shader reads and
`def` overrides before classifying them as per-instance dependencies. Render
depth/blend/stencil/color-write/scissor/clip states, sampler0, all16 vertex streams
and frequencies, buffer usage, declaration, render/depth targets, viewport and
scissor are also compared. Other samplers, int/bool registers and query lifetime
remain unverified. Indexed runs and model runs are diagnostic ordering counts,
not legal batching counts. The bounded probe observes this backend's query Issue
implementation and forwards every call; non-indexed/UP draws and query Issue calls
break runs. If a hook is unavailable, zero counters do not prove no barriers.
All temporary query/UP hooks detach with the observer. All native shader
output stays local outside Git; this is not a renderer compatibility proof.

## Captured mesh batching payoff test

`mesh-benchmark-probe` performs the bounded full indexed capture/parity check,
then warms ordinary/grouped emitters and takes four samples of each in ABBA
order. Both timed paths bind each group once in the same grouped order; ordinary
draws submit its instances individually, grouped draws use the tested instanced
shader. CPU submission includes shared state-block/scratch-target binding plus
emission, reported separately as `stateApplyMs` and `emitterMs`. GPU
timestamps bracket emission; samples are drained with bounded query waits. GPU
intervals can include idle gaps from CPU/driver submission and are not shader
occupancy measurements. Capture, native model/material preparation, shader
compilation, clears, initial-stencil restoration and staging readback are outside
the timed section. It briefly stalls one frame and is excluded from ordinary
frame-profile captures.

Accept `valid=1` only with complete native color/depth/stencil parity, no capture
drops/failures/query/other draws, successful restoration, and four nondisjoint
samples per mode. These isolate the current emitter's potential, not gameplay
FPS or the cost of a future live collection path. No native callbacks are skipped
in gameplay. The one-shot native material observer and queries are released
afterward.

The prepared benchmark emitter reuses the warm parity pass's stream/matrix
validation for those exact owned packets and leaves singleton groups native.
This shortcut is confined to timed scratch iterations; gameplay callers retain
live stream/matrix validation. A positive submission delta is potential only;
live packet preparation and native callbacks still need their own measurement.

## Bounded live mesh replacement (#013)

`mesh-native-probe` times one ordinary eye pair's G-buffer callbacks. `mesh-live-probe` replaces eligible indexed submissions for one eye pair, then automatically returns to ordinary rendering; headset rendering itself remains continuous. Both commands acknowledge scheduling, not completion. Read fresh `mesh-live-probe` log lines for each eye.

The expanded bounded collector uses the previous frame-local parity experiment's last-occurrence grouped order, including nonconsecutive identical geometry/non-transform state. This reordering remains experimental: equal-depth overlap and other scenes need validation. Static triangle buffers and supported rigid shaders only; dynamic/render-target textures, unsupported draws and state fallbacks stay native. Native material callbacks and pass cleanup run normally. The native binder and state-only cleanup execute normally without flushing; owned packets restore their state at submission. Pending submissions flush before VB/IB/texture locks, texture/surface uploads, mip generation, color fill, queries, UP/non-indexed draws, clear, stretch, attachment changes and pass exit. Resource-write hooks must all attach successfully. Cross-thread resource mutation is not established safe by this bounded test. No scratch image or frozen-frame substitution is used. The command is bounded to one eye pair and stays disabled during ordinary gameplay.

`gbufferCpuMs` includes native callbacks, full packet collection, state capture/restoration and actual draw submission. `queued-emitted` measures native submissions actually avoided; `batches` counts successful instanced submissions. Collector/shader warmup and scene variation must be distinguished from steady performance. Full state capture is expensive; these are bounded feasibility commands, not a production optimization or FPS claim. Any submission/restoration failure invalidates the measurement.

## CPU-work omission upper bound (#013)

Use `cpu-omit-test materials` to bypass native00444C50 geometry/material-loop callbacks, or `cpu-omit-test models` to bypass broader008DCBF0 model-render callbacks, during both main eyes' GBuffer/DrawScene phases. `cpu-omit-status` confirms installed mode, skipped callback counts and remaining time. `cpu-omit-test off` restores the original calls; a90-second timeout also restores them at the owning render callback. The distributed command bridge remains disabled by default.

This intentionally omits descendant draws and produces incomplete images. It does not emulate shader execution, GPU uploads or GPU computation cost, and does not prove all skipped CPU work is shader-replaceable. Measure it as an optimistic upper bound: normal/omitted/normal frame-profile and independent stereo-layer-count windows in a held scene. Keep packet collectors, native observers and other experiments off. Normal game simulation/list building/command release and auxiliary rendering remain; no save operations. Restore mode0 after testing and verify a fresh normal image.

Additional CPU omission modes: `cpu-omit-test gbuffer` (mode3), `scene` (mode4), `lighting` (mode5), and `eyes` (mode6, all three). These remove selected main-eye render-list construction and hence native execution, retaining boundary markers, auxiliary phase lists, simulation and OpenXR submission. Images may be black/stale/invalid. They measure phase-removal upper bounds, not shader feasibility. Same90s timeout; verify active mode/counters, measure profiler-off layer deltas, and explicitly use `off` afterward.

`producer-test on` temporarily pauses OpenXR transfer/submission for15s and lets native eye rendering run without runtime pacing. Uses an explicitly cached pose; the headset image is held/not updated. `producer-test-status` counts uniquely published CPU-executed eye command pairs, NOT GPU completions or headset submissions; `gpuCompletionVerified=false`. Ordinary gameplay limiter bypass continues, desktop presentation and simulation remain. `producer-test off` resumes XR; timeout/focus loss/runtime reset also clear it. Vulkan stereo only. Use short counter deltas with active mode/deadline and unchanged cpu-omit mode at both endpoints; no manual headset graphics test in this mode. This probes maximum producer throughput before implementing buffered asynchronous submission.
