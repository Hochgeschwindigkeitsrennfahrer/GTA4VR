# 007 — Attribute UI and post-processing draw order

## Goal

Measure where GTA IV's HUD, subtitles, phone, radar, pause map, and post-processing contribute pixels, and document camera/target ownership for later VR composition. Roadmap stages: 2–4. Builds on #003. UI presentation choices are deferred; this investigation is not a prerequisite for starting #014.

## Tasks

- [ ] Correlate render-phase callbacks, viewport commands, draw calls, and final pixels in representative free-roam, phone, subtitle, and pause-map frames.
- [ ] Identify the phase or draw path for subtitles, HUD, radar, 3D phone model, 2D phone overlay, and pause menu/map.
- [ ] Document source camera/target ownership and constraints for later composition. Placement and interaction choices belong to #020/#021/#022 and are deferred.
- [ ] Record post-processing order, target dependencies, and the effect of pausing or opening the phone.

## Done when

A repeatable capture and phase/draw timeline attributes each listed element to its source and existing final composition position, with camera/target ownership and constraints recorded for later implementation. Any unmatched pixels or transitions are recorded explicitly. Choosing the eventual VR UI presentation is not this issue's acceptance gate.

## Notes / evidence

- On 2026-10-01 the user deferred UI presentation decisions and prioritized first-person work. #014 may proceed while #007 remains open. Its required regression boundary is to preserve non-world radar/menu cameras and UI state, avoid duplicate UI/script updates, and verify pause/resume and flat-screen behavior. Investigate a specific boundary only if a camera change breaks it; full pixel attribution and UI redesign need not block camera bringup.
- #003 associated `PhoneModel` and `Script2d` with the phone, `FrontEnd` with the pause map, and mapped viewport-command order through G-buffer, lighting, DrawScene, radar, and post-processing. Those traces did not attribute final pixels or locate subtitles. See [`docs/rendering-pipeline.md`](../../docs/rendering-pipeline.md).
