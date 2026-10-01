# 023 — Fix world objective markers visible only in the left eye

## Goal

Render in-world objective markers, including parking-location markers, correctly in both eyes. Depends on #006; use #007 attribution where applicable. Roadmap stage 3.

## Tasks

- [ ] Capture a reproducible parking/objective marker in both eye images, recording mission/location and graphics configuration.
- [ ] Locate the marker draw path and inspect per-frame visibility stamps, command-list ownership, camera constants and target binding for both eyes.
- [ ] Apply the scoped fix so both eyes render the same world-space marker with correct parallax after one game update.
- [ ] Verify multiple marker orientations/distances and transitions, unrelated emissions (#010), and the flat-screen path.

## Done when

The reproduced marker is visible with correct placement/parallax in both eyes and does not disappear during head rotation or objective transitions. Before/after evidence is recorded.

## Notes / evidence

- User report on 2026-10-01: in-world objective markers such as parking indicators appear only in the left eye. This is distinct from the traffic-light lamp defect in #010; a shared cause is unproven.
- Requested on 2026-10-01. Implementation and runtime acceptance remain pending.
