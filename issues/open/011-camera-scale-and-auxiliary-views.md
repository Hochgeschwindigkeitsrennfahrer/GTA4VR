# 011 — Validate camera scale, pose timing and auxiliary views

## Goal

Move the first stereo world image from #006 toward roadmap stage 3: measured camera behavior and correct auxiliary views throughout a repeatable route. Depends on #006. UI composition remains #007 and the isolated right-eye traffic lamp remains #010.

## Tasks

- [ ] Measure game units per meter with a reproducible physical/headset reference; verify IPD and translation parallax at several distances.
- [ ] Verify recenter, near clipping, on-foot and vehicle camera behavior with the accepted level-headset locomotion reference, without feeding pitch/roll into movement.
- [ ] Measure the delay from predicted pose to rendered image and submission. Replace the previous-callback pose pipeline when supported by measured scheduling evidence; retain one simulation update per stereo frame.
- [ ] Test water, mirror and interior/exterior reflections explicitly in both eyes. The first-image implementation shares auxiliary views generated from the game camera; decide which need separate eye views. Water mismatch reproduction and repair are owned by [#027](027-stereo-water-shading.md); use its evidence here rather than duplicating the investigation.
- [ ] Test rain/clouds, particles, glass, AO/history and other temporal effects while moving and turning in the headset. Capture reproducible failures and apply scoped fixes.
- [ ] Coordinate route timing with #013. The 90 Hz sample in #006 does not establish sustained native refresh on every route.
- [ ] Retest ordinary flat-screen mode after camera or render changes; track GTA IV and the episodes separately.

## Done when

Measured scale and pose timing are documented, the repeatable on-foot/vehicle route has correct parallax, and the tested auxiliary/temporal effects have no obvious eye mismatch or missing world geometry. Record unresolved compatibility separately rather than claiming episode support.

## Notes / evidence

- 2026-10-04 #014's first-person, collision-resolved room movement and normal headset-aiming route is user-accepted and closed, including the combined gameplay route and final flat-screen check. This does not measure world units per meter or pose latency, or establish controlled tracking-loss/auxiliary-view coverage; retain those checks here. Existing keyboard/gamepad locomotion intentionally uses level headset facing as requested; revise the earlier decoupled-direction test to validate this intended reference without allowing pitch/roll into movement.

- Initial baseline from #006: CE 1.2.0.59, SteamVR/OpenXR 2.17.10, RTX 4070 Ti, system D3D9Ex → D3D11, 2560×1440 per eye. Directional shadows passed the user's outdoor head-rotation check. Shared reflection/weather views remain based on the game camera, one game unit per meter is a prototype assumption, and rendered poses come from the previous OpenXR callback. Final Release runtime pacing sampled 90.00 projection layers/sec against a queried 90 Hz headset; no frame-time distribution or all-scene performance claim exists.
