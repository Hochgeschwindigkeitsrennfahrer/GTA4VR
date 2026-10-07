# 033 — Melee feedback and compatibility follow-up

## Goal

Extend the user-accepted tracked melee foundation from closed #017 with physical
feedback and explicit compatibility coverage. Roadmap stage 6.

## Tasks

- [ ] Add contact haptics for fists and equipped melee weapons.
- [ ] Implement physical weapon blocking/contact response; preserve native damage protections and tracked arm ownership.
- [ ] Add bat/cue support-hand grip and verify weapon switching/hand ownership.
- [ ] Test tracking loss/recovery, pause/phone, vehicle transitions, death and owner replacement during combat.
- [ ] Record VR-disabled melee regression; no new flat-screen check was performed for the #017 closure.
- [ ] Record GTA IV, TLAD and TBoGT weapon/target coverage separately.

## Done when

Implemented feedback and supported hand ownership pass recorded gameplay cases;
lifecycle and flat-screen tests pass, and episode support/limitations are explicit.

## Notes / evidence

- Split from #017 on 2026-10-07 when the user requested closure after accepting
  fist/knife/bat/cue NPC hits and native vehicle/prop strikes. Existing deterministic
  tests cover strike motion/rearm/invalid samples; they do not establish runtime
  lifecycle or episode compatibility.
- Keep no upper swing-speed/displacement caps. Free-left-hand punches currently
  work with the standard pistol; other equipped firearm classes remain uncovered.
- General physics-car interaction owner/police attribution remains #032; firearm
  coverage and reloads remain #024.
