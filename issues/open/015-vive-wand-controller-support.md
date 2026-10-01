# 015 — OpenXR controller support with Vive Wands

## Goal

Add tracked OpenXR controller input, initially verified with the user's Vive Wands on SteamVR. Depends on #005; coordinate locomotion with #014. Roadmap stage 4.

## Tasks

- [ ] Define semantic OpenXR actions for grip/aim poses, trigger, grip, trackpad locomotion/selection, menu and haptics; record the actual Vive Wand binding layout.
- [ ] Attach/synchronize action sets, validate pose validity and convert controller poses through the same tracking origin and world scale as the headset.
- [ ] Provide visible hands/controller proxies, left/right ownership and usable menu/locomotion bindings without conflicting game input.
- [ ] Verify both wands, tracking loss/recovery, disconnect/reconnect and VR-disabled keyboard/gamepad behavior.

## Done when

Both Vive Wands report correct tracked poses and usable bound inputs/haptics in the recorded runtime. Locomotion/menu actions work and invalid tracking produces controlled behavior.

## Notes / evidence

- User hardware target: Vive Wands with SteamVR. No controller implementation or headset/controller route acceptance is claimed yet. This provides input infrastructure for #016–#022 and #024–#025.
- Requested on 2026-10-01. Implementation and runtime acceptance remain pending.
