# Performance work checkpoint — 2026-10-05

Issue #013 is shelved at the user's request. It remains open: no all-scene 90 FPS acceptance or production mesh-batching gain is claimed. Resume only when performance work is requested again. Preserve all authored shaders, probes, DXVK patches and measurement tools.

## Working installation left untouched

- GTA IV Complete Edition 1.2.0.59, x86 ASI, SteamVR 2.17.10, VIVE Pro, 90 Hz.
- Installed backend: DXVK v3.1.1, `d3d9.cfg` API=1, opt-in Vulkan/OpenXR bridge. Original GNU backend SHA256 `265888C31CA78DFFA290C39CB7E50BFB02762590E41927906E46FB32F01497FA`; rejected DXVK optimization patches are not installed.
- Installed ASI SHA256 `5C786C91BBFB49CC82659FC13D4CEC624288CEDFC3633DB730DA7B9CDF3AFABA`. Continuous stereo, accepted first-person/room-scale/culling/shadow work and the right-eye mip-filter correction remain intact. Separate grass mod remains installed.
- Read-only bridge checks at shelving: stereo ready, no runtime failure, zero command-buffer overflows; CPU omission mode0, producer test inactive, profiler inactive. Session VISIBLE during these checks, so they are not a focused performance benchmark.
- No game restart, deployment, config write, mod removal or save modification for this checkpoint. The source cleanup Release build passes; no redundant gameplay/performance capture was run.

## Repository paths

Distributed VR defaults remain opt-in; system D3D9Ex/D3D11 fallback remains available. The local working DXVK configuration is a separate installed configuration, not the distributed default.

Ordinary source execution retains native model/material callbacks. Mesh collection/replay, live mesh replacement, CPU omission, uncapped producer mode, billboard/rigid instancing and constant-batching experiments start disabled and require development commands. No async submission worker or display-refresh override is installed or retained in source. The bounded profiling implementation starts inactive.

Shaders and experiments are preserved:

- `source/includes/vrbillboardshader.hpp`: native light-billboard shader adaptation/GPU expansion; live graphics check passed, not promoted as a general performance solution.
- `source/includes/vrrigidinstanceshader.hpp` and `vrrigidinstance.hpp`: authored instancing prefixes and emitter, including supported declaration variants.
- `source/includes/vrmeshpacketprobe.hpp`: packet compatibility, scratch parity, grouping, benchmarks and bounded live replacement. Full StateBlock collection/replay was much slower; leave off.
- `tools/bridge-probe/`: authored billboard and instancing fixtures, Vulkan discovery/copy probes and test helpers.
- `patches/dxvk/`: deferred-index/device-lock experiments and guard move correction, with results and provenance. Rejected options stay off; keep patches for future analysis.
- `tools/capture-*.ps1`, native CPU export/summarization/flame-graph tools and focused tests remain available.

## Main findings and next useful work

See [full evidence](profiling-013-results.md), [profiling instructions](frame-profiling.md), [bridge commands](development-command-bridge.md), and [Vulkan integration](vulkan-openxr-feasibility.md).

- Native G-buffer and DrawScene execution/preparation are substantial costs in dense views. Omission tests remove CPU work AND descendant draws; they establish optimistic payoff bounds, not shader feasibility.
- Held city view: ordinary submissions about41–42/sec; material-loop omission about66/sec, G-buffer omission67/sec, DrawScene omission59/sec, all three eye passes omitted89/sec. Nearly90 is runtime cadence, not11.1ms of CPU computation.
- Uncapped five-second producer test: normal41.02 XR submissions/sec versus41.86 CPU-executed eye command pairs/sec with XR paused. No meaningful normal-scene gain demonstrated from removing the entire synchronous XR path. Material omission64.95 versus79.24 suggests synchronization may matter after native render cost is reduced. GPU completion was not measured by this diagnostic counter.
- Actual live mesh batching reduced indexed calls roughly40–41%, but native preparation still ran and collection/state replay overwhelmed emission savings. Earlier37–47% figures describe draw-count reduction, not model-callback or FPS improvement.
- Next performance work should target substantial native preparation/submission in both expensive passes using lightweight records or verified reuse. Avoid more generic StateBlock replay, small-cost tweaks, or async architecture work without evidence of useful payoff.
- A synchronous OpenXR loop is ordinary. Current bridge limitations are extra copies/CPU completion waits, next-frame pose scheduling and broader latency/lifecycle validation; retained inactive D3D11 fallback is not measured DXVK overhead.

## Preserved local evidence outside Git

`build/profiles/` holds JSON measurements, saved locations, CPU ETLs/metadata, native listings and flame graphs. `build/issue013-mesh-order-backup/` holds prior ASIs. Other DXVK builds/symbols/backups remain under ignored `build/`. These machine-local artifacts are retained but not committed; the tracked reports include filenames, hashes, results and limitations. Ghidra findings are saved in the external GTAIV project and described in the reports; its database/game binary are not in Git.

The downloaded grass-mod ZIP remains in the project root and is ignored. Commit source, authored shaders, documentation, public dependency gitlinks and tools only.
