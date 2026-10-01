# 022 — Physical interactive in-world phone

## Goal

Replace the gameplay phone overlay with a physical phone that the player can hold and operate in the world. Depends on #015/#016/#020 and phone attribution in #007. Roadmap stage 6.

## Tasks

- [ ] Map phone model, screen/UI input and mission/script state; define summon/stow and hand ownership.
- [ ] Attach a physical phone to a tracked hand with a readable world-space screen in both eyes.
- [ ] Support controller/hand interaction for calls, messages, contacts and required mission functions; preserve ringing and scripted phone behavior.
- [ ] Test receiving/placing calls, reading messages, mission progression, stowing/dropping policy and flat-screen phone behavior.

## Done when

A held phone can be operated physically for the recorded required game functions without a duplicate overlay or blocked mission progression.

## Notes / evidence

- User wants the phone to be a physical in-world object. Existing PhoneModel/Script2d observations in #003/#007 are starting evidence, not a complete interaction mapping.
- Requested on 2026-10-01. Implementation and runtime acceptance remain pending.
