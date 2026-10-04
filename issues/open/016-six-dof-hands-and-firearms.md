# 016 — 6DOF hands, firearm aiming and shooting

## Goal

Control hands and firearms with controller position and rotation so shots follow the actual weapon muzzle. Depends on #014, #015 and measured scale in #011; physical reloads are #024. Roadmap stage 4.

## Tasks

- [ ] Map controller grip/aim poses to hand and weapon transforms, documenting per-weapon grip and muzzle offsets.
- [ ] Trace/project shots from the world-space muzzle using weapon orientation rather than headset or third-person aim; preserve damage, ammo, recoil and collision semantics.
- [ ] Support dominant/off hand use and two-hand stabilization where appropriate; define aim assistance and obstruction behavior.
- [ ] Test representative pistols, long guns and other weapon classes, including close targets, rotated/sideways poses, muzzle obstruction and flat-screen regression.

## Done when

Representative firearms move in six degrees of freedom and aim/fire correctly from their muzzle independently of head direction. Hit/damage/ammo evidence, supported weapon classes and limitations are recorded.

## Notes / evidence

- 2026-10-04 #014 closes with user-accepted normal headset-directed on-foot gun shots, right-stick independence and controller lock-on suppression. This is the interim aiming model. Separate FPS/sniper camera/scopes and special weapon modes remain unverified; include those in this issue's per-weapon coverage and coordinate a scope presentation policy with #020. See docs/headset-aiming.md and Ghidra Issue014HeadsetAimBasis; do not infer 6DOF muzzle ownership from camera aiming acceptance.

- User explicitly requests guns and other weapons to work with proper 6DOF aiming and shooting. Reload handling must coordinate with #024 rather than declare conventional reload input the final result.
- Requested on 2026-10-01. Implementation and runtime acceptance remain pending.
