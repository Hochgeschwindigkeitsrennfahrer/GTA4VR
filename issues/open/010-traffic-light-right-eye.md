# 010 — Restore traffic-light lamps in the right eye

## Goal

Render traffic-light lamps consistently in both eye images. Follow-up to #006; the user does not require this isolated defect to block #006.

## Tasks

- [ ] Capture a stationary traffic light in both eyes and identify its geometry, lamp and glow contributions.
- [ ] Trace the lamp's list-building, draw and visibility state across both eye passes. Compare with brake lights, which work in both eyes.
- [ ] Correct the identified per-eye state and verify head movement plus a traffic-light color transition.

## Done when

The same traffic-light lamps remain visible in both eyes across head motion and color changes, with a working VR-disabled path. Record the location, game/runtime/build, screenshots or headset observations and remaining limitations.

## Notes / evidence

- 2026-10-01: during continuous #006 stereo testing on GTA IV CE 1.2.0.59 / SteamVR 2.17.10, the user reported traffic-light lamps visible in the left eye but absent in the right. Brake lights work in both eyes. The user clarified that no other light or emission source shows this issue; it is specific to traffic lights.
- This is a possible lead for shared or consumed rendering state, not evidence of a general lighting failure or the shadow root cause. Its draw path is not yet identified. Preserve one game-process update per stereo pair when fixing it.
