# 005 — Add opt-in OpenXR initialization

## Goal

Introduce an isolated, disabled-by-default OpenXR lifecycle before stereo rendering. Roadmap stage: 1. Depends on #001 and the runtime/bridge findings in #004.

## Tasks

- [ ] Add a VR enable setting whose default is off, with clear diagnostics for missing or incompatible runtimes.
- [ ] Create and destroy the OpenXR instance and session at defined lifecycle points; handle runtime loss and game/device reset without leaks or crashes.
- [ ] Prototype a frame loop and verify session states, frame timing, and clean shutdown. Keep world rendering unchanged in this issue.
- [ ] Re-run the #002 flat-screen route with VR disabled and record runtime/headset versions for enabled testing.

## Done when

Flat-screen mode still works with VR disabled; enabling VR reaches a recorded OpenXR session/frame-loop state on a supported setup and exits cleanly. A missing runtime falls back with a useful diagnostic.

## Notes / evidence

- The stereo headset image belongs to roadmap stage 2 and should be tracked in a later issue.
