# 019 — VR vehicle interaction and driving

## Goal

Provide usable VR vehicle cameras and controls for cars, bikes and available aircraft. Depends on #014/#015; coordinate aiming with #016. Roadmap stage 4.

## Tasks

- [ ] Inventory actual vehicle classes in GTA IV, TLAD and TBoGT separately. Assess the user's planes request against available aircraft; document any requirement for additional content.
- [ ] Define seated vehicle tracking origins, entry/exit, recenter and head/body collision; remove forced headset rotation where possible.
- [ ] Implement VR driving/riding/flying controls and necessary cockpit/dashboard feedback; define physical hand interactions and weapon use per supported vehicle class.
- [ ] Test entering/exiting, driving/riding/flying, collisions, passenger views and camera transitions; record comfort/performance and flat-screen results.

## Done when

A documented route passes for each supported vehicle class with usable controls and coherent headset camera/entry/exit. Aircraft coverage and any unavailable plane gameplay are stated explicitly.

## Notes / evidence

- User requests cars/bikes/planes. Do not assume the base game has playable planes or silently replace that requirement with helicopters. Split class-specific implementation issues when the inventory establishes scope.
- Requested on 2026-10-01. Implementation and runtime acceptance remain pending.
