# 015 — OpenXR controller support with Vive Wands

## Goal

Add tracked OpenXR controller input and controller-driven locomotion, initially verified with the user's Vive Wands on SteamVR. Depends on #005; integrate with the room-scale movement implemented in #014. Roadmap stage 4. This issue does not block #014's headset-only acceptance.

## Tasks

- [ ] Define semantic OpenXR actions for grip/aim poses, trigger, grip, trackpad locomotion/selection, menu and haptics; record the actual Vive Wand binding layout.
- [ ] Attach/synchronize action sets, validate pose validity and convert controller poses through the same tracking origin and world scale as the headset.
- [ ] Provide visible hands/controller proxies, left/right ownership and usable menu/locomotion bindings without conflicting game input.
- [ ] Define controller locomotion's directional reference and body-facing policy, keeping head rotation independent of movement. Verify combined controller and room-scale movement without double-counting physical translation.
- [ ] Verify both wands, tracking loss/recovery, disconnect/reconnect and VR-disabled keyboard/gamepad behavior.

## Done when

Both Vive Wands report correct tracked poses and usable bound inputs/haptics in the recorded runtime. Locomotion/menu actions work and invalid tracking produces controlled behavior. Movement-direction/body-facing policy is documented, and combined controller and room-scale movement passes without double-counting physical translation.

## Notes / evidence

- 2026-10-02 directional-reference decision: the user wants forward input to follow the headset's horizontal facing direction, with backward/strafe relative to that direction. #014 now covers adapting existing keyboard/gamepad locomotion to this reference; use the same reference for Vive Wand input here. Physical room-scale translation remains distinct from stick/key movement, and looking up/down must not cause vertical locomotion. Body-facing policy is still unverified.

- 2026-10-02 scope clarification: controller locomotion belongs here, not in #014. Controller runtime checks are deferred while #014 completes headset-only room-scale movement. The existing read-only input prototype remains unfinished #015 work.

- Started the input foundation needed by #014 on 2026-10-02: opt-in `ExperimentalControllerInput` creates/attaches one gameplay action set with per-hand trackpad vector, click and aim pose, suggested HTC Vive controller bindings, and action spaces owned by the existing OpenXR module. Sampling runs on the OpenXR callback thread and publishes a bounded snapshot; invalid tracking/focus and stale samples cannot provide movement intent. `input-status` exposes read-only diagnostic state. Builds/runtime checks and game locomotion injection are pending; this does not close the broader controller/haptic/hand-proxy acceptance gate.

- User hardware target: Vive Wands with SteamVR. No controller implementation or headset/controller route acceptance is claimed yet. This provides input infrastructure for #016–#022 and #024–#025.
- Requested on 2026-10-01. Implementation and runtime acceptance remain pending.
