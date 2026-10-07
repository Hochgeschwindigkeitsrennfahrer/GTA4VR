# 034 — Correct spatial audio direction in VR

## Goal

Make positional sounds, including NPC voices, arrive from the correct direction relative to the headset.

## Tasks

- [ ] Reproduce a stationary sound source while turning the headset independently of the ordinary camera.
- [ ] Trace native listener position/orientation and when it updates, including vehicles and cutscenes.
- [ ] Bind the gameplay listener to the VR head center and orientation without duplicating audio per eye.
- [ ] Check nearby voices, traffic and moving sources while turning and moving, plus VR-disabled behavior.

## Done when

Positional sources remain attached to their world locations as the headset turns/moves, with correct left/right direction, and normal flat-screen audio remains intact.

## Notes / evidence

- 2026-10-07: User reports NPC speech and other 3D sounds come from incorrect directions. The old third-person camera may still control the listener; this is a hypothesis awaiting native analysis. Current setup: GTA IV Complete Edition 1.2.0.59, SteamVR 2.17.10, Vive Wands, DXVK/Vulkan VR.
