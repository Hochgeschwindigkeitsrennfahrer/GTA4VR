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

### Closed as superseded — 2026-10-06

The core radar, native text/sprite HUD, loading and pause ownership findings were implemented and accepted in [#020](020-vr-overlays-and-subtitles.md). The original exhaustive timeline was not completed; its remaining work is consolidated into outcome-specific issues:

- [#031](../open/031-ui-coverage-and-lifecycle.md) owns unmatched UI pixels, draw attribution and transitions discovered during coverage/lifecycle checks.
- [#022](../closed/022-physical-interactive-phone.md) records the accepted native phone model/screen ownership and interaction mapping; broader phone route coverage remains #031.
- [#026](../closed/026-disable-cinematic-depth-blur.md) is now closed: the user disabled cinematic focus blur through an existing game option, so a custom bypass is unnecessary.

No separate full-pipeline investigation is required before those tasks. Historical observations below are retained; statements that this issue remains open describe its earlier scope.

- 2026-10-06 #020 radar watch: `00D76360` statically resembles a radar HUD composer but had zero calls in two CE1.2.0.59 VR runs; it is not the active boundary. Native CRadarNY ring textures are loaded at `005DB040`/`005DCA20` into `010344D0/D4`. `00903060` draws normalized health/armor ring geometry through `00903330`; runtime hook calls advance continuously. It runs in the render executor, so the watch copies the current viewport directly after original emission instead of appending simulation commands. Ghidra bookmarks distinguish unused and active candidates, and program saved. Pixel contents and health/armor acceptance pending. Full UI timeline remains open.
- On 2026-10-01 the user deferred UI presentation decisions and prioritized first-person work. #014 may proceed while #007 remains open. Its required regression boundary is to preserve non-world radar/menu cameras and UI state, avoid duplicate UI/script updates, and verify pause/resume and flat-screen behavior. Investigate a specific boundary only if a camera change breaks it; full pixel attribution and UI redesign need not block camera bringup.
- #003 associated `PhoneModel` and `Script2d` with the phone, `FrontEnd` with the pause map, and mapped viewport-command order through G-buffer, lighting, DrawScene, radar, and post-processing. Those traces did not attribute final pixels or locate subtitles. See [`docs/rendering-pipeline.md`](../../docs/rendering-pipeline.md).

- Frame-end source capture verifies the complete native radar (player/home/north markers plus health ring); copying immediately after00903060 yields only the ring. #020 therefore uses00903060 only for source viewport bounds, then copies the completed ordinary back buffer before explicit eye composition. Final-eye surfaces must bypass the ordinary outside-eye redirect to the desktop. Fresh post-draw eye capture verifies watch pixels on the left forearm.

- #020 loading/pause composition uses the completed ordinary back buffer in the OpenXR eye-image provider, before headset transfer/desktop-eye mirroring. A dedicated quad layer submits independently of world target creation; transition evidence shows555 panel frames before ordinary stereo gameplay. Wanted state is read through existing native APIs on the simulation callback, rendered procedurally above the watch instead of relying on unlocated HUD-star pixel bounds. Native loading/pause pixel capture, input and reset acceptance remain part of #020; broader HUD attribution remains open.

### Current attribution — 2026-10-06

#020's core UI presentation is accepted. Subtitles/help/native text resolve through font drawing00923200 (command executor008DDD50 consumes stream state and must not be replayed); sprite/window payload drawing uses008DDC90/008DDD00. Their transparent capture is composed after scene completion into both eye targets. SMS icon texture slot is resolved from005CB80A; this icon is excluded from the general HUD and routed to the radar. See docs/wrist-radar.md and saved Ghidra #020 bookmarks. Radar/menu/loading source ownership is documented there. Mark these attribution findings as established; remaining work is the full phone/post-processing timeline and any unmatched UI pixels/transitions found by #031. This issue stays open for that investigation, not for already accepted placement or menu interaction.
