# 021 — VR weapon wheel

## Goal

Provide a controller-operated weapon wheel with readable selection and reliable weapon switching. Depends on #015/#020; coordinate equipped weapon transforms with #016. Roadmap stage 4.

## Tasks

- [ ] Identify inventory/weapon-selection hooks and define wheel open, selection, confirm/cancel and handedness bindings for Vive Wands.
- [ ] Render readable weapon/ammo choices in both eyes with tracked pointing or trackpad selection.
- [ ] Handle empty slots, ammo changes, paused/time-scaled state and transitions without unintended firing.
- [ ] Test selecting each supported weapon, cancellation and return to gameplay while walking/driving where supported; verify flat-screen switching.

## Done when

The player can open, navigate, select and cancel the wheel using Vive Wands. Inventory/ammo display and equipped weapon agree, without accidental shots or stuck input.

## Notes / evidence

- User requested a weapon wheel. Interaction layout is to be designed and verified; do not assume an existing GTA IV wheel supplies this feature.
- Requested on 2026-10-01. Implementation and runtime acceptance remain pending.
