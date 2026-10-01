# 025 — Physics-based money and weapon drops

## Goal

Make money and weapon drops physical items requiring hand pickup in VR instead of automatic collection when walking over them. Depends on #018; coordinate weapons/ammo with #016/#024. Roadmap stage 6.

## Tasks

- [ ] Map money/weapon spawn, despawn and automatic collection paths, including mission/script-critical pickups.
- [ ] Give supported drops collision/physics and hand-grabbable representations while preserving item identity and reward/inventory semantics.
- [ ] Disable proximity-only collection for supported VR drops; award an item exactly once on the defined physical pickup action and handle release/rejection.
- [ ] Test walking over items, picking them up, throwing/dropping weapons, duplicate-contact prevention, mission-critical cases and flat-screen pickup behavior.

## Done when

Supported drops remain physical until deliberately collected by hand; walking across them does not collect them. Rewards/ammo/inventory update exactly once and tested missions remain completable.

## Notes / evidence

- User requests money and weapons to be physically picked up. Exceptions or script-required pickup behavior must be identified and documented rather than silently breaking progression.
- Requested on 2026-10-01. Implementation and runtime acceptance remain pending.
