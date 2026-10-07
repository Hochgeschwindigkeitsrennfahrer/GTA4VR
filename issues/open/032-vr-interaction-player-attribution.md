# 032 — Attribute VR physical interactions to the player

## Goal

Make native NPC, vehicle-owner and police reactions recognize what the player does with tracked hands. Depends on #018's physical contact/grab adapters; coordinate combat damage with #017.

## Tasks

- [ ] Trace native player-attributed vehicle contact, damage, window breaking and entry/open-attempt events, including police vehicles and lock states.
- [ ] Attribute actual arm-collider damage and deliberate door-open attempts to the player using the native event path. Grabbing a locked panel must preserve its lock and latch.
- [ ] Distinguish touching/holding from damage or an entry attempt; debounce persistent contacts and repeated held-grip updates.
- [ ] Preserve native ownership, wanted-level, witness, mission and episode rules instead of inventing wanted-star or NPC aggression logic.
- [ ] Verify representative civilian and police vehicles, occupied/unoccupied and locked/unlocked; record GTA IV, TLAD and TBoGT coverage separately.

## Done when

Comparable VR and ordinary player actions produce appropriate native reactions: window damage and deliberate opening attempts are attributed correctly, innocuous contact does not repeatedly provoke reactions, and locked cars remain locked. Existing flat-screen behavior and mission ownership remain intact.

## Notes / evidence

2026-10-07: User accepts locked-car grasping in #018. Native entry into a police car can give a wanted star even when locked, and native damage/contact can provoke the owner. Arm colliders already break vehicle windows physically, but player attribution and related reactions have not been established. This issue records the missing integration; no completion claim.
