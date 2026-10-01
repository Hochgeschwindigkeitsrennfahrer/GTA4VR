# 003 — Map camera and rendering hooks

## Goal

Identify where independent eye views can be produced without running game simulation twice. Roadmap stage: 1. Depends on #001 and benefits from #002.

## Tasks

- [ ] Trace the game camera, view/projection matrices, and culling inputs used during world rendering.
- [ ] Trace scene, HUD, subtitles, phone/map, and post-processing order for one normal frame.
- [ ] Measure or log invocation order for relevant hooks in `source/fusiondxhook.ixx`, `source/dllmain.cpp`, `source/common.ixx`, `source/comvars.ixx`, and `source/postfx.ixx`.
- [ ] Write a short design note with annotated hook points, unresolved risks, and a candidate boundary for per-eye scene rendering.

## Done when

The design note gives evidence for a stereo scene-render boundary and explains which passes are per-eye versus headset-locked. An EndScene callback alone is not sufficient evidence of stereo feasibility.

## Notes / evidence

- No hook trace has been recorded yet.
