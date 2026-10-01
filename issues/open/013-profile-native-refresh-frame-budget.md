# 013 — Profile and improve the native-refresh frame budget

## Goal

Explain and reduce the remaining scene-dependent frame-time cost now that #006 lets OpenXR pace stereo and removes desktop VSync. Target the configured 90 Hz headset with measured evidence. Roadmap stage 5; coordinate camera/auxiliary validation with #011.

## Tasks

- [ ] Add bounded timing attribution for game update/list building, per-eye D3D9 rendering/fence, D3D11 transfer, xrWaitFrame and desktop presentation. Separate intentional runtime wait from useful work and stalls.
- [ ] Measure a fixed apartment/street/vehicle route in Release. Record median/p95/p99 frame time, missed submissions and streaming spikes with graphics settings and headset/runtime versions.
- [ ] Determine whether observed drops come from CPU work, GPU work, interop synchronization, streaming, a safety limiter or runtime throttling; do not infer a cause from submittedHz alone.
- [ ] Implement a scoped improvement supported by that profile. Preserve one simulation update and correct synchronization; do not remove game loading/cutscene/minigame safety limits blindly.
- [ ] Repeat the route and flat-screen regression. If the hardware/settings cannot meet 11.11 ms, document the measured limit and usable settings without claiming all-scene native FPS.

## Done when

The measured bottleneck is explained, an evidence-supported improvement is verified, and the repeatable route has a published frame-time distribution and native-refresh coverage or explicit hardware/settings limitations.

## Notes / evidence

- The user reports neither CPU cores nor GPU utilization are close to 100% during the outdoor slowdown. These are user-observed utilization readings, not a captured timing trace. Include synchronization stalls, CPU thread dependencies, streaming and runtime throttling in the investigation; low utilization alone does not distinguish them.
- The user observes that the outdoor drop is likely game performance rather than a cap. This is consistent with scene-dependent rates after IMMEDIATE presentation and gameplay-limiter bypass, but CPU/GPU attribution is not yet measured. Prioritize profiling the route rather than increasing an arbitrary limiter.
- #006 final Release ASI SHA256 `CED41E3039DC0F4DEF56ECAAB8EED580DFD5C135B803FE749FB2BE04561009F1`, GTA IV CE 1.2.0.59, SteamVR/OpenXR 2.17.10, RTX 4070 Ti, system D3D9Ex → D3D11, 2560×1440 per eye. Actual desktop presentation is IMMEDIATE, and xrWaitFrame supplies pacing. Two five-second samples in PID 31412 measured 90.00 layers/sec; after reboot PID 16664 sampled 89.80 and later 49.80 layers/sec while FOCUSED. These are coarse projection-layer rates, not unique compositor frames or percentile/GPU timings. The later drop's cause is unmeasured.
- The user's 120 FPS FusionFix menu preset remains saved for ordinary gameplay; active rendered stereo bypasses that limiter. Existing apartment-exit stutter was reported before the stereo/pacing change.
