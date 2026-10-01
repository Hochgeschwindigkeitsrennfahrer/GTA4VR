# 007 — Attribute UI and post-processing draw order

## Goal

Measure where GTA IV's HUD, subtitles, phone, radar, pause map, and post-processing contribute pixels so VR can place each element deliberately. Roadmap stages: 2–4. Builds on #003.

## Tasks

- [ ] Correlate render-phase callbacks, viewport commands, draw calls, and final pixels in representative free-roam, phone, subtitle, and pause-map frames.
- [ ] Identify the phase or draw path for subtitles, HUD, radar, 3D phone model, 2D phone overlay, and pause menu/map.
- [ ] Decide and verify per-eye, world-locked, or headset-locked treatment for each observed pass.
- [ ] Record post-processing order, target dependencies, and the effect of pausing or opening the phone.

## Done when

A repeatable capture and phase/draw timeline attributes each listed element to its source and final composition position, with an implementable VR treatment. Any unmatched pixels or transitions are recorded explicitly.

## Notes / evidence

- #003 associated `PhoneModel` and `Script2d` with the phone, `FrontEnd` with the pause map, and mapped viewport-command order through G-buffer, lighting, DrawScene, radar, and post-processing. Those traces did not attribute final pixels or locate subtitles. See [`docs/rendering-pipeline.md`](../../docs/rendering-pipeline.md).
