# 018 — Grab and manipulate physics objects with 6DOF hands

## Goal

Allow tracked hands to pick up, carry, rotate, release and throw eligible game physics objects. Depends on #015/#016. Roadmap stage 6.

## Tasks

- [ ] Identify eligible dynamic objects and game physics ownership; define reach, grip detection and object mass/size limits.
- [ ] Implement a physical grab constraint or equivalent verified interaction that follows the hand while respecting world collision.
- [ ] Derive release/throw velocity from valid tracked motion; handle two hands, tracking loss and object deletion cleanly.
- [ ] Test picking up, carrying, rotating, dropping and throwing several objects, including contact with scenery and flat-screen regression.

## Done when

Eligible objects can be manipulated in six degrees of freedom and released with coherent physics, without unstable constraints, duplicated objects or retained grabs after tracking loss.

## Notes / evidence

- This is the shared physics-grab foundation. Inventory/reward pickup behavior for money and weapon drops is tracked in #025.
- Requested on 2026-10-01. Implementation and runtime acceptance remain pending.
