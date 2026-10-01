# 017 — 6DOF hand and knife combat

## Goal

Make unarmed and knife attacks respond to tracked hand motion and world-space contact. Depends on #015/#016 hand transforms and #011 scale. Roadmap stage 6.

## Tasks

- [ ] Locate melee hit/damage and animation ownership; establish authoritative tracked hand/knife collision volumes.
- [ ] Use swept contact and motion thresholds for punches/stabs/swings with cooldowns or contact state to prevent repeated damage while resting against a target.
- [ ] Preserve enemy damage/reactions and add collision feedback/haptics without forced camera motion.
- [ ] Verify punches and knife attacks from different orientations, slow contact, tracking loss and ordinary flat-screen melee.

## Done when

Tracked punches and knife swings/stabs produce appropriate contact-based damage and feedback without duplicate hits or headset-directed aim. Recorded combat cases pass.

## Notes / evidence

- User requested VR controller 6DOF hand/knife combat. Exact damage tuning and blocking/grappling scope require design evidence; they are not assumed implemented.
- Requested on 2026-10-01. Implementation and runtime acceptance remain pending.
